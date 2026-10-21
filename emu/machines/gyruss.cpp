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
//   whole passes are skipped (time, R, and A H L F as a pass leaves them at
//   that point), the CPU runs the rest.
// - Sub CPU, F0B6: BRA * (waits for the VBLANK IRQ), 3 cycles: skipped to
//   the slice's end while no interrupt can be taken.
// - Sound CPU, 00FE / 0105: LD A,0E / RST 10 (OUT (08),A / IN A,(09) / RET)
//   / AND 04 / JR NZ (JR Z) - waits for the timer's bit 2 to clear, then to
//   set: 69 cycles and 7 fetches a pass. The IN stops the CPU; the passes
//   whose reads would see the same bit 2 are skipped (time, R, A = the last
//   of them's read, F = its AND's). Never while an IRQ waits.
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

// The idle loops (see above).
constexpr uint8_t kMainLoop[] = {0xc3, 0xf7, 0x05, 0x3a, 0x65, 0x58, 0x67, 0x3a, 0x33, 0x94, 0x6f, 0x7e, 0x07,
                                 0xda, 0xf4, 0x05};                     // at 05F4
constexpr uint16_t kMainLoopAt = 0x05f4;
constexpr int kMainPass = 65, kMainFetches = 8;
// The addresses of the loop's instructions.
constexpr uint16_t kMainLoopPcs[] = {0x05f4, 0x05f7, 0x05fa, 0x05fb, 0x05fe, 0x05ff, 0x0600, 0x0601};
constexpr uint16_t kSubLoopAt = 0xf0b6;                                 // BRA * (opcode scrambled)
constexpr uint8_t kSndLoop[] = {0x3e, 0x0e, 0xd7, 0xe6, 0x04, 0x20, 0xf9,
                                0x3e, 0x0e, 0xd7, 0xe6, 0x04, 0x28, 0xf9};   // at 00FE
constexpr uint8_t kSndRst[] = {0xd3, 0x08, 0xdb, 0x09, 0xc9};          // at 0010
constexpr int kSndPass = 69, kSndFetches = 7;
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

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

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
    const Machine& m = *static_cast<Machine*>(ctx);
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
    if ((addr & 0xfe00) != 0xc000) return;
    switch (addr & 0x180) {
    case 0x080:                                         // C080: the sound CPU's IRQ
        m.sound_to(m.now());
        if (!m.snd_irq_) {
            m.snd_irq_ = true;
            m.snd_.irq(true, 0xff);
        }
        break;
    case 0x100:                                         // C100: the sound command
        m.sound_to(m.now());
        m.latch_ = v;
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

void Machine::run_sub(int target)
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

void Machine::capture()
{
    VideoState& vs = state_;
    std::memcpy(vs.cram, vcram_, 0x400);
    std::memcpy(vs.vram, vcram_ + 0x400, 0x400);
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
    vs.count = 0;
    for (int slot = kMaxSprites - 1; slot >= 0; --slot)
        for (int i = 0; i < entries_; ++i)
            if (pending_slot_[i] == slot) vs.spr[vs.count++] = pending_[i];
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
    m.ay_[2].set_port_in(0, timer_bits(m.snd_base_ + uint64_t(at)));
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
            m.sync_audio(at);
            m.chip_to(chip, at);
            m.ay_[chip].write_data(v);
            if (chip < 2) m.set_filter(chip, m.ay_[chip].port_out(1));
            break;
        }
        default: break;
        }
    } else if (p == 0x14) {
        m.mcu_to(m.sound_now());
        m.mcu_.irq(true);
    } else if (p == 0x18) {
        m.mcu_to(m.sound_now());
        m.latch2_ = v;
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
            const int at = int(int64_t(m.mcu_now()) * m.snd_frame_ / kI8039PerFrame);
            if (m.dac_qn_ == kDacQueue) {           // full: the samples so far take theirs
                m.audio_to(at);
                m.dac_to(at < m.sample_end_ ? at : m.sample_end_);
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
    const int target = int(int64_t(main_cycle) * snd_frame_ / kCyclesPerFrame);
    while (snd_done_ < target) {
        if (snd_irq_) {
            // Held until the CPU takes it (IM 1): an instruction at a time,
            // so it drops at once.
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

void Machine::mcu_to(int snd_cycle)
{
    const int target = int(int64_t(snd_cycle) * kI8039PerFrame / (snd_frame_ ? snd_frame_ : 1));
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

// --- idle loops (see the top) -------------------------------------------------------------

int Machine::main_idle(int room)
{
    if (!main_loop_ || nmi_now_) return 0;
    bool in_loop = false;
    for (uint16_t pc : kMainLoopPcs) in_loop |= cpu_.pc.w == pc;
    if (!in_loop) return 0;
    const uint8_t hi = cpu_.rd(0x5865), lo = cpu_.rd(0x9433);
    const uint16_t entry = uint16_t((hi << 8) | lo);
    if ((entry & 0xf000) != 0x9000) return 0;
    const uint8_t e = cpu_.rd(entry);
    if (!(e & 0x80)) return 0;                          // leaves the loop
    const int passes = (room - 1) / kMainPass;          // the CPU runs the rest
    if (passes <= 0) return 0;
    cpu_.r = uint8_t(cpu_.r + passes * kMainFetches);
    // The registers as a pass leaves them at this point of the loop.
    const uint8_t rot = uint8_t((e << 1) | (e >> 7));
    const uint16_t pc = cpu_.pc.w;
    cpu_.hl.b.h = hi;
    cpu_.hl.b.l = lo;
    cpu_.af.b.h = pc == 0x05fa || pc == 0x05fb ? hi : pc == 0x05fe || pc == 0x05ff ? lo : pc == 0x0600 ? e : rot;
    cpu_.af.b.l = uint8_t((cpu_.af.b.l & (z80::SF | z80::ZF | z80::PF)) | (rot & (z80::XF | z80::YF | z80::CF)));
    skipped_[0] += uint64_t(passes) * kMainPass;
    return passes * kMainPass;
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
    const uint64_t read = snd_base_ + uint64_t(wait_at_);
    uint64_t step = read / 1024;
    do ++step; while (((kTimer[step % 10] ^ wait_stay_) & 0x04) == 0);
    const int64_t left = int64_t(step * 1024 - read);    // cycles until the bit changes (> 0)
    int64_t passes = (left - 1) / kSndPass;             // later reads still before the change
    const int64_t room = (int64_t(target) - snd_done_ - 1) / kSndPass;
    if (passes > room) passes = room;
    if (passes <= 0) return;
    snd_done_ += int(passes * kSndPass);
    skipped_[2] += uint64_t(passes) * kSndPass;
    snd_.r = uint8_t(snd_.r + passes * kSndFetches);
    // A = the last skipped pass's read, F = its AND 04's: H, or Z H P.
    ay_[2].set_port_in(0, timer_bits(read + uint64_t(passes) * kSndPass));
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
    for (int ch = 0; ch < 3; ++ch) {
        const int b = (bits >> (2 * ch)) & 3;
        const float cap = ((b & 1) ? kCapBit0 : 0) + ((b & 2) ? kCapBit1 : 0);
        lp_a_[chip * 3 + ch] = cap > 0 ? 1.0f - std::exp(-1.0f / (kFilterR * cap * float(audio_rate_))) : 0;
    }
}

int16_t Machine::mix_sample(uint32_t clocks)
{
    const float scale = clocks ? 1.0f / float(clocks) : 0;
    float ch[6];
    for (int c = 0; c < 6; ++c) {
        const float x = float(acc_[c]) * scale;
        lp_y_[c] = lp_a_[c] > 0 ? lp_y_[c] + lp_a_[c] * (x - lp_y_[c]) : x;
        ch[c] = lp_y_[c];
    }
    const float ay34 = float(acc_[6] + acc_[7] + acc_[8] + acc_[9] + acc_[10] + acc_[11]) * scale;
    const float ay5 = float(acc_[12] + acc_[13] + acc_[14]) * scale;
    const float sum = kWAy1 * (ch[0] + ch[1] + ch[2]) + kWAy34 * ay34 + kWAy2 * (ch[3] + ch[4] + ch[5]) +
                      kWAy5 * ay5 + kWDac * float(dac_acc_) * scale * 0.5f;
    out_y_ += out_a_ * (sum - out_y_);
    const float out = dc_r_ * (dc_y_ + out_y_ - dc_x_);
    dc_x_ = out_y_;
    dc_y_ = out;
    // A soft knee above 16,000, so a pile-up of sounds never clips.
    float s = out * kOutGain;
    if (s > 16000.0f) s = 16000.0f + (s - 16000.0f) / (1.0f + (s - 16000.0f) / 5500.0f);
    if (s < -16000.0f) s = -16000.0f + (s + 16000.0f) / (1.0f - (s + 16000.0f) / 5500.0f);
    return int16_t(s);
}

// Sound cycles run the AYs at half the rate (the CPU's 3.58 MHz, the AYs'
// 1.79 MHz from the same crystal). Each AY is brought up to date only for
// its own writes and at the end of each output sample.
void Machine::chip_to(int chip, int snd_cycle)
{
    const int from = ay_clock_[chip];
    if (snd_cycle <= from) return;
    const uint32_t b = uint32_t(snd_base_ & 1);
    const uint32_t clocks = ((b + uint32_t(snd_cycle)) >> 1) - ((b + uint32_t(from)) >> 1);
    if (clocks) ay_[chip].run(clocks, acc_ + 3 * chip);
    ay_clock_[chip] = snd_cycle;
}

void Machine::dac_to(int snd_cycle)
{
    int i = 0;
    for (; i < dac_qn_ && dac_q_[i].at < snd_cycle; ++i) {
        const int at = dac_q_[i].at > dac_clock_ ? dac_q_[i].at : dac_clock_;
        dac_acc_ += uint32_t(dac_) * uint32_t(at - dac_clock_);
        dac_clock_ = at;
        dac_ = dac_q_[i].v;
    }
    if (i) {
        for (int k = i; k < dac_qn_; ++k) dac_q_[k - i] = dac_q_[k];
        dac_qn_ -= i;
    }
    if (snd_cycle > dac_clock_) {
        dac_acc_ += uint32_t(dac_) * uint32_t(snd_cycle - dac_clock_);
        dac_clock_ = snd_cycle;
    }
}

void Machine::audio_to(int snd_cycle)
{
    while (audio_done_ < audio_n_ && sample_end_ <= snd_cycle) {
        const int end = sample_end_;
        for (int k = 0; k < 5; ++k) chip_to(k, end);
        dac_to(end);
        const uint32_t b = uint32_t(snd_base_ & 1);
        const uint32_t clocks = ((b + uint32_t(end)) >> 1) - ((b + uint32_t(sample_start_)) >> 1);
        const int16_t s = mix_sample(clocks);
        if (audio_) audio_[audio_done_] = s;
        ++audio_done_;
        sample_start_ = end;
        sample_end_ = audio_done_ < audio_n_ ? (audio_done_ + 1) * snd_frame_ / audio_n_ : snd_frame_;
        for (uint32_t& a : acc_) a = 0;
        dac_acc_ = 0;
    }
}

// --- frame ---------------------------------------------------------------------------------

void Machine::vblank()
{
    capture();
    if (nmi_mask_ && !nmi_line_) {
        nmi_line_ = true;
        nmi_now_ = true;
        cpu_.nmi();
    }
    if (sub_irq_mask_) sub_.irq(true);
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // The sound board's cycles this frame: 50688 x (14.318 MHz / 4) / 3.072 MHz.
    snd_rem_ += uint64_t(kCyclesPerFrame) * kSoundXtal;
    snd_frame_ = int(snd_rem_ / (uint64_t(kCpuClock) * 4));
    snd_rem_ %= uint64_t(kCpuClock) * 4;

    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = n > 0 ? n : 0;
    audio_done_ = 0;
    sample_start_ = 0;
    sample_end_ = audio_n_ ? snd_frame_ / audio_n_ : snd_frame_;
    for (int& c : ay_clock_) c = 0;
    dac_clock_ = 0;
    for (uint32_t& a : acc_) a = 0;
    dac_acc_ = 0;

    // The main and sub CPUs take turns (they share RAM).
    static_assert(kVblankLine % kSliceLines == 0 && kLinesPerFrame % kSliceLines == 0, "slices");
    int done = carry_;
    for (int line = 0; line < kLinesPerFrame; line += kSliceLines) {
        if (line == kVblankLine) vblank();
        const int end = (line + kSliceLines) * kCyclesPerLine;
        if (idle_skip_) done += main_idle(end - done);
        nmi_now_ = false;
        while (done < end) {
            slice_start_ = done;
            done += cpu_.run(end - done);
        }
        run_sub(end / 2);
    }
    carry_ = done - kCyclesPerFrame;
    sub_done_ -= kCyclesPerFrame / 2;
    slice_start_ = 0;

    sound_to(kCyclesPerFrame);
    sync_audio(snd_frame_);
    for (int k = 0; k < 5; ++k) chip_to(k, snd_frame_);    // (no samples asked for)
    dac_to(snd_frame_);
    for (int i = 0; i < dac_qn_; ++i) dac_q_[i].at -= snd_frame_;   // written past the frame's end
    ++frame_;

    // The sound CPU's and the i8039's overrun carry into the next frame.
    snd_base_ += uint64_t(snd_frame_);
    snd_done_ -= snd_frame_;
    mcu_done_ -= kI8039PerFrame;
    const int count = audio_ ? audio_n_ : 0;
    audio_ = nullptr;
    return count;
}

} // namespace gyruss
