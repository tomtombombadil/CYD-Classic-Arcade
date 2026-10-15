// Williams Defender / Robotron boards - see williams.h.
//
// Robotron (main CPU):
//   0000-8FFF  reads: ROM when C900 bit 0 is set, else video RAM; writes
//              always go to the video RAM
//   9000-97FF  video RAM          9800-BFFF  work RAM
//   C000-C00F  palette (write; mirrored to C3FF)
//   C804-C807  PIA "widget": A = IN0 (move, starts, fire up / down), B = IN1
//   C80C-C80F  PIA "ROM": A = IN2 (coins, service), B = sound command,
//              CA1 = line >= 240, CB1 = line bit 5, IRQs to the CPU
//              (both PIAs mirrored every 0x10 to C8FF)
//   C900       ROM enable (bit 0), flip (bit 1)
//   CA00-CA07  blitter (mirrored to CAFF)
//   CB00       video counter: the line & FC (FC from line 256 on)
//   CBFF       watchdog: write 39
//   CC00-CFFF  CMOS RAM (4 bits)        D000-FFFF  ROM
//
// Defender (main CPU):
//   0000-BFFF  RAM (video 0000-97FF)
//   C000-CFFF  bank 0: I/O - C000-C00F palette (mirrored every 0x20 to C3FF),
//              C010-C01F flip, C3FC watchdog (any data; the game writes
//              38 - MAME maps C3FF / 39 and never times it out), C400-C4FF
//              CMOS (to C7FF),
//              C800-CBFF video counter, CC00-CC03 the ROM PIA, CC04-CC07 the
//              widget PIA (mirrored every 0x20); banks 1, 2, 3, 7: ROM
//   D000-DFFF  write: the C000 bank (0-15)
//   D000-FFFF  ROM
//
// All inputs are active high. Facts from MAME's driver (read for facts only).
//
// The frame's start line: render_line() shows the video RAM as it is when
// run_frame() returns, while the real beam reads each line at its own time.
// These games race the beam (an object is erased and redrawn while the
// beam is elsewhere), so the snapshot moment matters in theory: frames run
// from line kFrameStartLine (mid-screen) to the same line of the next
// frame. Counting one-frame pixel dropouts over 550 frames of Robotron's
// play found no difference between lines 0, 128 and 240 (the dropouts
// seen are the game's own effects), so this is a choice, not a fix.
#include "williams.h"

#include <cstring>

namespace williams {

namespace {

constexpr int kFrameStartLine = 128;

// The watchdog resets the board when it isn't fed for this many frames
// (an assumption: the real counter's length isn't documented; the games
// feed it every frame or two).
constexpr int kWatchdogFrames = 60;

// The main CPU runs in slices of this many cycles; between them it is
// checked for an idle loop, followed for at most kIdleProbe instructions.
constexpr int kSliceCycles = 128;
constexpr int kIdleProbe = 12;

// The first boot (the CMOS RAM is empty, so the games restore their
// factory settings): Robotron then shows "FACTORY SETTINGS RESTORED" and
// waits for Advance; Defender shows "PRESS ADVANCE WITH SWITCH SET FOR
// AUTO FOR GAME OVER" for about a second, then goes into the bookkeeping
// pages. So once the CMOS writes have stopped for this many frames, the
// machine presses Advance (with Auto Up on Defender) for the player.
constexpr uint32_t kRobotronQuietFrames = 60;
constexpr uint32_t kDefenderQuietFrames = 10;

// Defender: the RAM byte with the ship's facing, 03 = right, FD = left
// (found by watching the Green label game reverse; other values = not
// playing). The Red label is assumed to keep it there - checked only by
// the value test below.
constexpr uint16_t kDefenderFacing = 0xa098;

}

// --- ROM sets -------------------------------------------------------------------------

namespace robotron {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu_banked", 0x9000},
    {"maincpu",        0x3000},
    {"soundcpu",       0x1000},
};

// MAME's "robotron" (Release 5, solid blue label); older dumps name the
// files robotron.sb1 ... robotron.sbc, robotron.snd. The two decoder PROMs
// (flip screen) aren't needed.
constexpr romset::Rom kRoms[] = {
    {kRegionBanked, 0x0000, 0x1000, 0x66c7d3ef, "2084_rom_1b_3005-13.e4",  false},
    {kRegionBanked, 0x1000, 0x1000, 0x5bc6c614, "2084_rom_2b_3005-14.c4",  false},
    {kRegionBanked, 0x2000, 0x1000, 0xe99a82be, "2084_rom_3b_3005-15.a4",  false},
    {kRegionBanked, 0x3000, 0x1000, 0xafb1c561, "2084_rom_4b_3005-16.e5",  false},
    {kRegionBanked, 0x4000, 0x1000, 0x62691e77, "2084_rom_5b_3005-17.c5",  false},
    {kRegionBanked, 0x5000, 0x1000, 0xbd2c853d, "2084_rom_6b_3005-18.a5",  false},
    {kRegionBanked, 0x6000, 0x1000, 0x49ac400c, "2084_rom_7b_3005-19.e6",  false},
    {kRegionBanked, 0x7000, 0x1000, 0x3a96e88c, "2084_rom_8b_3005-20.c6",  false},
    {kRegionBanked, 0x8000, 0x1000, 0xb124367b, "2084_rom_9b_3005-21.a6",  false},
    {kRegionFixed,  0x0000, 0x1000, 0x13797024, "2084_rom_10b_3005-22.a7", false},
    {kRegionFixed,  0x1000, 0x1000, 0x7e3c1b87, "2084_rom_11b_3005-23.c7", false},
    {kRegionFixed,  0x2000, 0x1000, 0x645d543e, "2084_rom_12b_3005-24.e7", false},
    {kRegionSound,  0x0000, 0x1000, 0xc56c1d28, "video_sound_rom_3_std_767.ic12", false},
};

const romset::Set kRomSet = {
    "robotron", nullptr, "Robotron: 2084", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

} // namespace robotron

namespace defender {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x3000},
    {"banked",   0x4000},
    {"soundcpu", 0x0800},
};

const char* const kVariants[] = {"defender", "defenderg"};

// MAME's "defender" (Red label, variant 1) and "defenderg" (Green label,
// variant 2); the sound ROM is the same. Bank 7 (defend.6) sits at 0x3000
// of the banked region (MAME: 0x6000; banks 4-6 are empty).
constexpr romset::Rom kRoms[] = {
    {kRegionFixed,  0x0000, 0x0800, 0xc3e52d7e, "defend.1",     false, 1},
    {kRegionFixed,  0x0800, 0x0800, 0x9a72348b, "defend.4",     false, 1},
    {kRegionFixed,  0x1000, 0x1000, 0x89b75984, "defend.2",     false, 1},
    {kRegionFixed,  0x2000, 0x1000, 0x94f51e9b, "defend.3",     false, 1},
    {kRegionBanked, 0x0000, 0x0800, 0x6870e8a5, "defend.9",     false, 1},
    {kRegionBanked, 0x0800, 0x0800, 0xf1f88938, "defend.12",    false, 1},
    {kRegionBanked, 0x1000, 0x0800, 0xb649e306, "defend.8",     false, 1},
    {kRegionBanked, 0x1800, 0x0800, 0x9deaf6d9, "defend.11",    false, 1},
    {kRegionBanked, 0x2000, 0x0800, 0x339e092e, "defend.7",     false, 1},
    {kRegionBanked, 0x2800, 0x0800, 0xa543b167, "defend.10",    false, 1},
    {kRegionBanked, 0x3000, 0x0800, 0x65f4efd1, "defend.6",     false, 1},
    {kRegionFixed,  0x0000, 0x0800, 0x6111d74d, "defeng01.bin", false, 2},
    {kRegionFixed,  0x0800, 0x0800, 0x3cfc04ce, "defeng04.bin", false, 2},
    {kRegionFixed,  0x1000, 0x1000, 0xd184ab6b, "defeng02.bin", false, 2},
    {kRegionFixed,  0x2000, 0x1000, 0x788b76d7, "defeng03.bin", false, 2},
    {kRegionBanked, 0x0000, 0x0800, 0xf57caa62, "defeng09.bin", false, 2},
    {kRegionBanked, 0x0800, 0x0800, 0x33db686f, "defeng12.bin", false, 2},
    {kRegionBanked, 0x1000, 0x0800, 0x9a9eb3d2, "defeng08.bin", false, 2},
    {kRegionBanked, 0x1800, 0x0800, 0x5ca4e860, "defeng11.bin", false, 2},
    {kRegionBanked, 0x2000, 0x0800, 0x545c3326, "defeng07.bin", false, 2},
    {kRegionBanked, 0x2800, 0x0800, 0x941cf34e, "defeng10.bin", false, 2},
    {kRegionBanked, 0x3000, 0x0800, 0x3af34c05, "defeng06.bin", false, 2},
    {kRegionSound,  0x0000, 0x0800, 0xfefd5b48, "video_sound_rom_1.ic12", false},
};

const romset::Set kRomSet = {
    "defender", nullptr, "Defender", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

} // namespace defender

// --- setup -------------------------------------------------------------------------------

void Machine::init(Board board, const uint8_t* const* regions, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    board_ = board;
    audio_rate_ = audio_rate;
    swap_ = swap_rgb565_bytes;
    const uint8_t* sound_rom;
    uint32_t sound_size;
    if (board == Board::Robotron) {
        rom_banked_ = regions[robotron::kRegionBanked];
        rom_fixed_ = regions[robotron::kRegionFixed];
        sound_rom = regions[robotron::kRegionSound];
        sound_size = robotron::kRegions[robotron::kRegionSound].size;
    } else {
        rom_fixed_ = regions[defender::kRegionFixed];
        rom_banked_ = regions[defender::kRegionBanked];
        sound_rom = regions[defender::kRegionSound];
        sound_size = defender::kRegions[defender::kRegionSound].size;
    }

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.map_ram(0x0000, 0xc000, ram_);
    cpu_.map_read(0xd000, 0x3000, rom_fixed_);

    pia_rom_.ctx = this;
    pia_rom_.out_b = sound_command;
    pia_rom_.irq_changed = main_irq;

    sound_.init(sound_rom, sound_size, audio_rate);

    // BBGGGRRR through the resistor DAC: red and green 1200 / 560 / 330
    // ohm, blue 560 / 330 ohm, full scale = 255.
    const double rg[3] = {1.0 / 1200, 1.0 / 560, 1.0 / 330}, bl[2] = {1.0 / 560, 1.0 / 330};
    const double rg_sum = rg[0] + rg[1] + rg[2], bl_sum = bl[0] + bl[1];
    for (int i = 0; i < 256; ++i) {
        double r = 0, g = 0, b = 0;
        for (int k = 0; k < 3; ++k) {
            if (i & (1 << k)) r += rg[k];
            if (i & (8 << k)) g += rg[k];
        }
        for (int k = 0; k < 2; ++k)
            if (i & (64 << k)) b += bl[k];
        const int r8 = int(r / rg_sum * 255 + 0.5), g8 = int(g / rg_sum * 255 + 0.5), b8 = int(b / bl_sum * 255 + 0.5);
        uint16_t px = uint16_t(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
        if (swap_) px = uint16_t((px >> 8) | (px << 8));
        rgb565_[i] = px;
    }
    fc_ = 0;
    reset();
}

void Machine::reset()
{
    pia_in_.reset();
    pia_rom_.reset();
    blitter_.reset();
    sound_.reset();
    if (board_ == Board::Robotron) robotron_rom(false);
    else                           defender_bank(0);
    cpu_.irq(false);
    cpu_.reset();
    watchdog_ = 0;
    reverse_hold_ = 0;
}

void Machine::robotron_rom(bool on)
{
    bank_ = on ? 1 : 0;
    cpu_.map_read(0x0000, 0x9000, on ? rom_banked_ : ram_);
}

// Bank 0 = I/O (callbacks); 1, 2, 3, 7 = ROM; the rest read nothing.
void Machine::defender_bank(uint8_t bank)
{
    bank_ = bank & 0x0f;
    int page = -1;
    switch (bank_) {
    case 1: page = 0; break;
    case 2: page = 1; break;
    case 3: page = 2; break;
    case 7: page = 3; break;
    default: break;
    }
    cpu_.map_read(0xc000, 0x1000, page >= 0 ? rom_banked_ + page * 0x1000 : nullptr);
}

// --- main CPU bus -----------------------------------------------------------------------

int Machine::beam_line() const
{
    return (kFrameStartLine + now() / kCyclesPerLine) % kLinesPerFrame;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    ++m.io_accesses_;
    if (addr >= 0xc000 && addr < 0xd000) return m.io_read(addr);
    return 0;
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0xc000 && addr < 0xd000) {
        m.io_write(addr, v);
    } else if (addr >= 0xd000 && addr < 0xe000 && m.board_ == Board::Defender) {
        m.defender_bank(v);
    }
}

uint8_t Machine::io_read(uint16_t addr)
{
    if (board_ == Board::Robotron) {
        switch (addr & 0xff00) {
        case 0xc800:
            if ((addr & 0x0c) == 0x04) return pia_in_.read(addr & 3);
            if ((addr & 0x0c) == 0x0c) return pia_rom_.read(addr & 3);
            return 0;
        case 0xcb00: {
            const int line = beam_line();
            return line < 0x100 ? uint8_t(line & 0xfc) : 0xfc;
        }
        default:
            if (addr >= 0xcc00) return cmos_[addr & 0x3ff];
            return 0;
        }
    }
    if (bank_ != 0) return 0;               // an empty bank
    switch (addr & 0xfc00) {
    case 0xc400: return cmos_[addr & 0xff];
    case 0xc800: {
        const int line = beam_line();
        return line < 0x100 ? uint8_t(line & 0xfc) : 0xfc;
    }
    case 0xcc00:
        if ((addr & 0x1c) == 0x00) return pia_rom_.read(addr & 3);
        if ((addr & 0x1c) == 0x04) return pia_in_.read(addr & 3);
        return 0;
    default:
        return 0;
    }
}

void Machine::io_write(uint16_t addr, uint8_t v)
{
    if (board_ == Board::Robotron) {
        switch (addr & 0xff00) {
        case 0xc800:
            if ((addr & 0x0c) == 0x04) pia_in_.write(addr & 3, v);
            else if ((addr & 0x0c) == 0x0c) pia_rom_.write(addr & 3, v);
            return;
        case 0xc900:
            robotron_rom(v & 1);
            return;
        case 0xca00: {
            const int held = blitter_.write(addr & 7, v, cpu_, ram_);
            if (held) cpu_.add_cycles(held);
            return;
        }
        case 0xcb00:
            if (addr == 0xcbff && v == 0x39) watchdog_ = 0;
            return;
        default:
            if (addr < 0xc400) palette_[addr & 0x0f] = v;
            else if (addr >= 0xcc00) cmos_write(addr & 0x3ff, v);
            return;
        }
    }
    if (bank_ != 0) return;                 // ROM
    switch (addr & 0xfc00) {
    case 0xc000:
        if (addr >= 0xc3fc) {
            watchdog_ = 0;                  // Defender writes 38 to C3FC (no data check)
        } else if (!(addr & 0x10)) {
            palette_[addr & 0x0f] = v;
        }                                   // else flip screen: not drawn
        return;
    case 0xc400:
        cmos_write(addr & 0xff, v);
        return;
    case 0xcc00:
        if ((addr & 0x1c) == 0x00) pia_rom_.write(addr & 3, v);
        else if ((addr & 0x1c) == 0x04) pia_in_.write(addr & 3, v);
        return;
    default:
        return;
    }
}

// The CMOS RAM is 4 bits wide: the high nibble reads as 1s.
void Machine::cmos_write(int at, uint8_t v)
{
    cmos_[at] = uint8_t(v | 0xf0);
    ++cmos_writes_;
    last_cmos_frame_ = frame_;
}

// Port B of the ROM PIA: the sound board takes the low 6 bits.
void Machine::sound_command(void* ctx, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    m.sound_.run_to(int(int64_t(m.now()) * m.snd_cycles_ / kCyclesPerFrame));
    m.sound_.command(v & 0x3f);
    ++m.sound_commands_;
}

void Machine::main_irq(void* ctx)
{
    Machine& m = *static_cast<Machine*>(ctx);
    m.cpu_.irq(m.pia_rom_.irq());
}

// --- inputs -----------------------------------------------------------------------------

void Machine::set_input_ports(uint16_t b)
{
    uint8_t in0 = 0, in1 = 0, in2 = 0;
    if (board_ == Board::Robotron) {
        using namespace robotron;
        if (b & kUp)        in0 |= 0x01;
        if (b & kDown)      in0 |= 0x02;
        if (b & kLeft)      in0 |= 0x04;
        if (b & kRight)     in0 |= 0x08;
        if (b & kStart1)    in0 |= 0x10;
        if (b & kStart2)    in0 |= 0x20;
        if (b & kFireUp)    in0 |= 0x40;
        if (b & kFireDown)  in0 |= 0x80;
        if (b & kFireLeft)  in1 |= 0x01;
        if (b & kFireRight) in1 |= 0x02;
    } else {
        using namespace defender;
        if (b & kFire)       in0 |= 0x01;
        if (b & kThrust)     in0 |= 0x02;
        if (b & kSmartBomb)  in0 |= 0x04;
        if (b & kHyperspace) in0 |= 0x08;
        if (b & kStart2)     in0 |= 0x10;
        if (b & kStart1)     in0 |= 0x20;
        if (b & kReverse)    in0 |= 0x40;
        if (b & kDown)       in0 |= 0x80;
        if (b & kUp)         in1 |= 0x01;
    }
    // IN2 is the same on both: auto up, advance, right coin, high score
    // reset, left coin, centre coin, tilt.
    if (b & robotron::kAutoUp)          in2 |= 0x01;
    if (b & robotron::kAdvance)         in2 |= 0x02;
    if (b & robotron::kHighScoreReset)  in2 |= 0x08;
    if (b & robotron::kCoin)            in2 |= 0x10;
    if (b & robotron::kTilt)            in2 |= 0x40;
    if (board_ == Board::Robotron && (b & robotron::kCoin2)) in2 |= 0x20;
    pia_in_.in_a = in0;
    pia_in_.in_b = in1;
    pia_rom_.in_a = in2;
}

// Defender's stick: the cabinet has Reverse and Thrust buttons. Pushing
// the stick the way the ship faces thrusts; pushing it the other way
// presses Reverse until the ship has turned, then thrusts.
uint16_t Machine::defender_stick(uint16_t b)
{
    using namespace defender;
    const bool left = b & kLeft, right = b & kRight;
    const uint8_t facing = ram_[kDefenderFacing];
    if (left == right || (facing != 0x03 && facing != 0xfd)) {     // no push, or not playing
        reverse_hold_ = 0;
        return b;
    }
    if (left != (facing == 0xfd)) {
        // Reverse in short presses (3 frames on, 3 off) until the ship
        // has turned - the game turns on the press and shows it a frame
        // later.
        if (reverse_hold_++ % 6 < 3) b |= kReverse;
    } else {
        reverse_hold_ = 0;
        b |= kThrust;
    }
    return b;
}

// --- frame -----------------------------------------------------------------------------

// Runs the main CPU in slices of kSliceCycles; between slices, if the CPU
// is going round a loop that changes nothing, skips to `cycle` (where the
// next interrupt edge is).
void Machine::run_main_to(int cycle)
{
    while (fc_ < cycle) {
        const int slice = cycle - fc_ < kSliceCycles ? cycle - fc_ : kSliceCycles;
        fc_ += cpu_.run(slice);
        if (fc_ >= cycle) break;
        // A busy CPU (Defender draws everything itself) isn't probed after
        // every slice: each probe that finds no idle loop doubles the wait
        // before the next one (up to 16 slices).
        if (probe_wait_) {
            --probe_wait_;
            continue;
        }
        if (skip_idle(cycle)) {
            probe_backoff_ = 0;
        } else {
            probe_backoff_ = probe_backoff_ ? (probe_backoff_ < 16 ? probe_backoff_ * 2 : 16) : 1;
            probe_wait_ = probe_backoff_;
        }
    }
}

// Idle loops (Robotron waits for the next interrupt in "LDA <10 ; CMPA #2
// ; BCS"): follows the CPU one instruction at a time until its PC comes
// back to where it is now, twice. If one round trip left every register
// as it was, wrote nothing and touched no I/O, every further round will
// be the same until an interrupt - and the interrupt lines change only at
// `cycle` - so the whole rounds that end before `cycle` are counted at once. The RAM
// only changes by CPU writes (the blitter too runs on a CPU write), so this
// is exact.
bool Machine::skip_idle(int cycle)
{
    // fc_ moves with every step, so a sound command written meanwhile gets
    // the right time.
    const uint16_t pc0 = cpu_.pc;
    int first = -1;
    uint32_t writes = 0, io = 0;
    uint16_t d = 0, x = 0, y = 0, u = 0, s = 0;
    uint8_t dp = 0, cc = 0;
    for (int n = 0; n < kIdleProbe && fc_ < cycle; ++n) {
        fc_ += cpu_.run(1);
        if (cpu_.pc != pc0) continue;
        if (first < 0) {
            first = fc_;
            writes = cpu_.writes;
            io = io_accesses_;
            d = cpu_.d; x = cpu_.x; y = cpu_.y; u = cpu_.u; s = cpu_.s; dp = cpu_.dp; cc = cpu_.cc;
            continue;
        }
        if (cpu_.writes == writes && io_accesses_ == io && cpu_.d == d && cpu_.x == x && cpu_.y == y
            && cpu_.u == u && cpu_.s == s && cpu_.dp == dp && cpu_.cc == cc && !cpu_.in_cwai() && !cpu_.in_sync()) {
            // Whole rounds that end before `cycle`; the last one runs as usual.
            const int round = fc_ - first, left = cycle - fc_;
            if (left > round) {
                fc_ += (left - 1) / round * round;
                ++idle_skips_;
            }
            return true;
        }
        break;
    }
    return false;
}

int Machine::run_frame(int16_t* audio, int cap)
{
    uint16_t b = board_ == Board::Defender ? defender_stick(buttons_) : buttons_;
    const uint32_t quiet = board_ == Board::Robotron ? kRobotronQuietFrames : kDefenderQuietFrames;
    if (!factory_done_ && cmos_writes_ && frame_ - last_cmos_frame_ >= quiet) {
        factory_done_ = true;
        advance_left_ = 8;
        advanced_at_ = frame_;
    }
    if (advance_left_) {
        --advance_left_;
        b |= robotron::kAdvance;            // the same bits on both boards
        if (board_ == Board::Defender) b |= defender::kAutoUp;
    }
    set_input_ports(b);

    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    snd_rem_ += uint64_t(kCyclesPerFrame) * williams_sound::kCrystal;
    snd_cycles_ = int(snd_rem_ / (uint64_t(kCpuClock) * 4));
    snd_rem_ %= uint64_t(kCpuClock) * 4;
    sound_.begin_frame(audio && n > 0 ? audio : nullptr, n, snd_cycles_);

    // The line events from the frame's start line round to it again: CB1
    // follows bit 5 of the line counter (set at 0, 32, ... 224; the counter
    // runs 0-259, so 256 doesn't toggle it), CA1 = line >= 240.
    for (int i = 0; i < kLinesPerFrame; ++i) {
        const int line = (kFrameStartLine + i) % kLinesPerFrame;
        const bool cb1_edge = (line & 31) == 0 && line < 256;
        const bool ca1_edge = line == 0 || line == 240;
        if (!cb1_edge && !ca1_edge) continue;
        run_main_to(i * kCyclesPerLine);
        if (cb1_edge) pia_rom_.cb1(line & 32);
        if (ca1_edge) pia_rom_.ca1(line >= 240);
    }
    run_main_to(kCyclesPerFrame);
    fc_ -= kCyclesPerFrame;
    sound_.end_frame();

    if (++watchdog_ > watchdog_max_) watchdog_max_ = watchdog_;
    if (watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
    for (int i = 0; i < 16; ++i) pen_[i] = rgb565_[palette_[i]];
    ++frame_;
    return audio ? n : 0;
}

void Machine::render_line(int y, uint16_t* out) const
{
    const int line = y + kFirstLine;
    const int x0 = board_ == Board::Robotron ? 6 : 12;      // first visible pixel
    const uint8_t* src = ram_ + (x0 / 2) * 256 + line;
    for (int i = 0; i < kScreenW / 2; ++i, src += 256) {
        out[2 * i] = pen_[*src >> 4];
        out[2 * i + 1] = pen_[*src & 0x0f];
    }
}

} // namespace williams
