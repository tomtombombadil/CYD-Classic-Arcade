// Star Wars machine - see starwars.h.
//
// Main CPU memory map:
//   0000-2FFF  RAM (vector RAM + work RAM)     3000-3FFF  vector ROM
//   4300  IN0: 0 right coin, 1 left coin, 2 aux coin, 3 tilt, 4 self-test,
//         6 left thumb, 7 right trigger (all low = on)
//   4320  IN1: 2 diagnostic step, 4 right thumb, 5 left trigger (low = on),
//         6 AVG halted, 7 matrix processor running
//   4340 / 4360  the option / pricing switches   4380  ADC result
//   4400  read: the sound board's reply (clears its flag); write: a command
//   4401  read: bit 7 = command not yet taken, bit 6 = reply waiting
//   4500-45FF  X2212 NOVRAM (4 bits)   4600 AVG go   4620 AVG reset
//   4640  watchdog   4660 IRQ acknowledge
//   4680-4687  LS259 (data bit 7): 0-1 coin counters, 2 3 6 LEDs, 4 ROM
//              bank, 5 random number reset, 7 NOVRAM recall
//   46A0  NOVRAM store   46C0-46C3  ADC start (channel)   46E0 sound reset
//   4700-4707  write: 0 matrix processor go, 1-2 its BIC, 4-7 divider;
//              read: 4700 / 4701 quotient, 4703 random number
//   4800-4FFF  RAM   5000-5FFF  math RAM   6000-7FFF  banked ROM
//   8000-FFFF  ROM
// Sound CPU memory map:
//   0000-07FF  write: the reply to the main CPU
//   0800-0FFF  read: the command (clears its flag)
//   1000-107F  RIOT RAM   1080-109F  RIOT (PA0 speech /WS, PA1 /RS, PA2
//              /READY, PA4 sound self-test (high = off), PA6 reply
//              waiting, PA7 command waiting (edge IRQ); PB the speech bus)
//   1800-183F  POKEYs (A3-A4 = chip, A5 = registers 8-15)
//   2000-27FF  RAM   4000-7FFF  ROM (also at C000-FFFF)
#include "starwars.h"

#include <cmath>
#include <cstring>

namespace starwars {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"rom0", 0x4000}, {"rom1", 0x2000}, {"rom2", 0x2000}, {"rom3", 0x2000}, {"rom4", 0x2000},
    {"vector", 0x1000}, {"sound0", 0x2000}, {"sound1", 0x2000}, {"mathprom", 0x1000},
};

const char* const kVariants[] = {"starwars", "starwars_oldprom"};

// CRCs as in MAME's "starwars" (set 1). The math box PROMs: today's dumps
// (variant 1) or old MAME's (variant 2: the same low nibbles, stray bits
// above them in 14 bytes).
constexpr romset::Rom kRoms[] = {
    {kRegionRom0,   0x0000, 0x4000, 0x04f1876e, "136021.214.1f",  false, 0},
    {kRegionRom1,   0x0000, 0x2000, 0xf725e344, "136021.102.1hj", false, 0},
    {kRegionRom2,   0x0000, 0x2000, 0xf6da0a00, "136021.203.1jk", false, 0},
    {kRegionRom3,   0x0000, 0x2000, 0x7e406703, "136021.104.1kl", false, 0},
    {kRegionRom4,   0x0000, 0x2000, 0xc7e51237, "136021.206.1m",  false, 0},
    {kRegionVector, 0x0000, 0x1000, 0x538e7d2f, "136021-105.1l",  false, 0},
    {kRegionSound0, 0x0000, 0x2000, 0xdbf3aea2, "136021-107.1jk", false, 0},
    {kRegionSound1, 0x0000, 0x2000, 0xe38070a8, "136021-208.1h",  false, 0},
    {kRegionMath,   0x0000, 0x0400, 0x810e040e, "136021-110.7h",  false, 1},
    {kRegionMath,   0x0400, 0x0400, 0xae69881c, "136021-111.7j",  false, 1},
    {kRegionMath,   0x0800, 0x0400, 0xecf22628, "136021-112.7k",  false, 1},
    {kRegionMath,   0x0c00, 0x0400, 0x83febfde, "136021-113.7l",  false, 1},
    {kRegionMath,   0x0000, 0x0400, 0x01061762, "136021.110",     false, 2},
    {kRegionMath,   0x0400, 0x0400, 0x2e619b70, "136021.111",     false, 2},
    {kRegionMath,   0x0800, 0x0400, 0x6cfa3544, "136021.112",     false, 2},
    {kRegionMath,   0x0c00, 0x0400, 0x03f6acb2, "136021.113",     false, 2},
};

// The AVG's centre in its 16.16 units.
constexpr int32_t kCenterX = (kAvgW / 2) << 16, kCenterY = (kAvgH / 2) << 16;

// Main CPU, 6005: LDX #0; LDD #F800; LDA #$48; TFR A,DP; LSR <$3D; BCC 6005
// - the game's frame wait (the IRQ sets 483D bit 0). 23 cycles a turn.
constexpr uint8_t kFrameWait[] = {0x8e, 0x00, 0x00, 0xcc, 0xf8, 0x00, 0x86, 0x48, 0x1f, 0x8b, 0x04, 0x3d, 0x24, 0xf2};
constexpr uint16_t kFrameWaitPc = 0x6005, kFrameWaitEnd = 0x6013;
// Main CPU, 6032: LDA <$3F; BMI 6032 - waiting for the IRQ to clear bit 7
// of a flag. 7 cycles a turn.
constexpr uint8_t kFlagWait[] = {0x96, 0x3f, 0x2b, 0xfc};
constexpr uint16_t kFlagWaitPc = 0x6032;
// Main CPU, CDBD: TST $4320; BMI CDBD - waiting for the matrix processor.
// 10 cycles a turn.
constexpr uint8_t kMathWait[] = {0x7d, 0x43, 0x20, 0x2b, 0xfb};
constexpr uint16_t kMathWaitPc = 0xcdbd;
// Sound CPU, 7D4C: LDA <$00; CMPA #$3F; BNE; TFR DP,A; CMPA #$10; BNE;
// LEAX -1,X; BEQ; LSR <$0B; BCC 7D4C - waiting for the timer IRQ to set
// bit 0 of 100B (X counts down to a timeout). 37 cycles a turn.
constexpr uint8_t kSoundWait[] = {0x96, 0x00, 0x81, 0x3f, 0x26, 0xf2, 0x1f, 0xb8, 0x81, 0x10,
                                  0x26, 0xec, 0x30, 0x1f, 0x27, 0xe8, 0x04, 0x0b, 0x24, 0xec};
constexpr uint16_t kSoundWaitPc = 0x7d4c, kSoundWaitEnd = 0x7d60;

inline int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

} // namespace

const romset::Set kRomSet = {
    "starwars", nullptr, "Star Wars", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate ? audio_rate : 24000;
    swap_ = swap_rgb565_bytes;
    display_.init(kScreenW, kScreenH);
    avg_.init(vec_read, vec_line, this, 0x0000, kCenterX, kCenterY, atari_avg::Variant::StarWars);
    for (int p = 0; p < 3; ++p) avg_.map(p, ram_ + p * 0x1000);
    avg_.map(3, roms_.vector);
    mp_.init(roms_.math, math_ram_);
    for (pokey::Pokey& p : pokey_) p.init(kCpuClock, audio_rate_);
    tms_.init(kSpeechClock, kCpuClock, audio_rate_);
    dc_r_ = 1.0f - 6.2831853f * 15.0f / float(audio_rate_);
    {
        // 2-pole Butterworth low-pass at 4 kHz (bilinear transform).
        const float k = std::tan(3.14159265f * 4000.0f / float(audio_rate_));
        const float q = 0.70710678f, norm = 1.0f / (1.0f + k / q + k * k);
        lp_b0_ = k * k * norm;
        lp_b1_ = 2.0f * lp_b0_;
        lp_b2_ = lp_b0_;
        lp_a1_ = 2.0f * (k * k - 1.0f) * norm;
        lp_a2_ = (1.0f - k / q + k * k) * norm;
    }

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.map_ram(0x0000, 0x3000, ram_);
    cpu_.map_read(0x3000, 0x1000, roms_.vector);
    cpu_.map_ram(0x4800, 0x800, ram2_);
    cpu_.map_ram(0x5000, 0x1000, math_ram_);
    for (int i = 1; i < 5; ++i) cpu_.map_read(uint16_t(0x6000 + i * 0x2000), 0x2000, roms_.rom[i]);

    snd_.ctx = this;
    snd_.mem_read = snd_read;
    snd_.mem_write = snd_write;
    snd_.map_ram(0x2000, 0x800, snd_ram_);
    snd_.map_read(0x4000, 0x2000, roms_.sound[0]);
    snd_.map_read(0x6000, 0x2000, roms_.sound[1]);
    snd_.map_read(0xc000, 0x2000, roms_.sound[0]);
    snd_.map_read(0xe000, 0x2000, roms_.sound[1]);

    // The wait loops idle skipping knows (another program revision: none).
    frame_wait_ok_ = std::memcmp(roms_.rom[0] + (kFrameWaitPc - 0x6000), kFrameWait, sizeof kFrameWait) == 0;
    flag_wait_ok_ = std::memcmp(roms_.rom[0] + (kFlagWaitPc - 0x6000), kFlagWait, sizeof kFlagWait) == 0;
    math_wait_ok_ = std::memcmp(roms_.rom[3] + (kMathWaitPc - 0xc000), kMathWait, sizeof kMathWait) == 0;
    sound_wait_ok_ = std::memcmp(roms_.sound[1] + (kSoundWaitPc - 0x6000), kSoundWait, sizeof kSoundWait) == 0;

    std::memset(nv_ram_, 0x0f, sizeof nv_ram_);
    std::memset(nv_store_, 0x0f, sizeof nv_store_);
    reset();
}

void Machine::reset()
{
    std::memset(ram_, 0, sizeof ram_);
    std::memset(ram2_, 0, sizeof ram2_);
    std::memset(math_ram_, 0, sizeof math_ram_);
    std::memset(snd_ram_, 0, sizeof snd_ram_);
    avg_.reset();
    mp_.reset();
    div_.reset();
    riot_.reset();
    tms_.reset();
    for (pokey::Pokey& p : pokey_) p.reset();
    latch_bits_ = 0;
    set_bank(0);
    sound_pending_ = main_pending_ = false;
    vg_busy_until_ = math_busy_until_ = 0;
    watchdog_at_ = now();
    cpu_.irq(false);
    cpu_.reset();
    snd_.irq(false);
    snd_.reset();
    riot_pa_inputs();
}

void Machine::set_bank(int b)
{
    bank_ = b & 1;
    cpu_.map_read(0x6000, 0x2000, roms_.rom[0] + bank_ * 0x2000);
}

uint8_t Machine::adc(int ch) const
{
    // Pitch (channel 0, up = lower) and yaw (1, right = higher): the stick
    // added to the sights' position, -128..127, onto the yoke's pots. The
    // game calibrates the yoke itself (a minimum and a gain per axis,
    // learnt while playing, kept in the NOVRAM); a new board starts from
    // minimum 0x40, gain 1.25, so the pots read 0x40 at one end, 0xA6 in
    // the middle (the sights centred) and 0xFF at the other.
    if (ch > 1) return 0;           // thrust: not fitted
    int o = ch == 0 ? -(sight_y_ + stick_y_ / 4) : sight_x_ + stick_x_ / 4;
    o = clamp(o, -128, 127);
    return uint8_t(o < 0 ? 0xa6 + o * 0x66 / 128 : 0xa6 + o * 0x59 / 127);
}

// --- the picture ---------------------------------------------------------------

uint8_t Machine::vec_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (addr < 0x3000) return m.ram_[addr];
    if (addr < 0x4000) return m.roms_.vector[addr & 0xfff];
    return 0;
}

void Machine::vec_line(void* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const int key = (color & 7) << 8 | level;
    if (key != m.pen_key_) {
        // Colour bits 2 / 1 / 0 = red / green / blue, the intensity scaling
        // them (with a floor, so dim lines still show on an LCD).
        const int v = 0xff * (48 + level * 207 / 255) >> 8;
        const int r = (color & 4) ? v : 0, g = (color & 2) ? v : 0, b = (color & 1) ? v : 0;
        uint16_t c = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        if (m.swap_) c = uint16_t((c >> 8) | (c << 8));
        m.pen_key_ = key;
        m.pen_ = c;
    }
    if (!m.pen_) return;
    // 16.16 units -> 1/16 pixels: the 250 x 280 window stretched over
    // 320 x 240 (x * 320 x 16 / 250 / 65536 as a 32.32 multiplier).
    constexpr int64_t kSx = (int64_t(kScreenW) * vector_display::kSub << 32) / (int64_t(kAvgW) << 16);
    constexpr int64_t kSy = (int64_t(kScreenH) * vector_display::kSub << 32) / (int64_t(kAvgH) << 16);
    auto px = [](int32_t x) { return int((int64_t(x) * kSx) >> 32); };
    auto py = [](int32_t y) { return int((int64_t(y) * kSy) >> 32); };
    m.display_.add(px(x0), py(y0), px(x1), py(y1), m.pen_);
}

void Machine::vg_draw()
{
    display_.begin();
    const int clocks = avg_.run(16384);
    display_.finish();
    vg_busy_until_ = now() + clocks;
    ++vg_runs_;
}

// --- main CPU I/O ----------------------------------------------------------------

uint8_t Machine::in0() const
{
    uint8_t v = 0xdf;               // bit 5 not fitted (reads 0)
    if (buttons_ & kCoin2) v &= uint8_t(~0x01);
    if (buttons_ & kCoin) v &= uint8_t(~0x02);
    if (buttons_ & kAux) v &= uint8_t(~0x04);
    if (buttons_ & kTilt) v &= uint8_t(~0x08);
    if (buttons_ & kTest) v &= uint8_t(~0x10);
    if (buttons_ & kFire4) v &= uint8_t(~0x40);
    if (buttons_ & kFire1) v &= uint8_t(~0x80);
    return v;
}

uint8_t Machine::in1()
{
    uint8_t v = 0x34;               // bits 0, 1, 3 not fitted
    if (buttons_ & kDiagStep) v &= uint8_t(~0x04);
    if (buttons_ & kFire3) v &= uint8_t(~0x10);
    if (buttons_ & kFire2) v &= uint8_t(~0x20);
    const int64_t t = now();
    if (vg_busy_until_ <= t) v |= 0x40;
    if (math_busy_until_ > t) v |= 0x80;
    return v;
}

void Machine::outlatch(int bit, bool v)
{
    const uint8_t old = latch_bits_;
    latch_bits_ = uint8_t(v ? latch_bits_ | (1 << bit) : latch_bits_ & ~(1 << bit));
    if (bit == 4) set_bank(v);
    // The NOVRAM's recall: the EEPROM half back into the RAM.
    if (bit == 7 && (old & 0x80) && !v) std::memcpy(nv_ram_, nv_store_, sizeof nv_ram_);
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0x4300 && addr < 0x4400) {
        switch (addr & 0xe0) {
        case 0x00: return m.in0();
        case 0x20: return m.in1();
        case 0x40: return m.dsw0_;
        case 0x60: return kDipPricing;
        case 0x80: return m.adc(m.adc_channel_);
        default:   return 0;
        }
    }
    if (addr == 0x4400) {
        m.sound_to(m.cycle());
        m.main_pending_ = false;
        m.riot_pa_inputs();
        return m.main_latch_;
    }
    if (addr == 0x4401) {
        m.sound_to(m.cycle());
        return uint8_t((m.sound_pending_ ? 0x80 : 0) | (m.main_pending_ ? 0x40 : 0));
    }
    if ((addr & 0xff00) == 0x4500) return m.nv_ram_[addr & 0xff] & 0x0f;
    if (addr == 0x4700) return uint8_t(m.div_.quotient() >> 8);
    if (addr == 0x4701) return uint8_t(m.div_.quotient());
    if (addr == 0x4703) {
        // The LFSR runs at 3 MHz: two steps a CPU cycle.
        const int64_t t = m.now();
        m.lfsr_ = starwars_math::lfsr_advance(m.lfsr_, uint64_t(t - m.lfsr_at_) * 2);
        m.lfsr_at_ = t;
        return uint8_t(m.lfsr_ >> 8);
    }
    return 0;
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr & 0xffe0) {
    case 0x4400:
        if (addr != 0x4400) break;
        m.sound_to(m.cycle());
        m.sound_latch_ = v;
        m.sound_pending_ = true;
        ++m.sound_commands_;
        m.riot_pa_inputs();
        break;
    case 0x4600:
        m.vg_draw();
        break;
    case 0x4620:
        m.avg_.reset();
        break;
    case 0x4640:
        m.watchdog_at_ = m.now();
        break;
    case 0x4660:
        m.cpu_.irq(false);
        break;
    case 0x4680:
        m.outlatch(addr & 7, (v & 0x80) != 0);
        break;
    case 0x46a0:
        std::memcpy(m.nv_store_, m.nv_ram_, sizeof m.nv_store_);
        break;
    case 0x46c0:
        if (addr < 0x46c4) m.adc_channel_ = addr & 3;
        break;
    case 0x46e0:
        if (addr != 0x46e0) break;
        // The sound board's reset: both latches' flags clear.
        m.sound_to(m.cycle());
        m.sound_pending_ = m.main_pending_ = false;
        m.snd_.reset();
        m.riot_pa_inputs();
        break;
    case 0x4700:
        switch (addr & 0x1f) {
        case 0: {
            const int clocks = m.mp_.run(v);
            m.math_busy_until_ = m.now() + (clocks + 7) / 8;
            ++m.math_runs_;
            // The CPU waits for it next: end the run here, so the wait is
            // found at its start and skipped.
            if (m.idle_skip_) m.cpu_.stop();
            break;
        }
        case 1: m.mp_.set_bic_hi(v); break;
        case 2: m.mp_.set_bic_lo(v); break;
        case 4: case 5: case 6: case 7: m.div_.write(addr & 7, v); break;
        default: break;
        }
        break;
    default:
        if ((addr & 0xff00) == 0x4500) m.nv_ram_[addr & 0xff] = v & 0x0f;
        break;
    }
}

// --- the sound board -------------------------------------------------------------

void Machine::riot_pa_inputs()
{
    // PA2 = the speech chip's /READY, PA4 = self-test off, PA6 / PA7 = the
    // latches' flags; the outputs' pins read high.
    uint8_t v = 0x2b | 0x10;
    if (!tms_.ready()) v |= 0x04;
    if (main_pending_) v |= 0x40;
    if (sound_pending_) v |= 0x80;
    riot_.set_pa_in(v);
    riot_irq();
}

void Machine::riot_irq() { snd_.irq(riot_.irq(snd_now())); }

void Machine::speech_strobes()
{
    const uint8_t pins = riot_.pa_pins();
    tms_.strobes((pins & 2) != 0, (pins & 1) != 0, snd_now());
}

uint8_t Machine::snd_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0x0800 && addr < 0x1000) {
        m.sound_pending_ = false;
        m.riot_pa_inputs();
        return m.sound_latch_;
    }
    if (addr >= 0x1000 && addr < 0x1080) return m.riot_.ram[addr & 0x7f];
    if (addr >= 0x1080 && addr < 0x10a0) {
        const int reg = addr & 0x1f;
        if (!(reg & 4) && (reg & 1) == 0) {
            // A port: bring the speech chip up to now first.
            m.audio_to(m.snd_cycle());
            if ((reg & 3) == 0) m.riot_pa_inputs();
            else                m.riot_.set_pb_in(m.tms_.data());
        }
        const uint8_t v = m.riot_.read(reg, m.snd_now());
        m.riot_irq();
        if (reg & 4) m.snd_.stop();     // the timer may have changed: the run loop looks again
        return v;
    }
    return 0xff;
}

void Machine::snd_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr < 0x0800) {
        m.main_latch_ = v;
        m.main_pending_ = true;
        m.riot_pa_inputs();
        return;
    }
    if (addr >= 0x1000 && addr < 0x1080) {
        m.riot_.ram[addr & 0x7f] = v;
        return;
    }
    if (addr >= 0x1080 && addr < 0x10a0) {
        const int reg = addr & 0x1f;
        const bool port = !(reg & 4);
        if (port) m.audio_to(m.snd_cycle());
        m.riot_.write(reg, v, m.snd_now());
        if (port && (reg & 3) < 2) m.speech_strobes();
        if (port && (reg & 2)) m.tms_.set_data(m.riot_.pb_pins());
        m.riot_irq();
        if (!port) m.snd_.stop();       // a new timer: the run loop looks at it
        return;
    }
    if (addr >= 0x1800 && addr < 0x1840) {
        const int off = addr & 0x3f;
        m.audio_to(m.snd_cycle());
        m.pokey_[(off >> 3) & 3].write(uint8_t((off & 7) | ((off & 0x20) >> 2)), v, uint32_t(m.snd_now()));
        return;
    }
}

void Machine::sound_to(int target)
{
    while (snd_done_ < target) {
        const int64_t t = total_ + snd_done_;
        snd_.irq(riot_.irq(t));
        int64_t stop = total_ + target;
        const int64_t ev = riot_.next_event();
        if (ev > t && ev < stop) stop = ev;
        int n = int(stop - t);
        if (idle_skip_) {
            if (sound_idle(n)) continue;
            if (n > kIdleSlice) n = kIdleSlice;
        }
        const int ran = run_sound(n);
        snd_done_ += ran;
        snd_run_ += ran;
    }
}

// --- idle skipping ---------------------------------------------------------------
//
// Both CPUs spend most of their time in wait loops that only an interrupt
// (or the matrix processor finishing) ends. Found there, whole turns of the
// loop are skipped up to the next event, the registers left as the last
// turn leaves them - the same state, at the same time, as running them
// (checked against runs without skipping). Each loop is recognised by its
// code (checked once at init), so another program revision just runs them.

namespace {

inline bool irq_due(const m6809::Cpu& c) { return c.irq_line() && !(c.cc & m6809::CC_I); }

} // namespace

int Machine::main_idle(int budget)
{
    uint16_t pc = cpu_.pc;
    if (irq_due(cpu_) || budget <= 0) return 0;
    // Inside the frame wait: step to its start.
    if (pc > kFrameWaitPc && pc < kFrameWaitEnd && bank_ == 0 && ram2_[0x3d] == 0 && frame_wait_ok_) {
        int stepped = 0;
        while (cpu_.pc > kFrameWaitPc && cpu_.pc < kFrameWaitEnd && stepped < budget) {
            const int r = run_main(1);
            stepped += r;
            fc_ += r;               // counted as they run (I/O sees the time)
        }
        main_run_ += stepped;
        if (cpu_.pc != kFrameWaitPc || ram2_[0x3d] != 0) return 0;
        return main_idle(budget - stepped);
    }
    int turn = 0, turns = 0;
    if (pc == kFrameWaitPc && bank_ == 0 && ram2_[0x3d] == 0 && frame_wait_ok_) {
        turn = 23;
        turns = budget / turn;
        if (turns > 0) {
            cpu_.x = 0;
            cpu_.r.a = 0x48;
            cpu_.r.b = 0x00;
            cpu_.dp = 0x48;
            cpu_.cc = uint8_t((cpu_.cc & ~(m6809::CC_N | m6809::CC_V | m6809::CC_C)) | m6809::CC_Z);
        }
    } else if (pc == kFlagWaitPc && bank_ == 0 && flag_wait_ok_) {
        const uint8_t v = cpu_.rd(uint16_t((cpu_.dp << 8) | 0x3f));
        if (!(v & 0x80)) return 0;
        turn = 7;
        turns = budget / turn;
        if (turns > 0) {
            cpu_.r.a = v;
            cpu_.cc = uint8_t((cpu_.cc & ~(m6809::CC_Z | m6809::CC_V)) | m6809::CC_N);
        }
    } else if (pc == kMathWaitPc && math_wait_ok_) {
        // Turns whose TST still sees MATH_RUN (the core counts an
        // instruction's 7 cycles before its read).
        const int64_t left = math_busy_until_ - now() - 7;
        if (left <= 0) return 0;
        turn = 10;
        turns = budget / turn;
        const int busy = int((left + turn - 1) / turn);
        if (busy < turns) turns = busy;
        if (turns > 0)
            cpu_.cc = uint8_t((cpu_.cc & ~(m6809::CC_Z | m6809::CC_V)) | m6809::CC_N);
    }
    return turns > 0 ? turns * turn : 0;
}

bool Machine::sound_idle(int budget)
{
    const uint16_t pc = snd_.pc;
    if (pc < kSoundWaitPc || pc >= kSoundWaitEnd || !sound_wait_ok_ || irq_due(snd_)) return false;
    if (riot_.ram[0] != 0x3f || riot_.ram[0x0b] != 0 || snd_.dp != 0x10) return false;
    if (pc != kSoundWaitPc) {
        // Step to the loop's start (its last instructions read nothing new).
        int stepped = 0;
        while (snd_.pc > kSoundWaitPc && snd_.pc < kSoundWaitEnd && stepped < budget) {
            const int r = run_sound(1);
            stepped += r;
            snd_done_ += r;         // counted as they run (I/O sees the time)
        }
        snd_run_ += stepped;
        return true;
    }
    // X counts down once a turn and leaves the loop at 0: keep it above.
    int turns = budget / 37;
    if (turns > int(snd_.x) - 2) turns = int(snd_.x) - 2;
    if (turns <= 0) return false;
    snd_.x = uint16_t(snd_.x - turns);
    snd_.r.a = 0x10;
    snd_.cc = uint8_t((snd_.cc & ~(m6809::CC_N | m6809::CC_V | m6809::CC_C)) | m6809::CC_Z);
    snd_done_ += turns * 37;
    return true;
}

// --- sound output ------------------------------------------------------------------

void Machine::render_to(int cycle)
{
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    int n = target - audio_done_;
    int16_t* out = audio_ ? audio_ + audio_done_ : nullptr;
    audio_done_ = target;
    if (out) {
        // Four POKEYs (each 0..15360), AC-coupled, x 0.85: the loudest
        // play sounds then peak near 20,000.
        pokey_[0].render(out, n);
        for (int c = 1; c < 4; ++c) pokey_[c].render(out, n, true);
        for (int i = 0; i < n; ++i) {
            const float x = float(out[i]) * 0.85f;
            dc_y_ = x - dc_x_ + dc_r_ * dc_y_;
            dc_x_ = x;
            if (dc_y_ > -1e-3f && dc_y_ < 1e-3f) dc_y_ = 0;
            out[i] = int16_t(dc_y_ > 32767 ? 32767 : dc_y_ < -32768 ? -32768 : dc_y_);
        }
    }
    // The speech (+-32512 at full scale) at 0.44 of that, through a 4 kHz
    // low-pass (the 8.4 kHz samples' images; the board's own filter isn't
    // known). It runs without an output too: the sound CPU waits on it.
    int16_t sp[64];
    while (n > 0) {
        const int k = n < 64 ? n : 64;
        tms_.render(sp, k, false, 112);
        if (out) {
            for (int i = 0; i < k; ++i) {
                const float x = float(sp[i]);
                const float y = lp_b0_ * x + lp_z1_;
                lp_z1_ = lp_b1_ * x - lp_a1_ * y + lp_z2_;
                lp_z2_ = lp_b2_ * x - lp_a2_ * y;
                const int v = out[i] + int(y);
                out[i] = int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            out += k;
        }
        n -= k;
    }
}

void Machine::audio_to(int cycle)
{
    // The speech chip's bus actions fall due in time order with its output.
    while (tms_.event_at() >= 0 && tms_.event_at() <= total_ + cycle) {
        render_to(int(tms_.event_at() - total_));
        tms_.fire();
        riot_pa_inputs();
    }
    render_to(cycle);
}

// --- the frame ---------------------------------------------------------------------

int Machine::run_frame(int16_t* audio, int cap)
{
    ++frame_;
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (audio && n > cap) n = cap;
    audio_ = audio;
    audio_n_ = n;
    audio_done_ = 0;
    main_run_ = snd_run_ = 0;

    // The sights move at a steady speed while their buttons are held.
    if (buttons_ & kUp) sight_y_ -= kSightSpeed;
    if (buttons_ & kDown) sight_y_ += kSightSpeed;
    if (buttons_ & kLeft) sight_x_ -= kSightSpeed;
    if (buttons_ & kRight) sight_x_ += kSightSpeed;
    sight_x_ = clamp(sight_x_, -128, 127);
    sight_y_ = clamp(sight_y_, -128, 127);

    for (int k = 1; k <= 4; ++k) {
        const int target = k * kIrqCycles;
        while (fc_ < target) {
            const int slice = target - fc_ < 512 ? target - fc_ : 512;
            const int ran = run_main(slice);
            fc_ += ran;
            main_run_ += ran;
            if (idle_skip_) fc_ += main_idle(target - fc_);
            sound_to(fc_ < kCyclesPerFrame ? fc_ : kCyclesPerFrame);
        }
        cpu_.irq(true);
    }
    sound_to(kCyclesPerFrame);
    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    fc_ -= kCyclesPerFrame;
    snd_done_ -= kCyclesPerFrame;
    total_ += kCyclesPerFrame;

    if (now() - watchdog_at_ > kWatchdogCycles) {
        ++watchdog_resets_;
        reset();
    }
    return audio ? n : 0;
}

} // namespace starwars
