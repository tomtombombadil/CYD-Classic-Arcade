// Asteroids machine - see asteroids.h.
//
// Memory map (A15 not decoded: 8000-FFFF mirror 0000-7FFF):
//   0000-01FF  RAM          0200-02FF / 0300-03FF  RAM, swapped by RAMSEL
//   2000-2007  IN0, bit 7 = input (addr & 7): 1 3 kHz clock, 2 DVG busy,
//              3 hyperspace, 4 fire, 5 diagnostic step, 6 slam, 7 self-test
//   2400-2407  IN1, bit 7: 0-2 coins (left, centre, right), 3 start 1,
//              4 start 2, 5 thrust, 6 rotate right, 7 rotate left
//   2800-2803  DIP switches, two a read (bits 1-0): 2800 = SW8,7 ...
//              2803 = SW2,1
//   3000  DVG go          3200  lamps, RAMSEL (bit 2), coin counters
//   3400  watchdog        3600  explosion    3A00  thump
//   3C00-3C07  sound latch (bit 7)           3E00  noise reset
//   4000-47FF  vector RAM   5000-57FF  vector ROM   6800-7FFF  program
// Inputs are active high. Facts from MAME's driver (read for facts only).
#include "asteroids.h"

#include <cstring>

namespace asteroids {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x1800},
    {"vector",  0x0800},
};

const char* const kVariants[] = {"asteroid", "asteroid2"};

// CRCs as in MAME's "asteroid" (rev 4, variant 1) and "asteroid2" (rev 2,
// variant 2); J2 and the vector ROM are the same in both.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,    0x0000, 0x0800, 0xb503eaf7, "035145-04e.ef2", false, 1},
    {kRegionCpu,    0x0800, 0x0800, 0x25233192, "035144-04e.h2",  false, 1},
    {kRegionCpu,    0x0000, 0x0800, 0x0cc75459, "035145-02.ef2",  false, 2},
    {kRegionCpu,    0x0800, 0x0800, 0x096ed35c, "035144-02.h2",   false, 2},
    {kRegionCpu,    0x1000, 0x0800, 0x312caa02, "035143-02.j2",   false},
    {kRegionVector, 0x0000, 0x0800, 0x8b71fd9e, "035127-02.np3",  false},
};

// DVG units to output 1/16 pixels: 240 x 16 / 848 = 4.528 (x 1024 >> 10),
// X centred.
constexpr int kScale = 240 * vector_display::kSub * 1024 / kViewH;
constexpr int kLeft16 = (kScreenW * vector_display::kSub - ((1024 * kScale) >> 10)) / 2;
inline int to_x(int x) { return kLeft16 + ((x * kScale) >> 10); }
inline int to_y(int y) { return ((kTopY - y) * kScale) >> 10; }

} // namespace

const romset::Set kRomSet = {
    "asteroid", nullptr, "Asteroids", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate ? audio_rate : 24000;
    swap_ = swap_rgb565_bytes;
    // Intensity 1-15 as grey; the dimmest lines still show on an LCD.
    for (int i = 0; i < 16; ++i) {
        const int v = i ? 70 + i * 185 / 15 : 0;
        uint16_t c = uint16_t(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
        if (swap_) c = uint16_t((c >> 8) | (c << 8));
        gray_[i] = c;
    }
    display_.init(kScreenW, kScreenH);
    dvg_.init(vec_read, vec_line, this, 0x4000);
    sound_.init(audio_rate_);

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    for (int half = 0; half < 2; ++half) {
        const uint16_t o = uint16_t(half << 15);
        cpu_.map_ram(o, 0x200, ram_);
        cpu_.map_ram(uint16_t(o + 0x4000), 0x800, vram_);
        cpu_.map_read(uint16_t(o + 0x5000), 0x800, roms_.vector);
        cpu_.map_read(uint16_t(o + 0x6800), 0x1800, roms_.cpu);
    }
    reset();
}

void Machine::reset()
{
    std::memset(ram_, 0, sizeof ram_);
    outlatch_ = 0;
    thump_ = 0;
    map_ram_banks();
    dvg_.reset();
    sound_.reset();
    vg_busy_until_ = 0;
    fc_ = 0;
    watchdog_at_ = total_;
    cpu_.reset();
}

void Machine::map_ram_banks()
{
    // RAMSEL swaps the two pages (each player's game state).
    const bool sel = outlatch_ & 0x04;
    for (int half = 0; half < 2; ++half) {
        const uint16_t o = uint16_t(half << 15);
        cpu_.map_ram(uint16_t(o + 0x200), 0x100, ram_ + (sel ? 0x300 : 0x200));
        cpu_.map_ram(uint16_t(o + 0x300), 0x100, ram_ + (sel ? 0x200 : 0x300));
    }
}

uint8_t Machine::vec_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    return m.cpu_.rd(addr);
}

void Machine::vec_line(void* ctx, int x0, int y0, int x1, int y1, int intensity)
{
    Machine& m = *static_cast<Machine*>(ctx);
    m.display_.add(to_x(x0), to_y(y0), to_x(x1), to_y(y1), m.gray_[intensity & 15]);
}

void Machine::vg_go()
{
    display_.begin();
    vg_clocks_ = dvg_.run();
    display_.finish();
    vg_busy_until_ = now() + uint32_t(vg_clocks_);
    ++vg_runs_;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    addr &= 0x7fff;
    const int bit = addr & 7;
    switch (addr & 0x7c00) {
    case 0x2000: {
        m.check_idle(bit);
        bool on = false;
        switch (bit) {
        case 1: on = (m.now() & 0x100) != 0; break;                     // 3 kHz
        case 2: on = int32_t(m.vg_busy_until_ - m.now()) > 0; break;   // DVG busy
        case 3: on = m.buttons_ & kHyperspace; break;
        case 4: on = m.buttons_ & kFire; break;
        case 5: on = m.buttons_ & kDiagStep; break;
        case 6: on = m.buttons_ & kSlam; break;
        case 7: on = m.buttons_ & kTest; break;
        default: break;
        }
        return on ? 0x80 : 0x7f;
    }
    case 0x2400: {
        static const uint16_t kIn1[8] = {kCoin, kCoin2, 0, kStart1, kStart2, kThrust, kRight, kLeft};
        return (m.buttons_ & kIn1[bit]) ? 0x80 : 0x7f;
    }
    case 0x2800: {
        const int sel = addr & 3;           // 0: SW8,7 ... 3: SW2,1
        return uint8_t(0xfc | ((m.dsw_ >> (6 - 2 * sel)) & 3));
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
    case 0x3000:
        m.vg_go();
        break;
    case 0x3200:
        if ((v ^ m.outlatch_) & 0x04) {
            m.outlatch_ = v;
            m.map_ram_banks();
        }
        m.outlatch_ = v;
        break;
    case 0x3400:
        m.watchdog_at_ = m.now();
        break;
    case 0x3600:
        m.audio_to(m.fc_ + m.cpu_.elapsed());
        m.sound_.explode(v);
        break;
    case 0x3a00:
        m.audio_to(m.fc_ + m.cpu_.elapsed());
        m.thump_ = v;
        m.sound_.thump(v);
        break;
    case 0x3c00:
        m.audio_to(m.fc_ + m.cpu_.elapsed());
        m.sound_.latch(addr & 7, (v & 0x80) != 0);
        break;
    case 0x3e00:
        m.audio_to(m.fc_ + m.cpu_.elapsed());
        m.sound_.noise_reset();
        break;
    default:
        break;
    }
}

// The main loop's two waits, recognised by their code (both revisions):
//   LDA 2007; BMI *; LSR zp; BCC back - the NMI handler sets bit 0 of zp
//   LDA 2002; BMI back                - the DVG still drawing
// When a wait can only end at the next NMI (zp is 0: LSR keeps it 0) or
// when the DVG finishes, the time up to there is skipped. Called on the
// read of IN0 bit `bit`, with PC after the LDA.
void Machine::check_idle(int bit)
{
    const uint16_t pc = cpu_.pc;
    if (bit == 7) {
        if (cpu_.rd(pc) == 0x30 && cpu_.rd(uint16_t(pc + 1)) == 0xfe && cpu_.rd(uint16_t(pc + 2)) == 0x46 &&
            cpu_.rd(uint16_t(pc + 4)) == 0x90 && cpu_.rd(cpu_.rd(uint16_t(pc + 3))) == 0 && !(buttons_ & kTest)) {
            idle_ = true;
            idle_until_ = total_ + uint32_t(kCyclesPerFrame * 2);     // capped at the next NMI
            cpu_.stop();
        }
    } else if (bit == 2) {
        if (cpu_.rd(pc) == 0x30 && cpu_.rd(uint16_t(pc + 1)) == 0xfb && int32_t(vg_busy_until_ - now()) > 0) {
            idle_ = true;
            idle_until_ = vg_busy_until_;
            cpu_.stop();
        }
    }
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    sound_.render(audio_ + audio_done_, target - audio_done_);
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

    // fc_ starts at what the last frame ran past its end.
    for (int k = 1; k <= 4; ++k) {
        const int target = k * kNmiCycles;
        while (fc_ < target) {
            fc_ += cpu_.run(target - fc_);
            if (idle_) {
                // The program is only waiting: jump to the end of the wait.
                idle_ = false;
                const int until = int(idle_until_ - total_);
                if (until > fc_) fc_ = until < target ? until : target;
            }
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

} // namespace asteroids
