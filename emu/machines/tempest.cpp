// Tempest machine - see tempest.h.
//
// Memory map:
//   0000-07FF  RAM               0800-080F  colour RAM (write)
//   0C00       IN0: 0-2 coins (right, centre, left; low = in), 3 slam,
//              4 self-test, 5 diagnostic step (low = on), 6 AVG halted,
//              7 the 3 kHz clock
//   0D00 / 0E00  the switches at N13 / L12
//   2000-2FFF  vector RAM        3000-3FFF  vector ROM
//   4000  coin counters, picture flip   4800  AVG go
//   5000  watchdog + IRQ acknowledge    5800  AVG reset
//   6000-603F  EAROM address / data     6040  EAROM control (write),
//              math box status (read)   6050  EAROM data (read)
//   6060 / 6070  math box result low / high   6080-609F  math box commands
//   60C0-60CF  POKEY 1 (ALLPOT = the spinner (bits 0-3), cabinet)
//   60D0-60DF  POKEY 2 (ALLPOT = difficulty, rating, buttons, starts)
//   60E0  start lamps, player select     9000-DFFF  program (F000-FFFF
//              = D000-DFFF, for the vectors)
// Facts from MAME's driver (read for facts only).
#include "tempest.h"

#include <cstring>

namespace tempest {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x5000},
    {"vector",  0x1000},
};

const char* const kVariants[] = {"tempest", "tempest3"};

// CRCs as in MAME's "tempest" (rev 3 on 4 KB chips, variant 1) and
// "tempest3" (the same bytes on 2 KB chips, variant 2).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,    0x0000, 0x1000, 0x1d0cc503, "136002-133.d1",  false, 1},
    {kRegionCpu,    0x1000, 0x1000, 0xc88e3524, "136002-134.f1",  false, 1},
    {kRegionCpu,    0x2000, 0x1000, 0xa4b2ce3f, "136002-235.j1",  false, 1},
    {kRegionCpu,    0x3000, 0x1000, 0x65a9a9f9, "136002-136.lm1", false, 1},
    {kRegionCpu,    0x4000, 0x1000, 0xde4e9e34, "136002-237.p1",  false, 1},
    {kRegionVector, 0x0000, 0x1000, 0x9995256d, "136002-138.np3", false, 1},
    {kRegionCpu,    0x0000, 0x0800, 0x65d61fe7, "136002-113.d1",  false, 2},
    {kRegionCpu,    0x0800, 0x0800, 0x11077375, "136002-114.e1",  false, 2},
    {kRegionCpu,    0x1000, 0x0800, 0xf3e2827a, "136002-115.f1",  false, 2},
    {kRegionCpu,    0x1800, 0x0800, 0xaeb0f7e9, "136002-316.h1",  false, 2},
    {kRegionCpu,    0x2000, 0x0800, 0xef2eb645, "136002-217.j1",  false, 2},
    {kRegionCpu,    0x2800, 0x0800, 0xbeb352ab, "136002-118.k1",  false, 2},
    {kRegionCpu,    0x3000, 0x0800, 0xa4de050f, "136002-119.lm1", false, 2},
    {kRegionCpu,    0x3800, 0x0800, 0x35619648, "136002-120.mn1", false, 2},
    {kRegionCpu,    0x4000, 0x0800, 0x73d38e47, "136002-121.p1",  false, 2},
    {kRegionCpu,    0x4800, 0x0800, 0x707bd5c3, "136002-222.r1",  false, 2},
    {kRegionVector, 0x0000, 0x0800, 0x29f7e937, "136002-123.np3", false, 2},
    {kRegionVector, 0x0800, 0x0800, 0xc16ec351, "136002-124.r3",  false, 2},
};

// The AVG's centre in its 16.16 units.
constexpr int32_t kCenterX = (kAvgW / 2) << 16, kCenterY = (kAvgH / 2) << 16;

} // namespace

const romset::Set kRomSet = {
    "tempest", nullptr, "Tempest", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate ? audio_rate : 24000;
    swap_ = swap_rgb565_bytes;
    display_.init(kScreenW, kScreenH);
    avg_.init(vec_read, vec_line, this, 0x2000, kCenterX, kCenterY);
    avg_.map(0, vram_);             // 2000-2FFF
    avg_.map(1, roms_.vector);      // 3000-3FFF
    for (pokey::Pokey& p : pokey_) p.init(kCpuClock, audio_rate_);
    dc_r_ = 1.0f - 6.2831853f * 15.0f / float(audio_rate_);

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.map_ram(0x0000, 0x800, ram_);
    cpu_.map_ram(0x2000, 0x1000, vram_);
    cpu_.map_read(0x3000, 0x1000, roms_.vector);
    cpu_.map_read(0x9000, 0x5000, roms_.cpu);
    cpu_.map_read(0xf000, 0x1000, roms_.cpu + 0x4000);
    std::memset(earom_, 0xff, sizeof earom_);
    reset();
}

void Machine::reset()
{
    std::memset(ram_, 0, sizeof ram_);
    avg_.reset();
    mathbox_.reset();
    for (pokey::Pokey& p : pokey_) p.reset();
    vg_busy_until_ = 0;
    vg_started_ = false;
    player2_ = 0;
    earom_ctl_ = 0;
    fc_ = 0;
    watchdog_at_ = total_;
    cpu_.irq(false);
    cpu_.reset();
}

uint8_t Machine::vec_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (addr < 0x3000) return m.vram_[addr & 0xfff];
    if (addr < 0x4000) return m.roms_.vector[addr & 0xfff];
    return 0;
}

void Machine::vec_line(void* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level)
{
    Machine& m = *static_cast<Machine*>(ctx);
    // The colour RAM (bits low = on): 3 green, 2 blue, 1 red (+ 0, a little
    // more red); the intensity scales it, with a floor so dim lines show.
    const uint8_t d = uint8_t(~m.colorram_[color & 15]);
    const int r = ((d >> 1) & 1) * 0xf3 + (d & 1) * 0x0c, g = ((d >> 3) & 1) * 0xf3, b = ((d >> 2) & 1) * 0xf3;
    const int k = 64 + level * 192 / 15;
    uint16_t c = uint16_t(((r * k >> 8) >> 3) << 11 | ((g * k >> 8) >> 2) << 5 | ((b * k >> 8) >> 3));
    if (m.swap_) c = uint16_t((c >> 8) | (c << 8));
    // 16.16 units -> 1/16 pixels: the 580 x 570 window stretched over
    // 240 x 320.
    constexpr int64_t sx = int64_t(kScreenW) * vector_display::kSub, sy = int64_t(kScreenH) * vector_display::kSub;
    auto px = [&](int32_t x) { return int(int64_t(x) * sx / (int64_t(kAvgW) << 16)); };
    auto py = [&](int32_t y) { return int(int64_t(y) * sy / (int64_t(kAvgH) << 16)); };
    m.display_.add(px(x0), py(y0), px(x1), py(y1), c);
}

void Machine::vg_draw()
{
    display_.begin();
    const int clocks = avg_.run();
    display_.finish();
    vg_busy_until_ = now() + uint32_t(clocks);
    ++vg_runs_;
}

uint8_t Machine::in1() const
{
    // The spinner's 4-bit count, upright cabinet (bit 4), bits 5-7 high.
    return uint8_t(0xf0 | knob());
}

uint8_t Machine::in2() const
{
    // Medium difficulty (bits 0-1 = 3), rating 1-9 (bit 2); buttons and
    // starts low when pressed: 3 Superzapper, 4 fire, 5 start 1, 6 start 2.
    uint8_t v = 0xff;
    if (buttons_ & kZap) v &= uint8_t(~0x08);
    if (buttons_ & kFire) v &= uint8_t(~0x10);
    if (buttons_ & kStart1) v &= uint8_t(~0x20);
    if (buttons_ & kStart2) v &= uint8_t(~0x40);
    return v;
}

void Machine::earom_control(uint8_t v)
{
    // CK = bit 0, C1 = !bit 2, C2 = bit 1, CS1 = bit 3. On a clock's
    // falling edge while selected: C1 = read; else C2 = erase, neither =
    // write (an AND: bits only clear until erased).
    const bool falling = (earom_ctl_ & 1) && !(v & 1);
    earom_ctl_ = v;
    if (!(v & 0x08)) return;
    const bool c1 = !(v & 0x04), c2 = (v & 0x02) != 0;
    if (falling && c1) earom_out_ = earom_[earom_addr_];
    if (!c1 && !c2) earom_[earom_addr_] &= earom_data_;
    else if (!c1 && c2) earom_[earom_addr_] = 0xff;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr & 0xff00) {
    case 0x0c00: {
        uint8_t v = 0x3f;
        if (m.buttons_ & kCoin2) v &= uint8_t(~0x02);
        if (m.buttons_ & kCoin) v &= uint8_t(~0x04);
        if (m.buttons_ & kSlam) v &= uint8_t(~0x08);
        if (m.buttons_ & kTest) v &= uint8_t(~0x10);
        if (m.buttons_ & kDiagStep) v &= uint8_t(~0x20);
        if (int32_t(m.vg_busy_until_ - m.now()) <= 0) v |= 0x40;
        if (m.now() & 0x100) v |= 0x80;
        return v;
    }
    case 0x0d00: return m.dsw1_;
    case 0x0e00: return 0x00;
    case 0x6000:
        switch (addr & 0xf0) {
        case 0x40: return 0x00;                        // math box done
        case 0x50: return m.earom_out_;
        case 0x60: return m.mathbox_.lo();
        case 0x70: return m.mathbox_.hi();
        case 0xc0:
        case 0xd0: {
            const int n = (addr >> 4) & 1;
            pokey::Pokey& p = m.pokey_[n];
            p.set_allpot(n ? m.in2() : m.in1());
            return p.read(uint8_t(addr & 15), m.now());
        }
        default: return 0;
        }
    default:
        return 0;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr & 0xff00) {
    case 0x0800:
        m.colorram_[addr & 15] = v;
        break;
    case 0x4800:
        m.vg_started_ = true;
        break;
    case 0x5000:
        m.cpu_.irq(false);
        m.watchdog_at_ = m.now();
        break;
    case 0x5800:
        m.avg_.reset();
        break;
    case 0x6000:
        if (addr < 0x6040) {
            m.earom_addr_ = addr & 0x3f;
            m.earom_data_ = v;
        } else if ((addr & 0xf0) == 0x40) {
            m.earom_control(v);
        } else if ((addr & 0xe0) == 0x80) {
            m.mathbox_.go(addr & 0x1f, v);
        } else if ((addr & 0xe0) == 0xc0) {
            m.audio_to(m.fc_ + m.cpu_.elapsed());
            m.pokey_[(addr >> 4) & 1].write(uint8_t(addr & 15), v, m.now());
        } else if ((addr & 0xf0) == 0xe0) {
            m.player2_ = v & 0x04;
        }
        break;
    default:
        break;
    }
}

// The main loop paces itself on the IRQ count: LDA zp; CMP #n; BCC back,
// spinning until the IRQ handler has counted to n (C7A7 in rev 3). Found
// by its code; while the count is short, nothing else happens until the
// next IRQ, so that time is skipped.
bool Machine::waiting_for_irq()
{
    const uint16_t pc = cpu_.pc;
    for (int back = 0; back <= 4; back += 2) {
        const uint16_t at = uint16_t(pc - back);
        if (cpu_.rd(at) == 0xa5 && cpu_.rd(uint16_t(at + 2)) == 0xc9 && cpu_.rd(uint16_t(at + 4)) == 0x90 &&
            cpu_.rd(uint16_t(at + 5)) == 0xfa)
            return ram_[cpu_.rd(uint16_t(at + 1))] < cpu_.rd(uint16_t(at + 3)) && cpu_.irq_line() == false;
    }
    return false;
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    int16_t* out = audio_ + audio_done_;
    const int n = target - audio_done_;
    pokey_[0].render(out, n);
    pokey_[1].render(out, n, true);
    // Two chips at up to 60 x 256 each, around their middle: AC-coupled,
    // scaled so the loudest game sounds peak near 20,000 (a full-volume
    // chord of all eight channels would clip; the game never plays one).
    for (int i = 0; i < n; ++i) {
        const float x = float(out[i]) * 4.0f;
        dc_y_ = x - dc_x_ + dc_r_ * dc_y_;
        dc_x_ = x;
        if (dc_y_ > -1e-3f && dc_y_ < 1e-3f) dc_y_ = 0;
        const float y = dc_y_ > 32767 ? 32767 : dc_y_ < -32768 ? -32768 : dc_y_;
        out[i] = int16_t(y);
    }
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

    // The spinner: a steady speed from the buttons plus the stick's
    // (clockwise counts down).
    int speed = spin_speed_;
    if (buttons_ & kSpinRight) speed += kButtonSpin;
    if (buttons_ & kSpinLeft) speed -= kButtonSpin;
    spin_pos_ -= uint32_t(speed);

    for (int k = 1; k <= 4; ++k) {
        const int target = k * kIrqCycles;
        while (fc_ < target) {
            const int slice = target - fc_ < kIdleSlice ? target - fc_ : kIdleSlice;
            fc_ += cpu_.run(slice);
            if (waiting_for_irq()) fc_ = target;
        }
        cpu_.irq(true);
    }
    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    fc_ -= kCyclesPerFrame;
    total_ += kCyclesPerFrame;

    // The picture: the list as it stands now, from its start to its jump
    // back (the AVG loops on it). Every other frame: the game makes ~27
    // new pictures a second, and each run costs as much as a quarter of
    // the CPU's frame.
    if (vg_started_ && (frame_ & 1)) vg_draw();

    if (int32_t(total_ - watchdog_at_) > kWatchdogCycles) {
        ++watchdog_resets_;
        reset();
    }
    return audio ? n : 0;
}

} // namespace tempest
