// Amidar machine - see amidar.h.
//
// Main board (A14 isn't decoded above 8000: C000-FFFF mirrors 8000-BFFF):
//   0000-3FFF  program ROM (2C 2E 2F 2H)
//   8000-87FF  RAM
//   9000-93FF  tile codes (mirrored at 9400)
//   9800-98FF  object RAM (mirrored to 9FFF): as Galaxian's (galaxian.cpp)
//   A000-A7FF  write, A3-A5 pick (bit 0 of the data): 0 background red,
//              1 NMI enable, 2 / 3 flip Y / X, 4 background green, 5
//              background blue, 6 / 7 coin counters
//   A800       read: watchdog reset
//   B000-B7FF  PPI 1, register = A4-A5: A = IN0, B = IN1, C = IN2
//   B800-BFFF  PPI 2: A = the sound command (out), B = sound control (out:
//              bit 3 falling = sound IRQ, bit 4 = mute), C = IN3 (coinage)
// Inputs are active low.
//
// Sound board:
//   0000-1FFF  ROM (5C 5D)
//   8000-83FF  RAM (mirrored through 8000-FFFF: A10, A11, A13, A14 ignored)
//   9000-9FFF  write (mirrored at B000 D000 F000): the filters, A0-A11 =
//              two bits a channel (bit 0 = 0.22 uF, bit 1 = 0.047 uF):
//              A0-A5 the first AY's channels A B C, A6-A11 the second's
//   I/O (A0-A7): write 10 = AY 2 address, 20 = AY 2 data, 40 = AY 1
//              address, 80 = AY 1 data; read 20 = AY 2, 80 = AY 1 (both
//              ANDed when both are picked)
//   AY 1 port A = the command latch, port B = the counter chain (amidar.h).
//   The IRQ (IM 1) is a flip-flop set by the command's strobe and cleared
//   when the CPU takes it.
//
// Idle skipping, exact: between commands the sound program waits for the
// counter chain's bit 7 to go low and then high again, in two loops
// (01E2: LD A,0F / RST 08 / AND 80 / JR NZ; 01E9 the same with JR Z; RST 08
// at 0008 = OUT (40),A / IN A,(80) / RET): 69 cycles and 7 opcode fetches a
// pass, the chain read 29 cycles in. After a read in a pass that goes on,
// the passes that would read the same bit 7 are skipped: time and R as if
// run, A = the last of them's read, F = AND 80's flags. The CPU runs the
// last pass itself (and nothing is skipped while an IRQ waits).
#include "amidar.h"

#include <cmath>
#include <cstring>

namespace amidar {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x4000},
    {"audiocpu", 0x2000},
    {"gfx1",     0x1000},
    {"proms",    0x0020},
};

// CRCs as in MAME's "amidar" (Konami).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x1000, 0xc294bf27, "amidar.2c", false},
    {kRegionCpu,   0x1000, 0x1000, 0xe6e96826, "amidar.2e", false},
    {kRegionCpu,   0x2000, 0x1000, 0x3656be6f, "amidar.2f", false},
    {kRegionCpu,   0x3000, 0x1000, 0x1be170bd, "amidar.2h", false},
    {kRegionSound, 0x0000, 0x1000, 0xc4b66ae4, "amidar.5c", false},
    {kRegionSound, 0x1000, 0x1000, 0x806785af, "amidar.5d", false},
    {kRegionGfx,   0x0000, 0x0800, 0x5e51e84d, "amidar.5f", false},
    {kRegionGfx,   0x0800, 0x0800, 0x2f7f1c30, "amidar.5h", false},
    {kRegionProms, 0x0000, 0x0020, 0xf940dcc3, "amidar.clr", false},
};

// Each AY channel drives its own RC low-pass: 1k with 5.1k across it
// (taken from MAME's older model of this board - unsure) into the
// capacitors the program switches in.
constexpr float kFilterR = 1000.0f * 5100.0f / (1000.0f + 5100.0f);
constexpr float kCapLow = 0.22e-6f, kCapHigh = 0.047e-6f;

// The six channels summed, AC-coupled (20 Hz, assumed) and scaled: the
// loudest tunes in play peak ~21,000 (PC runs).
constexpr float kDcHz = 20.0f;
constexpr float kOutGain = 5.0f;

// The timer wait (see above).
constexpr uint8_t kWaitLoop1[] = {0x3e, 0x0f, 0xcf, 0xe6, 0x80, 0x20, 0xf9};   // at 01E2
constexpr uint8_t kWaitLoop2[] = {0x3e, 0x0f, 0xcf, 0xe6, 0x80, 0x28, 0xf9};   // at 01E9
constexpr uint8_t kWaitRst[] = {0xd3, 0x40, 0xdb, 0x80, 0xc9};                 // at 0008
constexpr int kWaitPass = 69;           // cycles a pass
constexpr int kWaitFetches = 7;         // opcode fetches (R) a pass

} // namespace

const romset::Set kRomSet = {
    "amidar", nullptr, "Amidar", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

uint8_t timer_bits(uint64_t xtal_clocks)
{
    uint32_t c = uint32_t(xtal_clocks % kTimerPeriod);
    uint8_t hi = 0;
    if (c >= kTimerPeriod / 2) {
        hi = 0x80;
        c -= kTimerPeriod / 2;
    }
    return uint8_t(hi | (((c >> 14) & 1) << 6) | (((c >> 13) & 1) << 5) | (((c >> 11) & 1) << 4) | 0x0e);
}

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = no_in;
    cpu_.port_out = no_out;
    cpu_.map_read(0x0000, 0x4000, roms.cpu);
    for (uint32_t base = 0x8000; base < 0x10000; base += 0x4000) {
        cpu_.map_ram(uint16_t(base), 0x800, ram_);
        cpu_.map_ram(uint16_t(base + 0x1000), 0x400, vram_);
        cpu_.map_ram(uint16_t(base + 0x1400), 0x400, vram_);
    }

    snd_.ctx = this;
    snd_.mem_read = snd_mem_read;
    snd_.mem_write = snd_mem_write;
    snd_.port_in = snd_port_read;
    snd_.port_out = snd_port_write;
    snd_.map_read(0x0000, 0x2000, roms.sound);
    for (uint32_t hi = 0x8000; hi < 0x10000; hi += 0x2000)
        for (uint32_t lo = 0; lo < 0x1000; lo += 0x400) snd_.map_ram(uint16_t(hi + lo), 0x400, snd_ram_);

    wait_loops_ = std::memcmp(roms.sound + 0x1e2, kWaitLoop1, sizeof kWaitLoop1) == 0 &&
                  std::memcmp(roms.sound + 0x1e9, kWaitLoop2, sizeof kWaitLoop2) == 0 &&
                  std::memcmp(roms.sound + 0x008, kWaitRst, sizeof kWaitRst) == 0;

    video_.init(roms.proms, roms.gfx, {false, false, true}, swap_rgb565_bytes);
    dc_r_ = std::exp(-2.0f * 3.14159265f * kDcHz / float(audio_rate));
    reset();
}

void Machine::reset()
{
    cpu_.reset();
    snd_.reset();
    ay_[0].reset();
    ay_[1].reset();
    nmi_enable_ = false;
    background_ = 0;
    latch_ = 0;
    control_ = 0;
    snd_irq_ = false;
    snd_.irq(false);
    set_filters(0);
    watchdog_ = 0;
    carry_ = 0;
}

uint8_t Machine::in(int port) const
{
    uint8_t v = 0xff;
    switch (port) {
    case 0:
        if (buttons_ & kService) v &= uint8_t(~0x04);
        if (buttons_ & kJump)    v &= uint8_t(~0x08);
        if (buttons_ & kRight)   v &= uint8_t(~0x10);
        if (buttons_ & kLeft)    v &= uint8_t(~0x20);
        if (buttons_ & kCoin2)   v &= uint8_t(~0x40);
        if (buttons_ & kCoin)    v &= uint8_t(~0x80);
        return v;
    case 1:
        v = uint8_t(0xfc | (dips_ & kDipLivesMask));
        if (buttons_ & kStart2) v &= uint8_t(~0x40);
        if (buttons_ & kStart1) v &= uint8_t(~0x80);
        return v;
    case 2:
        v &= uint8_t(~0x08);                            // upright
        if (demo_sounds_) v &= uint8_t(~0x02);
        if (!(dips_ & kDipBonus50000)) v &= uint8_t(~0x04);
        if (buttons_ & kUp)   v &= uint8_t(~0x10);
        if (buttons_ & kDown) v &= uint8_t(~0x40);
        return v;
    default:
        return 0xff;                                    // coinage: 1 coin 1 credit, both slots
    }
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (!(addr & 0x8000)) return 0xff;                  // 4000-7FFF: no chips
    switch (addr & 0x3800) {
    case 0x1800: return m.obj_[addr & 0xff];
    case 0x2800: m.watchdog_ = 0; return 0xff;
    case 0x3000: return m.in((addr >> 4) & 3);
    case 0x3800: return ((addr >> 4) & 3) == 2 ? m.in(3) : 0xff;
    default:     return 0xff;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (!(addr & 0x8000)) return;
    switch (addr & 0x3800) {
    case 0x1800:
        m.obj_[addr & 0xff] = v;
        break;
    case 0x2000: {
        const bool on = v & 1;
        switch ((addr >> 3) & 7) {
        case 0: m.background_ = uint8_t((m.background_ & ~1) | (on ? 1 : 0)); break;
        case 1: m.nmi_enable_ = on; break;
        case 4: m.background_ = uint8_t((m.background_ & ~2) | (on ? 2 : 0)); break;
        case 5: m.background_ = uint8_t((m.background_ & ~4) | (on ? 4 : 0)); break;
        default: break;                                 // flips (cocktail, not drawn), coin counters
        }
        break;
    }
    case 0x3800:
        switch ((addr >> 4) & 3) {
        case 0:
            m.sound_to(m.now());
            m.latch_ = v;
            break;
        case 1:
            m.sound_to(m.now());
            m.sound_control(v);
            break;
        default:
            break;                                      // port C, the control word
        }
        break;
    default:
        break;
    }
}

void Machine::sound_control(uint8_t v)
{
    if ((control_ & 0x08) && !(v & 0x08)) {
        snd_irq_ = true;
        snd_.irq(true, 0xff);
        ++sound_irqs_;
    }
    if ((control_ ^ v) & 0x10) audio_to(sound_now());
    control_ = v;
}

// --- sound board ---------------------------------------------------------------

uint8_t Machine::snd_mem_read(void*, uint16_t) { return 0xff; }

void Machine::snd_mem_write(void* ctx, uint16_t addr, uint8_t)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0x9000) == 0x9000) {
        m.audio_to(m.sound_now());
        m.set_filters(uint16_t(addr & 0x0fff));
    }
}

uint8_t Machine::snd_port_read(void* ctx, uint16_t port)
{
    Machine& m = *static_cast<Machine*>(ctx);
    uint8_t v = 0xff;
    if (port & 0x20) v &= m.ay_[1].read_data();
    if (port & 0x80) {
        const int at = m.sound_now();
        m.ay_[0].set_port_in(0, m.latch_);
        m.ay_[0].set_port_in(1, timer_bits((m.snd_base_ + uint64_t(at)) * 8));
        v &= m.ay_[0].read_data();
        // In a timer wait (RST 08 called from one of the loops): stop after
        // this IN, so sound_to() can skip the passes to come.
        if (m.wait_loops_ && m.idle_skip_ && !m.snd_irq_ && (port & 0xff) == 0x80 && m.snd_.pc.w == 0x000c) {
            const uint16_t ret = uint16_t(m.snd_.rd(m.snd_.sp.w) | (m.snd_.rd(uint16_t(m.snd_.sp.w + 1)) << 8));
            if (ret == 0x01e5 || ret == 0x01ec) {
                m.wait_read_ = true;
                m.wait_stay_ = ret == 0x01e5 ? 0x80 : 0x00;
                m.wait_at_ = at;
                m.snd_.stop();
            }
        }
    }
    return v;
}

void Machine::snd_port_write(void* ctx, uint16_t port, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (port & 0x30) {
        if (port & 0x10) {
            m.ay_[1].write_address(v);
        } else {
            m.audio_to(m.sound_now());
            m.ay_[1].write_data(v);
        }
    }
    if (port & 0xc0) {
        if (port & 0x40) {
            m.ay_[0].write_address(v);
        } else {
            m.audio_to(m.sound_now());
            m.ay_[0].write_data(v);
        }
    }
}

void Machine::sound_to(int main_cycle)
{
    if (main_cycle > kCyclesPerFrame) main_cycle = kCyclesPerFrame;
    const int target = int(int64_t(main_cycle) * snd_frame_ / kCyclesPerFrame);
    while (snd_done_ < target) {
        if (snd_irq_) {
            // The flip-flop holds the line until the CPU takes it (IM 1):
            // an instruction at a time, so it drops at once.
            const bool ready = snd_.iff1 != 0;
            snd_done_ += snd_.run(1);
            if (ready && !snd_.iff1) {
                snd_irq_ = false;
                snd_.irq(false);
            }
            continue;
        }
        snd_done_ += snd_.run(target - snd_done_);
        if (wait_read_) skip_timer_wait(target);
    }
}

void Machine::skip_timer_wait(int target)
{
    wait_read_ = false;
    if ((snd_.af.b.h & 0x80) != wait_stay_) return;     // this pass leaves the loop
    // Passes whose read (69 cycles apart) still sees the same bit 7: the
    // bit flips every 2560 cycles (20480 crystal clocks).
    constexpr int kHalf = int(kTimerPeriod / 8 / 2);
    const int phase = int((snd_base_ + uint64_t(wait_at_)) % uint64_t(2 * kHalf));
    const int left = wait_stay_ ? 2 * kHalf - phase : kHalf - phase;   // cycles until it flips (> 0)
    int passes = (left - 1) / kWaitPass;                // later reads still before the flip
    const int room = (target - snd_done_) / kWaitPass - 1;
    if (passes > room) passes = room;                   // the CPU runs the last pass itself
    if (passes <= 0) return;
    snd_done_ += passes * kWaitPass;
    skipped_ += uint64_t(passes) * kWaitPass;
    snd_.r = uint8_t(snd_.r + passes * kWaitFetches);
    snd_.af.b.h = timer_bits((snd_base_ + uint64_t(wait_at_ + passes * kWaitPass)) * 8);
    snd_.af.b.l = wait_stay_ ? 0x90 : 0x54;             // AND 80: S H, or Z H P
}

// --- audio -----------------------------------------------------------------------

void Machine::set_filters(uint16_t bits)
{
    filter_ = bits;
    for (int c = 0; c < 6; ++c) {
        const int b = (bits >> (2 * c)) & 3;
        const float cap = ((b & 1) ? kCapLow : 0) + ((b & 2) ? kCapHigh : 0);
        lp_a_[c] = cap > 0 ? 1.0f - std::exp(-1.0f / (kFilterR * cap * float(audio_rate_))) : 0;
    }
}

int16_t Machine::mix_sample(uint32_t clocks)
{
    const float scale = clocks ? 1.0f / float(clocks) : 0;
    float sum = 0;
    for (int c = 0; c < 6; ++c) {
        const float x = float(acc_[c]) * scale;
        lp_y_[c] = lp_a_[c] > 0 ? lp_y_[c] + lp_a_[c] * (x - lp_y_[c]) : x;
        sum += lp_y_[c];
    }
    if (control_ & 0x10) sum = 0;                       // muted
    const float out = dc_r_ * (dc_y_ + sum - dc_x_);
    dc_x_ = sum;
    dc_y_ = out;
    float s = out * kOutGain;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return int16_t(s);
}

void Machine::audio_to(int snd_cycle)
{
    if (snd_cycle > snd_frame_) snd_cycle = snd_frame_;
    while (audio_clock_ < snd_cycle) {
        const int end = audio_done_ < audio_n_ ? int(int64_t(audio_done_ + 1) * snd_frame_ / audio_n_) : snd_frame_;
        const int to = snd_cycle < end ? snd_cycle : end;
        ay_[0].run(uint32_t(to - audio_clock_), acc_);
        ay_[1].run(uint32_t(to - audio_clock_), acc_ + 3);
        audio_clock_ = to;
        if (to == end && audio_done_ < audio_n_) {
            const int16_t s = mix_sample(uint32_t(end - sample_start_));
            if (audio_) audio_[audio_done_] = s;
            ++audio_done_;
            sample_start_ = end;
            for (uint32_t& a : acc_) a = 0;
        }
    }
}

// --- frame ---------------------------------------------------------------------------

void Machine::vblank()
{
    video_.capture(state_, vram_, obj_, background_, false, 0);
    if (nmi_enable_) cpu_.nmi();
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // The sound board's cycles this frame: 50688 x (14.318 MHz / 8) / 3.072 MHz.
    snd_rem_ += uint64_t(kCyclesPerFrame) * kSoundXtal;
    snd_frame_ = int(snd_rem_ / (uint64_t(kCpuClock) * 8));
    snd_rem_ %= uint64_t(kCpuClock) * 8;

    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = n > 0 ? n : 0;
    audio_done_ = 0;
    audio_clock_ = 0;
    sample_start_ = 0;
    for (uint32_t& a : acc_) a = 0;

    slice_start_ = carry_;
    int want = kVblankLine * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    slice_start_ = kVblankLine * kCyclesPerLine + carry_;
    vblank();
    want = (kLinesPerFrame - kVblankLine) * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    sound_to(kCyclesPerFrame);
    audio_to(snd_frame_);
    ++frame_;

    // The sound CPU's overrun carries into the next frame.
    snd_base_ += uint64_t(snd_frame_);
    snd_done_ -= snd_frame_;
    const int done = audio_ ? audio_n_ : 0;
    audio_ = nullptr;
    return done;
}

} // namespace amidar
