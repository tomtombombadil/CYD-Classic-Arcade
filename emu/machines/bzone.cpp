// Battlezone machine - see bzone.h.
//
// Memory map (A15 not decoded: 8000-FFFF mirror 0000-7FFF):
//   0000-03FF  RAM
//   0800       IN0: 0-2 coins (low = in), 3 slam, 4 self-test, 5 diagnostic
//              step (low = on), 6 AVG halted, 7 the 3 kHz clock
//   0A00 / 0C00  the switches at M10 (game options) / P10 (pricing)
//   1000  coin counters                 1200  AVG go
//   1400  watchdog                      1600  AVG reset
//   1800  math box status               1810 / 1818  math box result low / high
//   1820-182F  POKEY (ALLPOT = the tread sticks, fire and starts - high when
//              pressed)
//   1840  sound latch (bzone_sound)     1860-187F  math box commands
//   2000-2FFF  vector RAM               3000-3FFF  vector ROM
//   5000-7FFF  program
// Facts from MAME's driver (read for facts only).
#include "bzone.h"

#include <cstring>

namespace bzone {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x3000},
    {"vector",  0x1000},
};

const char* const kVariants[] = {"bzone", "bzonea"};

// CRCs as in MAME's "bzone" (Rev 2, variant 1) and "bzonea" (Rev 1,
// variant 2); the other chips are the same in both.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,    0x0000, 0x0800, 0x13de36d5, "036414-02.e1",  false, 1},
    {kRegionCpu,    0x0000, 0x0800, 0xefbc3fa0, "036414-01.e1",  false, 2},
    {kRegionCpu,    0x0800, 0x0800, 0x5d9d9111, "036413-01.h1",  false},
    {kRegionCpu,    0x1000, 0x0800, 0xab55cbd2, "036412-01.j1",  false},
    {kRegionCpu,    0x1800, 0x0800, 0xad281297, "036411-01.k1",  false},
    {kRegionCpu,    0x2000, 0x0800, 0x0b7bfaa4, "036410-01.lm1", false},
    {kRegionCpu,    0x2800, 0x0800, 0x1e14e919, "036409-01.n1",  false},
    {kRegionVector, 0x0000, 0x0800, 0x7414177b, "036422-01.bc3", false},
    {kRegionVector, 0x0800, 0x0800, 0x8ea8f939, "036421-01.a3",  false},
};

// The AVG's centre in its 16.16 units.
constexpr int32_t kCenterX = (kAvgW / 2) << 16, kCenterY = (kAvgH / 2) << 16;

// 16.16 units -> 1/16 pixels: square units, the window's 580 units across
// the 320 columns (400 units = 220.7 rows).
constexpr int64_t kNum = int64_t(kScreenW) * vector_display::kSub, kDen = int64_t(kAvgW) << 16;
inline int to_px(int32_t v) { return int(int64_t(v) * kNum / kDen); }
// The overlay's edge: rows above it red, from it green (the top fifth).
constexpr int kRedRows = int(int64_t(kRedUnits) * kScreenW / kAvgW);     // 44
constexpr int kEdge16 = kRedRows * vector_display::kSub;

} // namespace

const romset::Set kRomSet = {
    "bzone", nullptr, "Battlezone", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

uint16_t treads(uint16_t buttons, int ly, int ry)
{
    constexpr int kThird = 170;
    if (ly > -kThird && ly < kThird && ry > -kThird && ry < kThird) return buttons;
    uint16_t b = uint16_t(buttons & ~(kLeftFwd | kLeftBack | kRightFwd | kRightBack));
    if (ly <= -kThird) b |= kLeftFwd;
    if (ly >= kThird) b |= kLeftBack;
    if (ry <= -kThird) b |= kRightFwd;
    if (ry >= kThird) b |= kRightBack;
    return b;
}

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate ? audio_rate : 24000;
    swap_ = swap_rgb565_bytes;
    // The overlay's colours (red 1 / 0.125 / 0.125, green 0.125 / 1 /
    // 0.125, as MAME's artwork) times the intensity, with a floor so dim
    // lines show on an LCD.
    for (int i = 0; i < 16; ++i) {
        const int k = 64 + i * 192 / 15;
        const int hi = 0xff * k >> 8, lo = 0x20 * k >> 8;
        uint16_t r = uint16_t((hi >> 3) << 11 | (lo >> 2) << 5 | (lo >> 3));
        uint16_t g = uint16_t((lo >> 3) << 11 | (hi >> 2) << 5 | (lo >> 3));
        if (swap_) {
            r = uint16_t((r >> 8) | (r << 8));
            g = uint16_t((g >> 8) | (g << 8));
        }
        pen_[0][i] = r;
        pen_[1][i] = g;
    }
    display_.init(kScreenW, kScreenH);
    avg_.init(vec_read, nullptr, this, 0x2000, kCenterX, kCenterY, atari_avg::Variant::Battlezone);
    avg_.map(0, vram_);             // 2000-2FFF
    avg_.map(1, roms_.vector);      // 3000-3FFF
    pokey_.init(kCpuClock, audio_rate_);
    sound_.init(audio_rate_);

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    for (int half = 0; half < 2; ++half) {
        const uint16_t o = uint16_t(half << 15);
        cpu_.map_ram(o, 0x400, ram_);
        cpu_.map_ram(uint16_t(o + 0x2000), 0x1000, vram_);
        cpu_.map_read(uint16_t(o + 0x3000), 0x1000, roms_.vector);
        cpu_.map_read(uint16_t(o + 0x5000), 0x3000, roms_.cpu);
    }
    reset();
}

void Machine::reset()
{
    std::memset(ram_, 0, sizeof ram_);
    avg_.reset();
    mathbox_.reset();
    pokey_.reset();
    sound_.reset();
    vg_busy_until_ = 0;
    fc_ = 0;
    watchdog_at_ = total_;
    cpu_.reset();
}

uint8_t Machine::vec_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (addr < 0x3000) return m.vram_[addr & 0xfff];
    if (addr < 0x4000) return m.roms_.vector[addr & 0xfff];
    return 0;
}

void Machine::vg_go()
{
    // Each line in the overlay's colour: cut in two where it crosses the
    // red / green edge.
    struct Sink {
        Machine& m;
        void operator()(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int, int level)
        {
            int ax = to_px(x0), ay = to_px(y0), bx = to_px(x1), by = to_px(y1);
            if (ay > by) {
                const int tx = ax, ty = ay;
                ax = bx; ay = by; bx = tx; by = ty;
            }
            const uint16_t* pen = m.pen_[0];
            if (ay < kEdge16 && by >= kEdge16) {
                const int xm = ax + int(int64_t(bx - ax) * (kEdge16 - ay) / (by - ay));
                m.display_.add(ax, ay, xm, kEdge16 - 1, pen[level & 15]);
                m.display_.add(xm, kEdge16, bx, by, m.pen_[1][level & 15]);
                return;
            }
            if (ay >= kEdge16) pen = m.pen_[1];
            m.display_.add(ax, ay, bx, by, pen[level & 15]);
        }
    } sink{*this};
    display_.begin();
    const int clocks = avg_.run_lines<atari_avg::Variant::Battlezone>(sink);
    display_.finish();
    vg_busy_until_ = now() + uint32_t(clocks);
    ++vg_runs_;
}

uint8_t Machine::in3() const
{
    // A tread with both of its switches pressed counts as neither (the
    // stick can't do that; the d-pad's diagonals rely on it).
    uint8_t v = 0;
    const bool lf = buttons_ & kLeftFwd, lb = buttons_ & kLeftBack;
    const bool rf = buttons_ & kRightFwd, rb = buttons_ & kRightBack;
    if (rb && !rf) v |= 0x01;
    if (rf && !rb) v |= 0x02;
    if (lb && !lf) v |= 0x04;
    if (lf && !lb) v |= 0x08;
    if (buttons_ & kFire) v |= 0x10;
    if (buttons_ & kStart1) v |= 0x20;
    if (buttons_ & kStart2) v |= 0x40;
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    addr &= 0x7fff;
    switch (addr & 0x7e00) {
    case 0x0800: {
        uint8_t v = 0x3f;
        if (m.buttons_ & kCoin) v &= uint8_t(~0x01);
        if (m.buttons_ & kCoin2) v &= uint8_t(~0x02);
        if (m.buttons_ & kSlam) v &= uint8_t(~0x08);
        if (m.buttons_ & kTest) v &= uint8_t(~0x10);
        if (m.buttons_ & kDiagStep) v &= uint8_t(~0x20);
        if (int32_t(m.vg_busy_until_ - m.now()) <= 0) v |= 0x40;
        if (m.now() & 0x100) v |= 0x80;
        return v;
    }
    case 0x0a00: return m.dsw0_;
    case 0x0c00: return kPricing1Coin1Credit;
    case 0x1800:
        switch (addr & 0xf8) {
        case 0x00: return 0x00;                        // math box done
        case 0x10: return m.mathbox_.lo();
        case 0x18: return m.mathbox_.hi();
        case 0x20:
        case 0x28:
            m.pokey_.set_allpot(m.in3());
            return m.pokey_.read(uint8_t(addr & 15), m.now());
        default: return 0;
        }
    default:
        return 0;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    addr &= 0x7fff;
    switch (addr & 0x7e00) {
    case 0x1200:
        m.vg_go();
        break;
    case 0x1400:
        m.watchdog_at_ = m.now();
        break;
    case 0x1600:
        m.avg_.reset();
        m.vg_busy_until_ = m.now();
        break;
    case 0x1800:
        if ((addr & 0xf0) == 0x20) {
            m.audio_to(m.fc_ + m.cpu_.elapsed());
            m.pokey_.write(uint8_t(addr & 15), v, m.now());
        } else if ((addr & 0xf0) == 0x40) {
            m.audio_to(m.fc_ + m.cpu_.elapsed());
            m.sound_.write(v);
        } else if ((addr & 0xe0) == 0x60) {
            m.mathbox_.go(addr & 0x1f, v);
        }
        break;
    default:
        break;
    }
}

// The main loop's two waits, found by their code (both revisions):
//   LSR zp; BCC back   - for the NMI handler's count (every 16th NMI) - 8
//                        cycles a turn while zp is 0
//   LDX zp; BEQ back   - for the NMI handler to swap the picture buffers -
//                        6 cycles a turn while zp is 0
// Nothing but the NMI ends either, so whole turns up to the next NMI are
// skipped and the CPU runs the rest itself: it ends where running every
// turn would have (exact - the same instructions at the same cycles).
void Machine::skip_idle(int target)
{
    for (int tries = 0; tries < 2; ++tries) {
        const uint16_t pc = cpu_.pc;
        const uint8_t op = cpu_.rd(pc);
        int turn = 0;
        if ((op == 0x46 || op == 0xa6) && cpu_.rd(uint16_t(pc + 3)) == 0xfc &&
            cpu_.rd(uint16_t(pc + 2)) == (op == 0x46 ? 0x90 : 0xf0) && ((pc ^ (pc + 4)) & 0xff00) == 0) {
            if (ram_[cpu_.rd(uint16_t(pc + 1))] != 0) return;
            turn = op == 0x46 ? 8 : 6;
        } else if ((op == 0x90 || op == 0xf0) && cpu_.rd(uint16_t(pc + 1)) == 0xfc && pc >= 2 &&
                   cpu_.rd(uint16_t(pc - 2)) == (op == 0x90 ? 0x46 : 0xa6)) {
            // On the branch: take it (one instruction) when it goes back,
            // then look again from the loop's start.
            const bool back = op == 0x90 ? !(cpu_.p() & m6502::CF) : (cpu_.p() & m6502::ZF);
            if (!back || ram_[cpu_.rd(uint16_t(pc - 1))] != 0 || target - fc_ < 32) return;
            fc_ += cpu_.run(1);
            continue;
        } else {
            return;
        }
        const int turns = (target - fc_) / turn - 1;
        if (turns > 0) {
            fc_ += turns * turn;
            idle_cycles_ += uint32_t(turns * turn);
        }
        return;
    }
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    int16_t* out = audio_ + audio_done_;
    const int n = target - audio_done_;
    pokey_.render(out, n);
    sound_.render(out, n);
    audio_done_ = target;
}

int Machine::run_frame(int16_t* audio, int cap)
{
    ++frame_;
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio;
    audio_n_ = audio ? n : 0;
    audio_done_ = 0;

    for (int k = 1; k <= 4; ++k) {
        const int target = k * kNmiCycles;
        while (fc_ < target) {
            const int slice = target - fc_ < kIdleSlice ? target - fc_ : kIdleSlice;
            fc_ += cpu_.run(slice);
            if (idle_skip_ && fc_ < target) skip_idle(target);
        }
        if (!(buttons_ & kTest)) cpu_.nmi();
    }
    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    fc_ -= kCyclesPerFrame;
    total_ += kCyclesPerFrame;

    if (int32_t(total_ - watchdog_at_) > kWatchdogCycles) {
        ++watchdog_resets_;
        reset();
    }
    return audio ? n : 0;
}

} // namespace bzone
