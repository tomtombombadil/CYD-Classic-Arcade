// Gyruss machine - see gyruss.h.
//
// Main Z80 (3.072 MHz):
//   0000-5FFF  program ROM (6000-7FFF: the empty diagnostics socket)
//   8000-83FF  colour RAM, 8400-87FF video RAM (32 x 32 tiles)
//   9000-9FFF  RAM
//   A000-A7FF  RAM shared with the sub CPU (its 6000-67FF): the sprite queue
//   C000       read DSW2 (write: the watchdog's address - no watchdog fitted)
//   C080       read SYSTEM; write: the sound CPU's IRQ (held until taken)
//   C0A0 / C0C0  read P1 / P2, C0E0 DSW1 (coinage)
//   C100       read DSW3; write the sound command
//   C180-C187  LS259 (bit 0 of the data): 0 NMI enable (clearing it drops
//              the NMI line), 2 / 3 coin counters, 5 flip screen
//   NMI at VBLANK (line 240) while enabled - the line stays high until the
//   enable is cleared, so one NMI a frame only if the handler clears it.
// Inputs are active low.
//
// Sub CPU (Konami-1 at 1.536 MHz):
//   0000       read: the beam's line (1V-128V)
//   2000       write: IRQ enable (bit 0; clearing it drops the IRQ line)
//   4000-47FF  RAM, sprites at 4040-40FF (48 x 4 bytes)
//   6000-67FF  the shared RAM
//   E000-FFFF  program ROM
//   IRQ at VBLANK while enabled (a level, until the enable is cleared).
//   Every opcode byte it fetches (the opcode and a 10 / 11 prefix's second
//   byte - not operands, not data) is XORed by its address bits 1 and 3:
//   22 82 28 88 (konami1_decrypt).
//
// Sound Z80 (3.579545 MHz):
//   0000-3FFF  ROM (4000-5FFF an empty socket), 6000-63FF RAM, 8000 read
//   the command. I/O: 00 / 04 / 08 / 0C / 10 = AY 1-5 address, +1 read,
//   +2 data; 14 = the i8039's interrupt, 18 = the i8039's command.
//   AY 1 / AY 2 port B: per channel two bits switching 0.047 uF (bit 0) and
//   0.22 uF (bit 1) onto its output: an RC low-pass with the chip's output
//   resistance + 1k. AY 3 port A: a timer, the CPU clock / 1024 through an
//   LS90 decade counter.
// i8039 (8 MHz): 4 KB ROM, every external read = its command, P1 = the
// DAC, a P2 write drops its interrupt.
//
// Idle skipping, exact (checked frame by frame against runs without it -
// picture and sound): each loop is recognised by its code at init.
// - Main CPU, 05F4: JP 05F7 / LD A,(5865) / LD H,A / LD A,(9433) / LD L,A /
//   LD A,(HL) / RLCA / JP C,05F4 - waits for a task queue entry to clear,
//   65 cycles and 8 opcode fetches a pass, reading only its own RAM. At a
//   slice's start in the loop (any of its instructions) with the entry still
//   set and no NMI just raised, nothing can change it before the slice ends:
//   whole passes are skipped, then instruction by instruction to where the
//   CPU would stop at the slice's end (time, R, PC, and A H L F as the loop
//   leaves them there).
// - Sub CPU, F0B6: BRA * (waits for the VBLANK IRQ), 3 cycles: skipped to
//   the slice's end while no interrupt can be taken.
// - Sound CPU, 00FE / 0105: LD A,0E / RST 10 (OUT (08),A / IN A,(09) / RET)
//   / AND 04 / JR NZ (JR Z) - waits for the timer's bit 2 to clear, then to
//   set: 69 cycles and 7 fetches a pass. The IN stops the CPU; the passes
//   whose reads would see the same bit 2 are skipped (time, R, A = the last
//   of them's read, F = its AND's), and when the pass that reads the change
//   ends before the CPU's target, that one too: the CPU is left after the
//   loop (A = the read AND 04, F, R, time as it leaves them). Never while an
//   IRQ waits.
// - i8039, 0019: NOP / JMP 0019 (waits for its interrupt), 3 cycles; and
//   any DJNZ Rr to itself (the effects' delays after each DAC write, which
//   stops the CPU), 2 cycles, all passes but the last. Skipped: time, the
//   register, the timer prescaler (never past an overflow that would
//   interrupt).
//
// Mixing (MAME's model of the board, read for its part values): right =
// AY 1's channels through 2.2k each, AY 3 and AY 4 (each chip's three
// tied through 3.3k each); left = AY 2 through 2.2k, AY 5 (3.3k each) and
// the DAC through 4.7k; both into an output RC (200 ohm, 0.1 uF). Mono
// here: left + right.
#include "gyruss.h"

#include <cmath>
#include <cstring>

namespace gyruss {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x6000},
    {"sub",      0x2000},
    {"audiocpu", 0x4000},
    {"audio2",   0x1000},
    {"sprites",  0x8000},
    {"tiles",    0x2000},
    {"proms",    0x0220},
};

// CRCs as in MAME's "gyruss" (Konami).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,     0x0000, 0x2000, 0xc673b43d, "gyrussk.1", false},
    {kRegionCpu,     0x2000, 0x2000, 0xa4ec03e4, "gyrussk.2", false},
    {kRegionCpu,     0x4000, 0x2000, 0x27454a98, "gyrussk.3", false},
    {kRegionSub,     0x0000, 0x2000, 0x822bf27e, "gyrussk.9", false},
    {kRegionSound,   0x0000, 0x2000, 0xf4ae1c17, "gyrussk.1a", false},
    {kRegionSound,   0x2000, 0x2000, 0xba498115, "gyrussk.2a", false},
    {kRegionI8039,   0x0000, 0x1000, 0x3f9b5dea, "gyrussk.3a", false},
    {kRegionSprites, 0x0000, 0x2000, 0xc949db10, "gyrussk.6", false},
    {kRegionSprites, 0x2000, 0x2000, 0x4f22411a, "gyrussk.5", false},
    {kRegionSprites, 0x4000, 0x2000, 0x47cd1fbc, "gyrussk.8", false},
    {kRegionSprites, 0x6000, 0x2000, 0x8e8d388c, "gyrussk.7", false},
    {kRegionTiles,   0x0000, 0x2000, 0x27d8329b, "gyrussk.4", false},
    {kRegionProms,   0x0000, 0x0020, 0x98782db3, "gyrussk.pr3", false},
    {kRegionProms,   0x0020, 0x0100, 0x7ed057de, "gyrussk.pr1", false},
    {kRegionProms,   0x0120, 0x0100, 0xde823a81, "gyrussk.pr2", false},
};

// The LS90 decade counter's outputs as AY 3's port A reads them, by count.
constexpr uint8_t kTimer[10] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x0a, 0x0b, 0x0a, 0x0d};

// AY 1 / AY 2 output filters: the chip's output resistance (~356 ohm,
// MAME's figure) + 1k, into the switched capacitors.
constexpr float kFilterR = 1356.0f;
constexpr float kCapBit0 = 0.047e-6f, kCapBit1 = 0.22e-6f;

// Mixer weights (conductance shares on each side; a tied chip's channels
// count a third each). The DAC: 4 V full swing against an AY channel's ~1 V
// (assumed).
constexpr float kWAy1 = 0.143f, kWAy34 = 0.0953f, kWAy2 = 0.183f, kWAy5 = 0.122f;
constexpr float kWDac = 0.0856f * 4.0f / 255.0f * float(ay8910::kMaxLevel);
constexpr float kOutHz = 7958.0f;       // 200 ohm / 0.1 uF
constexpr float kDcHz = 20.0f;          // output coupling (assumed)
constexpr float kOutGain = 9.0f;          // play peaks ~20,000 (PC runs)

// A filter state that has decayed below the smallest normal float is 0:
// denormal arithmetic is very slow on many CPUs (here: the PC ran its mix
// 4x slower on the silent channels' tails), and so small a value never
// reaches an output sample (they are whole numbers: --hash the same).
// (Exponent 0 = +-0 or a denormal: the same as |v| < FLT_MIN, NaN kept;
// float compares only - the S3 moves nothing to the integer registers.)
inline float flush_tiny(float v)
{
    return __builtin_fabsf(v) < 1.17549435e-38f ? 0.0f : v;
}

// The idle loops (see above).
constexpr uint8_t kMainLoop[] = {0xc3, 0xf7, 0x05, 0x3a, 0x65, 0x58, 0x67, 0x3a, 0x33, 0x94, 0x6f, 0x7e, 0x07,
                                 0xda, 0xf4, 0x05};                     // at 05F4
constexpr uint16_t kMainLoopAt = 0x05f4;
constexpr int kMainPass = 65, kMainFetches = 8;
// The addresses of the loop's instructions.
constexpr uint16_t kMainLoopPcs[] = {0x05f4, 0x05f7, 0x05fa, 0x05fb, 0x05fe, 0x05ff, 0x0600, 0x0601};
constexpr uint8_t kMainCycles[] = {10, 13, 4, 13, 4, 7, 4, 10};    // their T-states (65)
constexpr uint16_t kSubLoopAt = 0xf0b6;                                 // BRA * (opcode scrambled)
constexpr uint8_t kSndLoop[] = {0x3e, 0x0e, 0xd7, 0xe6, 0x04, 0x20, 0xf9,
                                0x3e, 0x0e, 0xd7, 0xe6, 0x04, 0x28, 0xf9};   // at 00FE
constexpr uint8_t kSndRst[] = {0xd3, 0x08, 0xdb, 0x09, 0xc9};          // at 0010
constexpr int kSndPass = 69, kSndFetches = 7;
constexpr int kSndExit = 93, kSndExitFetches = 10;      // from after an IN to after the loop (see skip_timer_wait)
constexpr uint8_t kMcuLoop[] = {0x00, 0x04, 0x19};                      // at 0019
constexpr int kMcuPass = 3;

// The i8039 at a DJNZ Rr jumping to itself (a delay loop).
bool djnz_self(const i8035::Cpu& c)
{
    const uint8_t op = c.rom[c.pc & c.rom_mask];
    return (op & 0xf8) == 0xe8 && (c.pc & 0xff) != 0xff && c.rom[(c.pc + 1) & c.rom_mask] == (c.pc & 0xff);
}

// Palette PROM: red and green 1K / 470 / 220 ohm, blue 470 / 220, into 470
// ohm: conductance shares, scaled so the brightest (red or green full) is
// 255.
uint16_t rgb565(int r, int g, int b, bool swap)
{
    const uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return swap ? uint16_t((px >> 8) | (px << 8)) : px;
}

} // namespace

const romset::Set kRomSet = {
    "gyruss", nullptr, "Gyruss", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

uint8_t Machine::timer_bits(uint64_t sound_cycles) { return kTimer[(sound_cycles / 1024) % 10]; }

// The same at this frame's sound cycle `at`, in 32 bits.
uint8_t Machine::timer_at(int at) const { return kTimer[((snd_base10_ + uint32_t(at)) % 10240) / 1024]; }

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap)
{
    roms_ = roms;
    audio_rate_ = audio_rate;
    audio_q_ = uint32_t(uint64_t(audio_rate) * kCyclesPerFrame / kCpuClock);
    audio_r_ = uint32_t(uint64_t(audio_rate) * kCyclesPerFrame % kCpuClock);

    // Palette.
    {
        const float g1 = 1.0f / 1000, g2 = 1.0f / 470, g3 = 1.0f / 220, gl = 1.0f / 470;
        const float rg_sum = g1 + g2 + g3 + gl, b_sum = g2 + g3 + gl;
        const float scale = 255.0f / ((g1 + g2 + g3) / rg_sum);
        const float wrg[3] = {g1 / rg_sum * scale, g2 / rg_sum * scale, g3 / rg_sum * scale};
        const float wb[2] = {g2 / b_sum * scale, g3 / b_sum * scale};
        for (int e = 0; e < 32; ++e) {
            const uint8_t v = roms.proms[e];
            const float r = wrg[0] * (v & 1) + wrg[1] * ((v >> 1) & 1) + wrg[2] * ((v >> 2) & 1);
            const float g = wrg[0] * ((v >> 3) & 1) + wrg[1] * ((v >> 4) & 1) + wrg[2] * ((v >> 5) & 1);
            const float b = wb[0] * ((v >> 6) & 1) + wb[1] * ((v >> 7) & 1);
            pens_[e] = rgb565(int(r + 0.5f), int(g + 0.5f), int(b + 0.5f), swap);
        }
        for (int i = 0; i < 256; ++i) sprite_pen_[i] = pens_[roms.proms[0x20 + i] & 0x0f];
        for (int i = 0; i < 64; ++i) tile_pen_[i] = pens_[0x10 | (roms.proms[0x120 + i] & 0x0f)];
    }

    // Tiles by column (see render_line).
    for (int code = 0; code < 512; ++code)
        for (int c = 0; c < 8; ++c) {
            const uint8_t* g = roms.tiles + code * 16 + ((c & 4) << 1);
            uint16_t bits = 0;
            for (int r = 0; r < 8; ++r)
                bits |= uint16_t(((((g[r] >> (3 - (c & 3))) & 1) << 1) | ((g[r] >> (7 - (c & 3))) & 1)) << (2 * r));
            tile_cols_[code * 8 + c] = bits;
        }

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = no_in;
    cpu_.port_out = no_out;
    cpu_.map_read(0x0000, 0x6000, roms.cpu);
    cpu_.map_ram(0x8000, 0x800, vcram_);
    cpu_.map_ram(0x9000, 0x1000, ram_);
    cpu_.map_ram(0xa000, 0x800, shared_);

    sub_.ctx = this;
    sub_.mem_read = sub_read;
    sub_.mem_write = sub_write;
    sub_.op_read = sub_opcode;
    sub_.map_read(0x4000, 0x800, sub_ram_);
    sub_.map_write(0x4400, 0x400, sub_ram_ + 0x400);   // 4000-43FF: through sub_write (sprites)
    sub_.map_ram(0x6000, 0x800, shared_);
    sub_.map_read(0xe000, 0x2000, roms.sub);
    // Every opcode fetch is decrypted: the program's from a decrypted copy,
    // anywhere else (never seen) through sub_opcode.
    for (int a = 0; a < 0x2000; ++a) sub_ops_[a] = konami1_decrypt(uint16_t(0xe000 + a), roms.sub[a]);
    sub_.map_opcodes(0x0000, 0xe000, nullptr);
    sub_.map_opcodes(0xe000, 0x2000, sub_ops_);

    snd_.ctx = this;
    snd_.mem_read = snd_mem_read;
    snd_.mem_write = snd_mem_write;
    snd_.port_in = snd_port_read;
    snd_.port_out = snd_port_write;
    snd_.map_read(0x0000, 0x4000, roms.sound);
    snd_.map_ram(0x6000, 0x400, snd_ram_);

    mcu_.ctx = this;
    mcu_.rom = roms.i8039;
    mcu_.rom_mask = 0x0fff;
    mcu_.ram_mask = 0x7f;                               // the 8039's 128 bytes
    mcu_.ext_read = mcu_ext_read;
    mcu_.port_in = mcu_port_read;
    mcu_.port_out = mcu_port_write;

    main_loop_ = std::memcmp(roms.cpu + kMainLoopAt, kMainLoop, sizeof kMainLoop) == 0;
    sub_loop_ = konami1_decrypt(kSubLoopAt, roms.sub[kSubLoopAt - 0xe000]) == 0x20 &&
                roms.sub[kSubLoopAt + 1 - 0xe000] == 0xfe;
    snd_loop_ = std::memcmp(roms.sound + 0xfe, kSndLoop, sizeof kSndLoop) == 0 &&
                std::memcmp(roms.sound + 0x10, kSndRst, sizeof kSndRst) == 0;
    mcu_loop_ = std::memcmp(roms.i8039 + 0x19, kMcuLoop, sizeof kMcuLoop) == 0;

    // The filters' steps by a channel's two capacitor bits (set_filter).
    for (int b = 0; b < 4; ++b) {
        const float cap = ((b & 1) ? kCapBit0 : 0) + ((b & 2) ? kCapBit1 : 0);
        lp_tab_[b] = cap > 0 ? 1.0f - std::exp(-1.0f / (kFilterR * cap * float(audio_rate_))) : 0;
    }
    out_a_ = 1.0f - std::exp(-2.0f * 3.14159265f * kOutHz / float(audio_rate));
    dc_r_ = std::exp(-2.0f * 3.14159265f * kDcHz / float(audio_rate));
    reset();
}

void Machine::reset()
{
    cpu_.reset();
    sub_.reset();
    snd_.reset();
    mcu_.reset();
    for (ay8910::Chip& a : ay_) a.reset();
    nmi_mask_ = nmi_line_ = false;
    sub_irq_mask_ = false;
    sub_.irq(false);
    latch_ = latch2_ = 0;
    snd_irq_ = false;
    snd_.irq(false);
    mcu_.irq(false);
    set_filter(0, 0);
    set_filter(1, 0);
    carry_ = 0;
    sub_due_ = 0;
    sub_lag_ = false;
    sub_vbl_ = false;
    sub_log_n_ = 0;
    cmd_n_ = 0;
    snd_defer_ = false;
    cpu_.map_ram(0xa000, 0x800, shared_);
}

uint8_t Machine::in(int port) const
{
    uint8_t v = 0xff;
    switch (port) {
    case 0:                                             // SYSTEM
        if (buttons_ & kCoin)    v &= uint8_t(~0x01);
        if (buttons_ & kCoin2)   v &= uint8_t(~0x02);
        if (buttons_ & kService) v &= uint8_t(~0x04);
        if (buttons_ & kStart1)  v &= uint8_t(~0x08);
        if (buttons_ & kStart2)  v &= uint8_t(~0x10);
        return v;
    case 1:                                             // P1
        if (buttons_ & kLeft)  v &= uint8_t(~0x01);
        if (buttons_ & kRight) v &= uint8_t(~0x02);
        if (buttons_ & kUp)    v &= uint8_t(~0x04);
        if (buttons_ & kDown)  v &= uint8_t(~0x08);
        if (buttons_ & kFire)  v &= uint8_t(~0x10);
        return v;
    case 2:                                             // DSW2: upright, demo sounds
        return uint8_t((dips_ & 0x7b) | (demo_sounds_ ? 0 : 0x80));
    case 3:                                             // DSW3: demo music
        return demo_sounds_ ? 0xfe : 0xff;
    default:                                            // P2 (cocktail), DSW1 = 1 coin 1 credit
        return 0xff;
    }
}

// --- main CPU ------------------------------------------------------------------

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0xa000) {                    // the shared RAM while the sub CPU is behind
        m.sub_catch_up();
        return m.shared_[addr & 0x7ff];
    }
    if ((addr & 0xfe00) != 0xc000) return 0xff;        // 6000-7FFF: the empty socket; no chips elsewhere
    switch (addr & 0x1e0) {
    case 0x000: return m.in(2);
    case 0x080: return m.in(0);
    case 0x0a0: return m.in(1);
    case 0x100: return m.in(3);
    default:    return 0xff;                            // C0C0 P2, C0E0 DSW1
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0xa000) {
        // The sub CPU sees it from its turn of this slice on: kept until
        // the sub CPU runs (sub_catch_up), which then takes it there.
        if (m.sub_log_n_ == kSubLog) m.sub_catch_up();
        if (m.sub_lag_) {
            m.sub_log_[m.sub_log_n_++] = {uint16_t(m.sub_due_), uint16_t(addr & 0x7ff), v};
            return;
        }
        m.shared_[addr & 0x7ff] = v;
        return;
    }
    if ((addr & 0xfe00) != 0xc000) return;
    switch (addr & 0x180) {
    case 0x080:                                         // C080: the sound CPU's IRQ
    case 0x100:                                         // C100: the sound command
        // Queued: the sound board runs to now and takes it at the frame's
        // end (flush_cmds; here only when the queue is full).
        if (m.cmd_n_ == kMaxCmds) m.flush_cmds();
        m.cmds_[m.cmd_n_++] = {m.now(), uint8_t((addr & 0x180) == 0x100), v};
        break;
    case 0x180:                                         // C180-C187: LS259
        if ((addr & 7) == 0) {
            m.nmi_mask_ = v & 1;
            if (!m.nmi_mask_) m.nmi_line_ = false;
        }
        break;
    default:
        break;
    }
}

// --- sub CPU ----------------------------------------------------------------------

uint8_t Machine::sub_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<Machine*>(ctx);
    if (addr < 0x2000) return uint8_t(m.sub_now() * 2 / kCyclesPerLine);    // the beam's line
    return 0xff;
}

void Machine::sub_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xfc00) == 0x4000) {
        if ((addr & 0x3ff) >= 0x40 && (addr & 0x3ff) < 0x100 && m.sub_ram_[addr & 0x3ff] != v)
            m.sprite_write((addr & 0x3ff) - 0x40, v);
        m.sub_ram_[addr & 0x3ff] = v;
    } else if ((addr & 0xe000) == 0x2000) {
        m.sub_irq_mask_ = v & 1;
        if (!m.sub_irq_mask_) m.sub_.irq(false);
    }
}

uint8_t Machine::sub_opcode(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    return konami1_decrypt(addr, m.sub_.rd(addr));
}

// The sub CPU's turns come after the main CPU's (2 lines each) but are put
// off until the main CPU reads the shared RAM (its pages are unmapped while
// the sub CPU is behind) or the frame's end: then the sub CPU runs
// all of them in one go. The main CPU's writes meanwhile are kept with the
// turn they belong to (sub_log_) and put into the shared RAM when the sub
// CPU gets there. The same instructions at the same times: what it reads
// is as it would have been, and where a turn ends never matters to it (a
// wait, BRA * or an idle skip only runs on to the later end), and its VBLANK
// is taken where it reaches line 240 (run_sub). (~19 sub CPU runs a frame
// -> ~2: each one cost the S3's instruction cache the 6809's code and then
// the Z80's again.)
void Machine::sub_catch_up()
{
    // The main CPU's writes kept while it ran ahead, each where the sub CPU
    // would have seen it.
    for (int i = 0; i < sub_log_n_; ++i) {
        const SubWrite& w = sub_log_[i];
        if (sub_done_ < w.due) run_sub(w.due);
        shared_[w.addr] = w.v;
    }
    sub_log_n_ = 0;
    if (sub_done_ < sub_due_) run_sub(sub_due_);
    if (sub_lag_) {
        sub_lag_ = false;
        cpu_.map_ram(0xa000, 0x800, shared_);
    }
}

void Machine::sub_behind(int due)
{
    sub_due_ = due;
    if (sub_done_ < due && !sub_lag_) {
        sub_lag_ = true;
        cpu_.map_ram(0xa000, 0x800, nullptr);
    }
}

void Machine::run_sub(int target)
{
    // Its VBLANK (line 240: the sprites' capture, the IRQ) where it gets
    // there, as when it caught up at the main CPU's VBLANK: nothing it does
    // reaches the main CPU before the next catch-up anyway.
    if (sub_vbl_ && target >= kSubVblank) {
        run_sub_to(kSubVblank);
        sub_vbl_ = false;
        capture_sprites();
        if (sub_irq_mask_) sub_.irq(true);
    }
    run_sub_to(target);
}

void Machine::run_sub_to(int target)
{
    while (sub_done_ < target) {
        if (idle_skip_) sub_done_ += sub_idle(target - sub_done_);
        sub_start_ = sub_done_;
        sub_done_ += sub_.run(target - sub_done_);
    }
}

// --- sprites ------------------------------------------------------------------------

// A slot changes while the beam is in lines 0-239: what it held is shown on
// the lines since it last changed. Writes in VBLANK (lines 240-255) are the
// next frame's from its start.
void Machine::sprite_write(int offset, uint8_t)
{
    const int slot = offset >> 2;
    int line = sub_now() * 2 / kCyclesPerLine;
    if (line >= kVblankLine) return;
    if (line <= slot_from_[slot]) return;
    if (entries_ < kMaxEntries) {
        const uint8_t* s = sub_ram_ + 0x40 + slot * 4;
        Sprite& e = pending_[entries_];
        e.x = s[0];
        e.top = int16_t(241 - s[3]);
        e.code = uint16_t(((s[2] & 0x20) << 3) | s[1]);
        e.color = s[2] & 0x0f;
        e.flags = uint8_t(((s[2] & 0x40) ? 0 : 1) | ((s[2] & 0x80) ? 2 : 0));
        e.from = slot_from_[slot];
        e.to = uint8_t(line);
        // Only if some of its rows are on the lines shown.
        const int lo = e.from > kFirstLine ? e.from : kFirstLine, hi = e.to;
        if (e.top < hi && e.top + 16 > lo) pending_slot_[entries_++] = uint8_t(slot);
    }
    slot_from_[slot] = uint8_t(line);
}

void Machine::capture_tiles()
{
    std::memcpy(state_.cram, vcram_, 0x400);
    std::memcpy(state_.vram, vcram_ + 0x400, 0x400);
}

void Machine::capture_sprites()
{
    VideoState& vs = state_;
    // Close every slot at the frame's last line.
    for (int slot = 0; slot < kMaxSprites; ++slot) {
        if (slot_from_[slot] >= kVblankLine || entries_ >= kMaxEntries) continue;
        const uint8_t* s = sub_ram_ + 0x40 + slot * 4;
        Sprite& e = pending_[entries_];
        e.x = s[0];
        e.top = int16_t(241 - s[3]);
        e.code = uint16_t(((s[2] & 0x20) << 3) | s[1]);
        e.color = s[2] & 0x0f;
        e.flags = uint8_t(((s[2] & 0x40) ? 0 : 1) | ((s[2] & 0x80) ? 2 : 0));
        e.from = slot_from_[slot];
        e.to = uint8_t(kVblankLine);
        const int lo = e.from > kFirstLine ? e.from : kFirstLine;
        if (e.top < kVblankLine && e.top + 16 > lo) pending_slot_[entries_++] = uint8_t(slot);
    }
    // Back to front: the highest slot first (slot 0 on top).
    {
        // (A counting sort: each slot's pieces keep their order.)
        uint16_t at[kMaxSprites + 1] = {};
        for (int i = 0; i < entries_; ++i) ++at[kMaxSprites - 1 - pending_slot_[i] + 1];
        for (int k = 1; k <= kMaxSprites; ++k) at[k] = uint16_t(at[k] + at[k - 1]);
        for (int i = 0; i < entries_; ++i) vs.spr[at[kMaxSprites - 1 - pending_slot_[i]]++] = pending_[i];
        vs.count = entries_;
    }
    entries_ = 0;
    std::memset(slot_from_, 0, sizeof slot_from_);
    // Bands of 8 pixels along the scanline (display lines).
    uint8_t n[32] = {};
    for (int i = 0; i < vs.count; ++i) {
        const int x = vs.spr[i].x;
        ++n[x >> 3];
        if ((x & 7) && x < 248) ++n[(x >> 3) + 1];
    }
    vs.band_at[0] = 0;
    for (int b = 0; b < 32; ++b) vs.band_at[b + 1] = uint16_t(vs.band_at[b] + n[b]);
    uint16_t at[32];
    for (int b = 0; b < 32; ++b) at[b] = vs.band_at[b];
    for (int i = 0; i < vs.count; ++i) {
        const int x = vs.spr[i].x;
        vs.band_spr[at[x >> 3]++] = uint8_t(i);
        if ((x & 7) && x < 248) vs.band_spr[at[(x >> 3) + 1]++] = uint8_t(i);
    }
}

// --- video ------------------------------------------------------------------------------

// Tiles: 16 bytes each, bit planes in the nibbles (bit 3 - x the high
// bit, bit 7 - x the low bit), pixels 4-7 8 bytes on. Sprites: 64 bytes,
// bank 1 16 bytes on; planes 0 / 1 as tiles, 2 / 3 the same 16 KB on;
// pixels 4-7 8 bytes on, lines 8-15 32 bytes on.
void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // y = pixel along the scanline; display x = 239 - line. Tile row tr
    // covers display x 239 - 8 tr - 7 .. 239 - 8 tr.
    const int tc = y >> 3, c = y & 7;
    uint32_t front = 0;                                 // bit tr: that tile row is in front of the sprites
    for (int tr = 2; tr < 30; ++tr) {
        const int t = tr * 32 + tc;
        const uint8_t attr = vs.cram[t];
        const int code = ((attr & 0x20) << 3) | vs.vram[t];
        uint32_t bits = tile_cols_[code * 8 + ((attr & 0x40) ? 7 - c : c)];
        const uint16_t* pens = tile_pen_ + (attr & 0x0f) * 4;
        if (attr & 0x10) front |= 1u << tr;
        uint16_t* o = out + (kLastLine - tr * 8 - 7);   // display x of the tile's line 7
        if (!bits) {
            const uint16_t p = pens[0];
            for (int k = 0; k < 8; ++k) o[k] = p;
        } else if (attr & 0x80) {                       // flip y: line 0 at the left
            for (int k = 0; k < 8; ++k, bits >>= 2) o[k] = pens[bits & 3];
        } else {
            for (int k = 7; k >= 0; --k, bits >>= 2) o[k] = pens[bits & 3];
        }
    }

    const int band = y >> 3;
    const uint8_t* sprites = roms_.sprites;
    for (int k = vs.band_at[band]; k < vs.band_at[band + 1]; ++k) {
        const Sprite& s = vs.spr[vs.band_spr[k]];
        const int col = y - s.x;
        if (col < 0 || col > 7) continue;
        const int cc = (s.flags & 1) ? 7 - col : col;
        const uint8_t* g = sprites + (s.code >> 1) * 64 + (s.code & 1) * 16 + ((cc & 4) << 1);
        const int b1 = 3 - (cc & 3), b0 = 7 - (cc & 3);
        const uint16_t* pens = sprite_pen_ + s.color * 16;
        const int lo = s.from > kFirstLine ? s.from : kFirstLine;
        const int hi = s.to;
        for (int j = 0; j < 16; ++j) {
            const int line = s.top + j;
            if (line < lo || line >= hi) continue;
            if ((front >> (line >> 3)) & 1) continue;
            const int jj = (s.flags & 2) ? 15 - j : j;
            const int at = jj < 8 ? jj : 32 + jj - 8;
            const uint8_t lo_b = g[at], hi_b = g[0x4000 + at];
            const int pix = (((hi_b >> b1) & 1) << 3) | (((hi_b >> b0) & 1) << 2) | (((lo_b >> b1) & 1) << 1) |
                            ((lo_b >> b0) & 1);
            if (pix) out[kLastLine - line] = pens[pix];
        }
    }
}

// --- sound board ------------------------------------------------------------------------

uint8_t Machine::snd_mem_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xe000) == 0x8000) return m.latch_;
    return 0xff;
}

void Machine::snd_mem_write(void*, uint16_t, uint8_t) {}

uint8_t Machine::snd_port_read(void* ctx, uint16_t port)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const int p = port & 0xff;
    if ((p & 3) != 1 || p > 0x11) return 0xff;
    const int chip = p >> 2;
    if (chip != 2) return m.ay_[chip].read_data();
    const int at = m.sound_now();
    m.ay_[2].set_port_in(0, m.timer_at(at));
    const uint8_t v = m.ay_[2].read_data();
    // In a timer wait (RST 10 called from one of the loops): stop after this
    // IN, so sound_to() can skip the passes to come.
    if (m.snd_loop_ && m.idle_skip_ && !m.snd_irq_ && m.snd_.pc.w == 0x0014) {
        const uint16_t ret = uint16_t(m.snd_.rd(m.snd_.sp.w) | (m.snd_.rd(uint16_t(m.snd_.sp.w + 1)) << 8));
        if (ret == 0x0101 || ret == 0x0108) {
            m.wait_read_ = true;
            m.wait_stay_ = ret == 0x0101 ? 0x04 : 0x00;
            m.wait_at_ = at;
            m.snd_.stop();
        }
    }
    return v;
}

void Machine::snd_port_write(void* ctx, uint16_t port, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const int p = port & 0xff;
    if (p <= 0x12) {
        const int chip = p >> 2;
        switch (p & 3) {
        case 0: m.ay_[chip].write_address(v); break;
        case 2: {
            const int at = m.sound_now();
            bool flush = m.needs_flush(at);
            if (chip < 2 && !flush) {
                // A new filter applies to the samples from here on: the ones
                // so far are finished with the old one first.
                const ay8910::Chip& a = m.ay_[chip];
                const int r = a.selected();
                const uint8_t bits = r == 15 ? ((a.reg(7) & 0x80) ? v : 0xff)
                                   : r == 7 ? ((v & 0x80) ? a.reg(15) : 0xff) : m.filter_bits_[chip];
                flush = bits != m.filter_bits_[chip];
            }
            if (flush) m.defer_sound(chip, v, at);      // the finishing by sound_to()
            else m.ay_write(chip, v, at);
            break;
        }
        default: break;
        }
    } else if (p == 0x14) {
        m.defer_sound(kSndMcuIrq, v, m.sound_now());     // the i8039 brought up to date by sound_to()
    } else if (p == 0x18) {
        m.defer_sound(kSndMcuCmd, v, m.sound_now());
    }
}

void Machine::ay_write(int chip, uint8_t v, int at)
{
    chip_to(chip, at);
    ay_[chip].write_data(v);
    if (chip < 2) set_filter(chip, ay_[chip].port_out(1));
}

// A sound CPU write that needs the i8039 or the samples brought up to date
// first: done once its instruction ends (the CPU stops), by sound_to() -
// the same, but not nested in the CPU's run.
void Machine::defer_sound(int what, uint8_t v, int at)
{
    if (snd_defer_) apply_sound();                     // (never: one write an instruction)
    snd_defer_ = true;
    snd_what_ = what;
    snd_v_ = v;
    snd_at_ = at;
    snd_.stop();
}

void Machine::apply_sound()
{
    snd_defer_ = false;
    if (snd_what_ == kSndMcuIrq) {
        mcu_to(snd_at_);
        mcu_.irq(true);
    } else if (snd_what_ == kSndMcuCmd) {
        mcu_to(snd_at_);
        latch2_ = snd_v_;
    } else {
        sync_audio(snd_at_);
        ay_write(snd_what_, snd_v_, snd_at_);
    }
}

uint8_t Machine::mcu_ext_read(void* ctx, uint8_t) { return static_cast<Machine*>(ctx)->latch2_; }

uint8_t Machine::mcu_port_read(void*, uint8_t) { return 0xff; }

void Machine::mcu_port_write(void* ctx, uint8_t port, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (port == i8035::kP1) {
        if (v != m.dac_out_) {
            m.dac_out_ = v;
            const int at = int(uint32_t(m.mcu_now()) * uint32_t(m.snd_frame_) / kI8039PerFrame);
            if (m.dac_qn_ == kDacQueue) {           // full: the samples so far take theirs
                m.audio_to(at);
                m.dac_to(at);
            }
            m.dac_q_[m.dac_qn_++] = {at, v};
            // The effects wait between DAC writes in DJNZ loops: stop, so
            // mcu_to() can skip the wait.
            if (m.idle_skip_ && djnz_self(m.mcu_)) m.mcu_.stop();
        }
    } else if (port == i8035::kP2) {
        m.mcu_.irq(false);
    }
}

void Machine::sound_to(int main_cycle)
{
    if (main_cycle > kCyclesPerFrame) main_cycle = kCyclesPerFrame;
    // (32 bits: 50,688 x 59,063 < 2^32; no 64-bit division on the boards.)
    const int target = int(uint32_t(main_cycle) * uint32_t(snd_frame_) / uint32_t(kCyclesPerFrame));
    while (snd_done_ < target) {
        if (snd_irq_) {
            // Held until the CPU takes it (IM 1): an instruction at a time,
            // so it drops at once.
            const bool ready = snd_.iff1 != 0;
            snd_done_ += snd_.run(1);
            if (snd_defer_) apply_sound();
            if (ready && !snd_.iff1) {
                snd_irq_ = false;
                snd_.irq(false);
            }
            continue;
        }
        snd_done_ += snd_.run(target - snd_done_);
        if (snd_defer_) apply_sound();
        if (wait_read_) skip_timer_wait(target);
    }
}

void Machine::flush_cmds()
{
    for (int i = 0; i < cmd_n_; ++i) {
        sound_to(cmds_[i].at);
        if (!cmds_[i].command) {
            if (!snd_irq_) {
                snd_irq_ = true;
                snd_.irq(true, 0xff);
            }
        } else {
            latch_ = cmds_[i].v;
        }
    }
    cmd_n_ = 0;
}

void Machine::mcu_to(int snd_cycle)
{
    const int target = int(uint32_t(snd_cycle) * kI8039PerFrame / uint32_t(snd_frame_ ? snd_frame_ : 1));
    // In slices, so a CPU gone idle is found soon (where a run ends changes
    // nothing: its inputs don't change meanwhile).
    while (mcu_done_ < target) {
        if (idle_skip_) mcu_done_ += mcu_idle(target - mcu_done_);
        const int n = target - mcu_done_;
        if (n > 0) mcu_done_ += mcu_.run(n < 256 ? n : 256);
    }
}

void Machine::sync_audio(int snd_cycle)
{
    mcu_to(snd_cycle);
    audio_to(snd_cycle);
}

// Before an AY runs to snd_cycle: is there room for its samples in rows_?
// Else the samples so far are finished first (the i8039 brought up to date
// for their DAC). (Past the frame's end, or no samples asked for:
// everything at once, as the i8039 then runs past the frame's end too.)
bool Machine::needs_flush(int snd_cycle) const
{
    const int last = audio_done_ + kRows - 1;
    return !audio_n_ || snd_cycle >= snd_frame_ || (last < audio_n_ && sample_end(last) <= snd_cycle);
}

// --- idle loops (see the top) -------------------------------------------------------------

int Machine::main_idle(int room)
{
    if (!main_loop_ || nmi_now_ || uint16_t(cpu_.pc.w - 0x05f4) > 0x0601 - 0x05f4) return 0;
    int at = -1;                                        // the instruction it is at
    for (int k = 0; k < 8; ++k)
        if (cpu_.pc.w == kMainLoopPcs[k]) at = k;
    if (at < 0) return 0;
    const uint8_t hi = cpu_.rd(0x5865), lo = cpu_.rd(0x9433);
    const uint16_t entry = uint16_t((hi << 8) | lo);
    if ((entry & 0xf000) != 0x9000) return 0;
    const uint8_t e = cpu_.rd(entry);
    if (!(e & 0x80)) return 0;                          // leaves the loop
    // Whole passes, then on instruction by instruction to the first one
    // ending at or past the slice's end, as run() would stop - at least a
    // whole pass, so every register the loop sets is as it leaves them.
    const int passes = room / kMainPass;
    if (passes <= 0) return 0;
    int t = passes * kMainPass, n = passes * kMainFetches;
    while (t < room) {
        t += kMainCycles[at];
        at = (at + 1) & 7;
        ++n;
    }
    cpu_.r = uint8_t(cpu_.r + n);
    const uint8_t rot = uint8_t((e << 1) | (e >> 7));
    const uint16_t pc = kMainLoopPcs[at];
    cpu_.pc.w = pc;
    cpu_.hl.b.h = hi;
    cpu_.hl.b.l = lo;
    cpu_.af.b.h = pc == 0x05fa || pc == 0x05fb ? hi : pc == 0x05fe || pc == 0x05ff ? lo : pc == 0x0600 ? e : rot;
    cpu_.af.b.l = uint8_t((cpu_.af.b.l & (z80::SF | z80::ZF | z80::PF)) | (rot & (z80::XF | z80::YF | z80::CF)));
    skipped_[0] += uint64_t(t);
    return t;
}

int Machine::sub_idle(int room)
{
    if (!sub_loop_ || sub_.pc != kSubLoopAt || sub_.in_cwai() || sub_.in_sync()) return 0;
    if ((sub_.irq_line() && !(sub_.cc & m6809::CC_I)) || (sub_.firq_line() && !(sub_.cc & m6809::CC_F))) return 0;
    const int passes = (room - 1) / 3;
    skipped_[1] += uint64_t(passes) * 3;
    return passes * 3;
}

void Machine::skip_timer_wait(int target)
{
    wait_read_ = false;
    if ((snd_.af.b.h & 0x04) != wait_stay_) return;     // this pass leaves the loop
    // The passes after this one whose reads (69 cycles apart) still see
    // the same bit 2: up to the timer's next change of it.
    // (The timer's place in its 10 x 1024-cycle round: 32 bits.)
    const uint32_t read = (snd_base10_ + uint32_t(wait_at_)) % 10240;
    uint32_t step = read / 1024;
    do ++step; while (((kTimer[step % 10] ^ wait_stay_) & 0x04) == 0);
    const int left = int(step * 1024 - read);           // cycles until the bit changes (> 0)
    int passes = (left - 1) / kSndPass;                 // later reads still before the change
    // The pass after them reads the change and leaves the loop: if that
    // ends before the target, it is done here too (RET AND JR LD RST OUT
    // to the read, then IN RET AND and the JR that falls through: 93 cycles,
    // 10 fetches), the CPU left after the loop with A = the read AND 04.
    const int exit_at = snd_done_ + passes * kSndPass + kSndExit;
    if (exit_at <= target) {
        skipped_[2] += uint64_t(exit_at - snd_done_);
        snd_done_ = exit_at;
        snd_.r = uint8_t(snd_.r + passes * kSndFetches + kSndExitFetches);
        ay_[2].set_port_in(0, kTimer[((read + uint32_t(passes + 1) * kSndPass) % 10240) / 1024]);
        const uint8_t a = uint8_t(ay_[2].read_data() & 0x04);
        snd_.af.b.h = a;
        snd_.af.b.l = a ? 0x10 : 0x54;                  // AND's flags: H, or Z H P
        snd_.pc.w = wait_stay_ ? 0x0105 : 0x010c;       // after JR NZ / JR Z
        snd_.sp.w = uint16_t(snd_.sp.w + 2);            // RST 10's return address taken by its RET
        return;
    }
    const int room = (target - snd_done_ - 1) / kSndPass;
    if (passes > room) passes = room;
    if (passes <= 0) return;
    snd_done_ += passes * kSndPass;
    skipped_[2] += uint64_t(passes) * kSndPass;
    snd_.r = uint8_t(snd_.r + passes * kSndFetches);
    // A = the last skipped pass's read, F = its AND 04's: H, or Z H P.
    ay_[2].set_port_in(0, kTimer[((read + uint32_t(passes) * kSndPass) % 10240) / 1024]);
    snd_.af.b.h = ay_[2].read_data();
    snd_.af.b.l = wait_stay_ ? 0x10 : 0x54;
}

int Machine::mcu_idle(int room)
{
    i8035::Cpu& c = mcu_;
    if (c.count_mode == 2) return 0;
    if (!c.in_irq && ((c.irq_line() && c.xirq_enabled) || (c.timer_overflow && c.tirq_enabled))) return 0;
    int pass, passes;
    uint8_t* counter = nullptr;
    if (mcu_loop_ && (c.pc == 0x19 || c.pc == 0x1a) && (!c.a11 || c.in_irq)) {
        pass = kMcuPass;
        passes = (room - 1) / pass;
    } else if (djnz_self(c)) {
        // DJNZ Rr to itself, 2 cycles a pass: all but the last (which falls
        // through) skipped.
        counter = &c.reg(c.rom[c.pc & c.rom_mask] & 7);
        pass = 2;
        passes = (room - 1) / pass;
        const int left = (*counter ? *counter : 256) - 1;
        if (passes > left) passes = left;
    } else {
        return 0;
    }
    if (passes <= 0) return 0;
    if (c.count_mode == 1) {
        // The timer counts the skipped cycles; an overflow that would
        // interrupt is left for the CPU to run into.
        int ticks = (c.prescaler + passes * pass) >> 5;
        if (c.tirq_enabled && c.timer + ticks > 0xff) {
            passes = ((0x100 - c.timer) * 32 - c.prescaler - 1) / pass - 1;   // ends before it
            if (passes <= 0) return 0;
            ticks = (c.prescaler + passes * pass) >> 5;
        }
        if (c.timer + ticks > 0xff) c.timer_flag = true;
        c.timer = uint8_t(c.timer + ticks);
        c.prescaler = uint8_t((c.prescaler + passes * pass) & 31);
    }
    if (counter) *counter = uint8_t(*counter - passes);
    skipped_[3] += uint64_t(passes) * pass;
    return passes * pass;
}

// --- audio ---------------------------------------------------------------------------------

void Machine::set_filter(int chip, uint8_t bits)
{
    filter_bits_[chip] = bits;
    for (int ch = 0; ch < 3; ++ch) lp_a_[chip * 3 + ch] = lp_tab_[(bits >> (2 * ch)) & 3];
}

int16_t Machine::mix_sample(const uint32_t* acc, uint32_t dac_acc, uint32_t clocks)
{
    // A sample is 74 or 75 AY clocks: 1 / clocks kept for the last two.
    int slot = clocks == scale_clocks_[0] ? 0 : clocks == scale_clocks_[1] ? 1 : -1;
    if (slot < 0) {
        slot = scale_next_;
        scale_next_ ^= 1;
        scale_clocks_[slot] = clocks;
        scale_[slot] = clocks ? 1.0f / float(clocks) : 0;
    }
    const float scale = scale_[slot];
    float ch[6];
    for (int c = 0; c < 6; ++c) {
        const float x = float(acc[c]) * scale;
        lp_y_[c] = lp_a_[c] > 0 ? flush_tiny(lp_y_[c] + lp_a_[c] * (x - lp_y_[c])) : x;
        ch[c] = lp_y_[c];
    }
    const float ay34 = float(acc[6] + acc[7] + acc[8] + acc[9] + acc[10] + acc[11]) * scale;
    const float ay5 = float(acc[12] + acc[13] + acc[14]) * scale;
    const float sum = kWAy1 * (ch[0] + ch[1] + ch[2]) + kWAy34 * ay34 + kWAy2 * (ch[3] + ch[4] + ch[5]) +
                      kWAy5 * ay5 + kWDac * float(dac_acc) * scale * 0.5f;
    out_y_ = flush_tiny(out_y_ + out_a_ * (sum - out_y_));
    const float out = flush_tiny(dc_r_ * (dc_y_ + out_y_ - dc_x_));
    dc_x_ = out_y_;
    dc_y_ = out;
    // A soft knee above 16,000, so a pile-up of sounds never clips.
    float s = out * kOutGain;
    if (s > 16000.0f) s = 16000.0f + (s - 16000.0f) / (1.0f + (s - 16000.0f) / 5500.0f);
    if (s < -16000.0f) s = -16000.0f + (s + 16000.0f) / (1.0f - (s + 16000.0f) / 5500.0f);
    return int16_t(s);
}

// Sound cycles run the AYs at half the rate (the CPU's 3.58 MHz, the AYs'
// 1.79 MHz from the same crystal). An AY is brought up to date only for its
// own writes and when samples are finished (audio_to): each output sample's
// sums go to its row of rows_ (row r = sample audio_done_ + r), so the
// others, the i8039 and the mix wait until kRows samples have piled up -
// with the same runs (split at every sample's end and at the AY's own
// writes), sums and mix as finishing every sample at once.
void Machine::chip_to(int chip, int snd_cycle)
{
    int from = ay_clock_[chip];
    if (snd_cycle <= from) return;
    const uint32_t b = uint32_t(snd_base_ & 1);
    if (!audio_n_) {                                    // no samples asked for: one run
        const uint32_t clocks = ((b + uint32_t(snd_cycle)) >> 1) - ((b + uint32_t(from)) >> 1);
        if (clocks) ay_[chip].run(clocks, rows_[0] + 3 * chip);
        ay_clock_[chip] = snd_cycle;
        return;
    }
    int j = ay_sample_[chip];                           // the sample `from` is in
    if (j >= audio_n_) {                                // past the frame's last sample: not heard
        const uint32_t clocks = ((b + uint32_t(snd_cycle)) >> 1) - ((b + uint32_t(from)) >> 1);
        uint32_t lost[3] = {};
        if (clocks) ay_[chip].run(clocks, lost);
        ay_clock_[chip] = snd_cycle;
        return;
    }
    const int row = j - audio_done_;
    uint32_t clocks[kRows];
    int n = 0;
    for (;;) {
        const int e = sample_end(j);
        const int to = e < snd_cycle ? e : snd_cycle;
        clocks[n++] = ((b + uint32_t(to)) >> 1) - ((b + uint32_t(from)) >> 1);
        from = to;
        if (to < e) break;
        ++j;
        if (to == snd_cycle) break;
        if (j >= audio_n_) {                            // and on past the frame's end
            ay_[chip].run_pieces(clocks, n, rows_[row] + 3 * chip, 16);
            n = 0;
            const uint32_t c = ((b + uint32_t(snd_cycle)) >> 1) - ((b + uint32_t(from)) >> 1);
            uint32_t lost[3] = {};
            if (c) ay_[chip].run(c, lost);
            break;
        }
    }
    if (n) ay_[chip].run_pieces(clocks, n, rows_[row] + 3 * chip, 16);
    ay_clock_[chip] = snd_cycle;
    ay_sample_[chip] = j;
}

// The DAC up to snd_cycle: its queued changes in order, into the samples'
// rows.
void Machine::dac_to(int snd_cycle)
{
    if (snd_cycle > snd_frame_) snd_cycle = snd_frame_;
    int q = 0;
    while (dac_clock_ < snd_cycle) {
        const int e = audio_n_ ? sample_end(dac_sample_) : snd_frame_;
        const int to = e < snd_cycle ? e : snd_cycle;
        uint32_t a = 0;
        for (; q < dac_qn_ && dac_q_[q].at < to; ++q) {
            const int at = dac_q_[q].at > dac_clock_ ? dac_q_[q].at : dac_clock_;
            a += uint32_t(dac_) * uint32_t(at - dac_clock_);
            dac_clock_ = at;
            dac_ = dac_q_[q].v;
        }
        a += uint32_t(dac_) * uint32_t(to - dac_clock_);
        dac_clock_ = to;
        rows_[audio_n_ ? dac_sample_ - audio_done_ : 0][15] += a;
        if (to == e && audio_n_) ++dac_sample_;
    }
    // Changes before where the DAC already was (it ran there earlier): just their values.
    for (; q < dac_qn_ && dac_q_[q].at < snd_cycle; ++q) dac_ = dac_q_[q].v;
    if (q) {
        for (int k = q; k < dac_qn_; ++k) dac_q_[k - q] = dac_q_[k];
        dac_qn_ -= q;
    }
}

// Finishes the samples that end by snd_cycle, kRows at a time.
void Machine::audio_to(int snd_cycle)
{
    while (audio_done_ < audio_n_ && sample_end(audio_done_) <= snd_cycle) {
        // The samples' ends and AY clocks.
        int ends[kRows];
        uint32_t clocks[kRows];
        const uint32_t b = uint32_t(snd_base_ & 1);
        int n = 0;
        // (Sample j ends at floor((j + 1) x snd_frame_ / audio_n_): stepped
        // along without a division a sample.)
        const int q = snd_frame_ / audio_n_, r = snd_frame_ % audio_n_;
        int e = sample_end(audio_done_);
        int rem = (audio_done_ + 1) * r % audio_n_;
        for (int start = audio_done_ ? sample_end(audio_done_ - 1) : 0;;) {
            if (n) {
                e += q;
                rem += r;
                if (rem >= audio_n_) {
                    rem -= audio_n_;
                    ++e;
                }
            }
            if (e > snd_cycle) break;
            ends[n] = e;
            clocks[n] = ((b + uint32_t(e)) >> 1) - ((b + uint32_t(start)) >> 1);
            start = e;
            if (++n == kRows || audio_done_ + n == audio_n_) break;
        }
        const int end = ends[n - 1];
        for (int k = 0; k < 5; ++k) {
            const int from = ay_clock_[k];
            if (end <= from) continue;
            if (ay_sample_[k] != audio_done_) {         // written in a later sample
                chip_to(k, end);
                continue;
            }
            // Its piece of the first sample, then whole samples.
            uint32_t first = ((b + uint32_t(ends[0])) >> 1) - ((b + uint32_t(from)) >> 1);
            ay_[k].run_pieces(&first, 1, rows_[0] + 3 * k, 16);
            if (n > 1) ay_[k].run_pieces(clocks + 1, n - 1, rows_[1] + 3 * k, 16);
            ay_clock_[k] = end;
            ay_sample_[k] = audio_done_ + n;
        }
        if (dac_sample_ == audio_done_ && dac_clock_ < end) {
            // The DAC as dac_to() does it, with the ends at hand.
            int q = 0;
            for (int i = 0; i < n; ++i) {
                const int e = ends[i];
                uint32_t a = 0;
                for (; q < dac_qn_ && dac_q_[q].at < e; ++q) {
                    const int at = dac_q_[q].at > dac_clock_ ? dac_q_[q].at : dac_clock_;
                    a += uint32_t(dac_) * uint32_t(at - dac_clock_);
                    dac_clock_ = at;
                    dac_ = dac_q_[q].v;
                }
                a += uint32_t(dac_) * uint32_t(e - dac_clock_);
                dac_clock_ = e;
                rows_[i][15] += a;
            }
            dac_sample_ = audio_done_ + n;
            if (q) {
                for (int k = q; k < dac_qn_; ++k) dac_q_[k - q] = dac_q_[k];
                dac_qn_ -= q;
            }
        } else {
            dac_to(end);
        }
        for (int i = 0; i < n; ++i) {
            const int16_t s = mix_sample(rows_[i], rows_[i][15], clocks[i]);
            if (audio_) audio_[audio_done_ + i] = s;
        }
        // The rows of samples still going (AYs written later) move down:
        // only rows up to the furthest any AY or the DAC has reached can
        // hold anything (the rest are 0 already).
        int hi = dac_sample_;
        for (int k = 0; k < 5; ++k) hi = ay_sample_[k] > hi ? ay_sample_[k] : hi;
        hi = hi - audio_done_ + 1;
        if (hi > kRows) hi = kRows;
        if (hi > n) {
            std::memmove(rows_[0], rows_[n], sizeof rows_[0] * (hi - n));
            std::memset(rows_[hi - n], 0, sizeof rows_[0] * n);
        } else {
            std::memset(rows_[0], 0, sizeof rows_[0] * hi);
        }
        audio_done_ += n;
    }
}

// --- frame ---------------------------------------------------------------------------------

// The main CPU's side of VBLANK; the sub CPU's comes when it gets to line
// 240 (run_sub).
void Machine::vblank()
{
    capture_tiles();
    if (nmi_mask_ && !nmi_line_) {
        nmi_line_ = true;
        nmi_now_ = true;
        cpu_.nmi();
    }
    sub_vbl_ = true;
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // The sound board's cycles this frame: 50688 x (14.318 MHz / 4) / 3.072 MHz.
    // (The remainders stepped along: no 64-bit division a frame - a library
    // call on the boards.)
    constexpr uint64_t kSndPerFrame = uint64_t(kCyclesPerFrame) * kSoundXtal, kSndDiv = uint64_t(kCpuClock) * 4;
    snd_frame_ = int(kSndPerFrame / kSndDiv);
    snd_rem_ += uint32_t(kSndPerFrame % kSndDiv);
    if (snd_rem_ >= kSndDiv) {
        snd_rem_ -= uint32_t(kSndDiv);
        ++snd_frame_;
    }

    int n = int(audio_q_);
    audio_rem_ += audio_r_;
    if (audio_rem_ >= kCpuClock) {
        audio_rem_ -= kCpuClock;
        ++n;
    }
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = n > 0 ? n : 0;
    audio_done_ = 0;
    for (int& c : ay_clock_) c = 0;
    for (int& c : ay_sample_) c = 0;
    dac_clock_ = 0;
    dac_sample_ = 0;
    std::memset(rows_, 0, sizeof rows_);

    // The main and sub CPUs take turns (they share RAM).
    static_assert(kVblankLine % kSliceLines == 0 && kLinesPerFrame % kSliceLines == 0, "slices");
    int done = carry_;
    for (int line = 0; line < kLinesPerFrame; line += kSliceLines) {
        // The sub CPU's turns so far are due before this slice (sub_catch_up).
        sub_behind(line * kCyclesPerLine / 2);
        if (line == kVblankLine) vblank();
        const int end = (line + kSliceLines) * kCyclesPerLine;
        if (idle_skip_) done += main_idle(end - done);
        nmi_now_ = false;
        while (done < end) {
            slice_start_ = done;
            done += cpu_.run(end - done);
        }
    }
    sub_behind(kCyclesPerFrame / 2);
    sub_catch_up();
    carry_ = done - kCyclesPerFrame;
    sub_done_ -= kCyclesPerFrame / 2;
    slice_start_ = 0;

    flush_cmds();
    sound_to(kCyclesPerFrame);
    sync_audio(snd_frame_);
    for (int k = 0; k < 5; ++k) chip_to(k, snd_frame_);    // (no samples asked for)
    dac_to(snd_frame_);
    for (int i = 0; i < dac_qn_; ++i) dac_q_[i].at -= snd_frame_;   // written past the frame's end
    ++frame_;

    // The sound CPU's and the i8039's overrun carry into the next frame.
    snd_base_ += uint64_t(snd_frame_);
    snd_base10_ = (snd_base10_ + uint32_t(snd_frame_)) % 10240;
    snd_done_ -= snd_frame_;
    mcu_done_ -= kI8039PerFrame;
    const int count = audio_ ? audio_n_ : 0;
    audio_ = nullptr;
    return count;
}

} // namespace gyruss
