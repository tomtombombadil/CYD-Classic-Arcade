// Mappy board - see mappy.h.
//
// Main CPU (6809, 1.536 MHz):
//   0000-07FF  tile codes          0800-0FFF  tile attributes (bits 0-5
//              colour, bit 6 in front of the sprites)
//   1000-27FF  RAM; the sprite registers sit in it at 1780-17FF (code,
//              colour), 1F80-1FFF (Y, X) and 2780-27FF (flips, sizes / off,
//              X bit 8), two bytes a sprite
//   3800-3FFF  write: the tile map's scroll = address bits 3-10
//   4000-43FF  RAM shared with the sound CPU (mirrored at 4400); its first
//              64 bytes are the 15XX's registers, 8 a voice: 3 volume, 4-5
//              frequency bits 0-15, 6 bits 0-3 frequency bits 16-19, bits
//              4-6 waveform; 2 sets the waveform position
//   4800-480F  I/O chip 0          4810-481F  I/O chip 1
//   5000-500F  write: LS259 latch, bit = A1-A3, data = A0: 0 sound CPU IRQ
//              on, 1 main CPU IRQ on (off clears a pending one), 2 flip
//              screen, 3 sound on, 4 I/O chips run (0 = held in reset), 5
//              sound CPU runs (0 = held in reset)
//   8000       write: watchdog reset (Mappy; Dig Dug II doesn't use it)
//   8000-FFFF  program ROM (Mappy: A000-FFFF)
// Sound CPU (6809, 1.536 MHz):
//   0000-03FF  the shared RAM (mirrored to 1FFF)
//   2000-200F  write: the same latch (mirrored to 3FFF)
//   E000-FFFF  program ROM
// Both IRQs come at VBLANK (line 224) and stay until the CPU turns its IRQ
// enable off; the I/O chips carry out their commands a moment after it.
#include "mappy.h"

#include <cstring>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "sprite_column() reads 8 bytes as a little-endian word");

namespace mappy {

namespace {

// Palette PROM bits -> 8-bit levels through the resistor DAC: red and green
// 1K / 470 / 220 ohm, blue 470 / 220 ohm (as Pac-Man's).
constexpr uint8_t kRGWeights[3] = {33, 71, 151};
constexpr uint8_t kBWeights[2] = {81, 174};

uint16_t rgb565(uint8_t v, bool swap)
{
    const int r = kRGWeights[0] * (v & 1) + kRGWeights[1] * ((v >> 1) & 1) + kRGWeights[2] * ((v >> 2) & 1);
    const int g = kRGWeights[0] * ((v >> 3) & 1) + kRGWeights[1] * ((v >> 4) & 1) + kRGWeights[2] * ((v >> 5) & 1);
    const int b = kBWeights[0] * ((v >> 6) & 1) + kBWeights[1] * ((v >> 7) & 1);
    uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return swap ? uint16_t((px >> 8) | (px << 8)) : px;
}

// Lines a CPU slice runs before the other CPU gets its turn (the CPUs share
// RAM; MAME runs them in ~6000 Hz slices).
constexpr int kSliceLines = 2;

// Idle skipping, exact: a CPU waiting in "LDA addr ; BEQ back" (extended
// B6 hh ll 27 FB, 8 cycles a pass; direct 96 dd 27 FC, 7) on a RAM byte
// that is 0 sees it change only through an interrupt it takes, or the
// other CPU - which has run its slice already or runs only after this one
// (slices never overlap). With no interrupt it would take, the passes that
// end before the slice does are skipped (time counted, A = 0, flags as LDA
// left them); the CPU runs the last one itself, so it stops where running
// would have. Dig Dug II: the main CPU's frame wait (80C1), the sound CPU's
// (E381) - ~60 % of the cycles.
int skip_wait_at_lda(m6809::Cpu& c, int t, int target);

int skip_wait(m6809::Cpu& c, int t, int target)
{
    if (c.in_cwai() || c.in_sync() || c.halted()) return t;
    if ((c.irq_line() && !(c.cc & m6809::CC_I)) || (c.firq_line() && !(c.cc & m6809::CC_F))) return t;
    // At the BEQ with Z set: it branches back to the LDA (3 cycles).
    if (c.rd(c.pc) == 0x27 && (c.cc & m6809::CC_Z) &&
        ((c.rd(uint16_t(c.pc + 1)) == 0xfb && c.rd(uint16_t(c.pc - 3)) == 0xb6) ||
         (c.rd(uint16_t(c.pc + 1)) == 0xfc && c.rd(uint16_t(c.pc - 2)) == 0x96))) {
        if (t + 3 >= target) return t;
        const uint16_t back = uint16_t(c.pc + 2 + int8_t(c.rd(uint16_t(c.pc + 1))));
        const uint16_t at = c.pc;
        c.pc = back;
        const int t2 = skip_wait_at_lda(c, t + 3, target);
        if (t2 == t + 3) {
            c.pc = at;                          // nothing skipped: leave it to the CPU
            return t;
        }
        return t2;
    }
    return skip_wait_at_lda(c, t, target);
}

int skip_wait_at_lda(m6809::Cpu& c, int t, int target)
{
    const uint16_t pc = c.pc;
    uint16_t addr;
    int pass;
    const uint8_t op = c.rd(pc);
    if (op == 0xb6 && c.rd(uint16_t(pc + 3)) == 0x27 && c.rd(uint16_t(pc + 4)) == 0xfb) {
        addr = uint16_t(c.rd(uint16_t(pc + 1)) << 8 | c.rd(uint16_t(pc + 2)));
        pass = 8;
    } else if (op == 0x96 && c.rd(uint16_t(pc + 2)) == 0x27 && c.rd(uint16_t(pc + 3)) == 0xfc) {
        addr = uint16_t(c.dp << 8 | c.rd(uint16_t(pc + 1)));
        pass = 7;
    } else {
        return t;
    }
    const int page = addr >> m6809::Cpu::kPageShift;
    if (!c.read_page[page] || !c.write_page[page] || c.rd(addr) != 0) return t;
    const int k = (target - t - 1) / pass;
    if (k <= 0) return t;
    c.r.a = 0;
    c.cc = uint8_t((c.cc & ~(m6809::CC_N | m6809::CC_V)) | m6809::CC_Z);
    return t + k * pass;
}

} // namespace

// --- Dig Dug II ------------------------------------------------------------------

namespace digdug2 {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x8000},
    {"sub",      0x2000},
    {"tiles",    0x1000},
    {"sprites0", 0x4000},
    {"sprites1", 0x4000},
    {"proms",    0x0220},
    {"namco",    0x0100},
};

const char* const kVariants[] = {"digdug2", "digdug2o"};

// CRCs as in MAME's "digdug2" (New Ver., variant 1) and "digdug2o" (Old
// Ver., variant 2: other program ROMs and sprite lookup PROM).
constexpr romset::Rom kRoms[] = {
    {kRegionMain,     0x0000, 0x4000, 0xcc155338, "d23_3.1d", false, 1},
    {kRegionMain,     0x4000, 0x4000, 0x40e46af8, "d23_1.1b", false, 1},
    {kRegionMain,     0x0000, 0x4000, 0xbe7ec80b, "d21_3.1d", false, 2},
    {kRegionMain,     0x4000, 0x4000, 0x5c77c0d4, "d21_1.1b", false, 2},
    {kRegionSub,      0x0000, 0x2000, 0x737443b1, "d21_4.1k", false},
    {kRegionTiles,    0x0000, 0x1000, 0xafcb4509, "d21_5.3b", false},
    {kRegionSprites0, 0x0000, 0x4000, 0xdf1f4ad8, "d21_6.3m", false},
    {kRegionSprites1, 0x0000, 0x4000, 0xccadb3ea, "d21_7.3n", false},
    {kRegionProms,    0x0000, 0x0020, 0x9b169db5, "d21-5.5b", false},
    {kRegionProms,    0x0020, 0x0100, 0x55a88695, "d21-6.4c", false},
    {kRegionProms,    0x0120, 0x0100, 0x9c55feda, "d21-7.5k", false, 1},
    {kRegionProms,    0x0120, 0x0100, 0x1525a4d1, "d2x-7.5k", false, 2},
    {kRegionSound,    0x0000, 0x0100, 0xe0074ee2, "d21-3.3m", false},
};

} // namespace

const romset::Set kRomSet = {
    "digdug2", nullptr, "Dig Dug II", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

const Board kBoard = {
    &kRomSet, 0x8000, {namco_io::Chip5x::Type::k58xx, namco_io::Chip5x::Type::k56xx}, 0, 16, kDipDefault,
};

} // namespace digdug2

// --- setup -------------------------------------------------------------------------

void Machine::init(const Board& board, const uint8_t* const* r, uint32_t audio_rate, bool swap)
{
    board_ = board;
    if (board_.sprite_colours < 1) board_.sprite_colours = 1;
    if (board_.sprite_colours > kMaxSpriteColours) board_.sprite_colours = kMaxSpriteColours;
    audio_rate_ = audio_rate;
    rom_tiles_ = r[kRegionTiles];
    rom_spr_[0] = r[kRegionSprites0];
    rom_spr_[1] = r[kRegionSprites1];
    dsw1_ = board_.dsw1_default;

    main_.ctx = this;
    main_.mem_read = main_read;
    main_.mem_write = main_write;
    main_.map_ram(0x0000, 0x1000, vram_);
    main_.map_ram(0x1000, 0x1800, ram_);
    main_.map_ram(0x4000, 0x400, shared_);
    main_.map_ram(0x4400, 0x400, shared_);
    main_.map_read(board_.main_rom_base, 0x10000u - board_.main_rom_base, r[kRegionMain]);

    sub_.ctx = this;
    sub_.mem_read = sub_read;
    sub_.mem_write = sub_write;
    for (uint16_t a = 0; a < 0x2000; a += 0x400) sub_.map_ram(a, 0x400, shared_);
    sub_.map_read(0xe000, 0x2000, r[kRegionSub]);

    io_[0].init(board_.chip[0]);
    io_[1].init(board_.chip[1]);
    wsg_.init(r[kRegionSound], 8, kSoundClock, audio_rate);
    // The loudest moments (Dig Dug II's tunes with every voice up) peak
    // ~20,000 at this level (the default, for all 8 voices at full volume,
    // reached ~24,000).
    wsg_.set_gain(26);

    // Colours: the palette PROM's 32 entries; characters use the upper 16
    // through their lookup PROM, sprites the lower 16 through theirs. A
    // lookup value of 15 is a character's see-through pen (for sprites
    // behind front characters) and a sprite's transparent pen.
    const uint8_t* pal = r[kRegionProms];
    const uint8_t* clut = pal + 0x20;
    const uint8_t* slut = pal + 0x120;
    for (int c = 0; c < 64; ++c) {
        char_clear_[c] = 0;
        for (int p = 0; p < 4; ++p) {
            const uint8_t e = clut[c * 4 + p] & 0x0f;
            char_pen_[c * 4 + p] = rgb565(pal[0x10 + e], swap);
            if (e == 0x0f) char_clear_[c] |= uint8_t(1 << p);
        }
    }
    for (int c = 0; c < board_.sprite_colours; ++c) {
        sprite_opaque_[c] = 0;
        for (int p = 0; p < 16; ++p) {
            const uint8_t e = slut[c * 16 + p] & 0x0f;
            sprite_pen_[c * 16 + p] = rgb565(pal[e], swap);
            if (e != 0x0f) sprite_opaque_[c] |= uint16_t(1 << p);
        }
    }

    // Characters (ROM stored inverted): 16 bytes each; pixel x 0-3 in bytes
    // 8-15, 4-7 in bytes 0-7 (byte = line y); in each byte plane 0 (the
    // pixel's high bit) in bits 7-4, plane 1 in bits 3-0, leftmost pixel in
    // the top bit. Kept per board column x: the 8 pixels down it.
    for (int code = 0; code < 256; ++code) {
        const uint8_t* src = rom_tiles_ + code * 16;
        for (int x = 0; x < 8; ++x) {
            uint16_t bits = 0;
            for (int y = 0; y < 8; ++y) {
                const uint8_t b = uint8_t(~src[(x < 4 ? 8 : 0) + y]);
                const int xi = x & 3;
                const int p = (((b >> (7 - xi)) & 1) << 1) | ((b >> (3 - xi)) & 1);
                bits |= uint16_t(p << (2 * y));
            }
            tile_col_[code * 8 + x] = bits;
        }
    }

    reset();
}

void Machine::reset()
{
    main_.reset();
    sub_.reset();
    main_.irq(false);
    sub_.irq(false);
    // The LS259 clears: both IRQs off, sound off, the I/O chips and the
    // sound CPU held in reset.
    latch_ = 0;
    wsg_.reset();
    wsg_.set_enabled(false);
    std::memset(sound_shadow_, 0, sizeof sound_shadow_);
    io_[0].set_reset(true);
    io_[1].set_reset(true);
    watchdog_ = 0;
    main_t_ = sub_t_ = 0;
}

// --- memory ----------------------------------------------------------------------

uint8_t Machine::main_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xffe0) == 0x4800) return m.io_[(addr >> 4) & 1].read(uint8_t(addr & 15));
    return 0xff;
}

void Machine::main_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0x3800) {
        m.scroll_ = uint8_t(addr >> 3);
    } else if ((addr & 0xffe0) == 0x4800) {
        m.io_[(addr >> 4) & 1].write(uint8_t(addr & 15), v);
    } else if ((addr & 0xfff0) == 0x5000) {
        m.latch_write(addr);
    } else if (addr == 0x8000) {
        m.watchdog_ = 0;
    }
}

uint8_t Machine::sub_read(void*, uint16_t) { return 0xff; }

void Machine::sub_write(void* ctx, uint16_t addr, uint8_t)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xe000) == 0x2000) m.latch_write(addr);
}

void Machine::latch_write(uint16_t addr)
{
    const int bit = (addr >> 1) & 7;
    const bool on = addr & 1;
    const uint8_t mask = uint8_t(1 << bit);
    const bool was = latch_ & mask;
    latch_ = on ? uint8_t(latch_ | mask) : uint8_t(latch_ & ~mask);
    switch (bit) {
    case 0: if (!on) sub_.irq(false); break;
    case 1: if (!on) main_.irq(false); break;
    case 3:
        if (on != was) {
            audio_to(now());
            wsg_.set_enabled(on);
        }
        break;
    case 4:
        if (on != was) {
            io_[0].set_reset(!on);
            io_[1].set_reset(!on);
        }
        break;
    case 5:
        if (on && !was) {                   // the sound CPU starts from its reset vector
            sub_.reset();
            sub_t_ = now();
        } else if (!on && was && cur_ == &sub_) {
            sub_.stop();                    // it held itself in reset
        }
        break;
    default: break;                         // 2: flip screen (not drawn), 6-7: not connected
    }
}

// --- running ---------------------------------------------------------------------

void Machine::set_ports()
{
    const uint16_t b = buttons_;
    auto low = [b](uint16_t mask, int bit) { return (b & mask) ? 0 : (1 << bit); };
    // Chip 0: A coins, B player 1's stick, C player 2's (cocktail), D buttons.
    const uint8_t coins = uint8_t(low(kCoin, 0) | low(kCoin2, 1) | 0x04 | low(kService, 3));
    const uint8_t p1 = uint8_t(low(kUp, 0) | low(kRight, 1) | low(kDown, 2) | low(kLeft, 3));
    const uint8_t buttons = uint8_t(low(kButton1, 0) | 0x02 | low(kStart1, 2) | low(kStart2, 3));
    io_[0].set_ports(0, coins, p1, 0x0f, buttons);
    io_[0].set_ports(1, coins, p1, 0x0f, buttons);
    // Chip 1: A DSW2 (unused, all off) through a multiplexer on pin 13, B /
    // C DSW1 bits 0-3 / 4-7, D DSW0: bit 0 button 2, 1 player 2's button 2,
    // 2 upright cabinet, 3 service switch (off).
    const uint8_t dsw1 = uint8_t(b & kTest ? dsw1_ & ~1 : dsw1_);
    const uint8_t dsw0 = uint8_t(low(kButton2, 0) | 0x02 | 0x04 | 0x08);
    io_[1].set_ports(0, 0x0f, dsw1 & 15, dsw1 >> 4, dsw0);
    io_[1].set_ports(1, 0x0f, dsw1 & 15, dsw1 >> 4, dsw0);
}

void Machine::run_to(int cycle)
{
    if (idle_skip_) main_t_ = skip_wait(main_, main_t_, cycle);
    if (main_t_ < cycle) {
        cur_ = &main_;
        cur_base_ = main_t_;
        main_t_ += main_.run(cycle - main_t_);
    }
    if (latch_ & 0x20) {
        if (idle_skip_) sub_t_ = skip_wait(sub_, sub_t_, cycle);
        if (sub_t_ < cycle) {
            cur_ = &sub_;
            cur_base_ = sub_t_;
            sub_t_ += sub_.run(cycle - sub_t_);
        }
    } else if (sub_t_ < cycle) {
        sub_t_ = cycle;
    }
    cur_ = nullptr;
    sound_regs();
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    wsg_.render(audio_ + audio_done_, target - audio_done_);
    audio_done_ = target;
}

// The 15XX's registers are RAM the CPUs write directly: after each slice,
// the ones that changed take effect (the slice's sound so far played with
// the old ones).
void Machine::sound_regs()
{
    if (std::memcmp(sound_shadow_, shared_, sizeof sound_shadow_) == 0) return;
    audio_to(main_t_ < sub_t_ ? main_t_ : sub_t_);
    for (int v = 0; v < 8; ++v) {
        const uint8_t* r = shared_ + v * 8;
        uint8_t* old = sound_shadow_ + v * 8;
        if (std::memcmp(r, old, 8) == 0) continue;
        if (r[2] != old[2]) wsg_.set_position(v, r[2]);
        wsg_.set_volume(v, r[3] & 0x0f);
        wsg_.set_waveform(v, uint8_t((r[6] >> 4) & 7));
        wsg_.set_frequency(v, uint32_t(r[4]) | uint32_t(r[5]) << 8 | uint32_t(r[6] & 0x0f) << 16);
        std::memcpy(old, r, 8);
    }
}

void Machine::capture()
{
    VideoState& vs = video_;
    std::memcpy(vs.vram, vram_, sizeof vs.vram);
    vs.scroll = scroll_;
    vs.flip = latch_ & 0x04;
    const uint8_t* s1 = ram_ + 0x780;       // 1780: code, colour
    const uint8_t* s2 = ram_ + 0xf80;       // 1F80: Y, X
    const uint8_t* s3 = ram_ + 0x1780;      // 2780: flips / sizes, off / X bit 8
    int n = 0;
    for (int o = 0; o < 0x80; o += 2) {
        if (s3[o + 1] & 0x02) continue;     // off
        const int sizex = (s3[o] >> 2) & 1, sizey = (s3[o] >> 3) & 1;
        Sprite& s = vs.sprite[n];
        s.x = int16_t(s2[o + 1] + 0x100 * (s3[o + 1] & 1) - 40);
        // Sprites are delayed a line; Y counts up from the bottom.
        int y = 256 - s2[o] + 1 - 16 * sizey;
        s.y = int16_t((y & 0xff) - 32);
        s.w = uint8_t(16 << sizex);
        s.h = uint8_t(16 << sizey);
        if (s.x >= 288 || s.x + s.w <= 0 || s.y >= 224 || s.y + s.h <= 0) continue;
        s.code = uint8_t(s1[o] & ~(sizex | (sizey << 1)));
        s.colour = uint8_t(s1[o + 1] & (board_.sprite_colours - 1));
        s.flip = uint8_t(s3[o] & 3);
        ++n;
    }
    vs.sprite_count = uint8_t(n);
}

void Machine::vblank()
{
    capture();
    if (latch_ & 0x02) main_.irq(true);
    if ((latch_ & 0x01) && (latch_ & 0x20)) sub_.irq(true);
    if (board_.watchdog_frames && ++watchdog_ >= board_.watchdog_frames) {
        ++watchdog_resets_;
        reset();
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    set_ports();
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio;
    audio_n_ = audio ? n : 0;
    audio_done_ = 0;

    int line = 0;
    auto run_lines = [&](int to) {
        while (line < to) {
            line = line + kSliceLines < to ? line + kSliceLines : to;
            run_to(line * kCyclesPerLine);
        }
    };
    run_lines(kVblankLine);
    vblank();
    // The I/O chips act on their commands a moment after VBLANK (the CPU
    // writes them in its interrupt handler).
    run_lines(kVblankLine + 1);
    for (namco_io::Chip5x& c : io_) c.run();
    run_lines(kLinesPerFrame);

    main_t_ -= kCyclesPerFrame;
    sub_t_ -= kCyclesPerFrame;
    ++frame_;
    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    return audio ? n : 0;
}

// --- video -------------------------------------------------------------------------

// A sprite cell's 16 pixels down board column x (0-15), a byte each (line
// y in byte y of the pair, lines 0-7 first): planes 0-1 in the even bytes
// (3M), 2-3 in the odd (3N); 8 bytes (lines 0-7) per 4-pixel group, lines
// 8-15 32 bytes on; in a byte the first plane in bits 7-4, the second in
// 3-0, leftmost pixel in the top bit. Eight lines at a time: the column's
// bit of each of 8 bytes lands in bit 0 of its byte lane.
void Machine::sprite_column(int code, int x, uint64_t col[2]) const
{
    constexpr uint64_t kLanes = 0x0101010101010101ull;
    const int k = (code & 0xff) * 64 + (x >> 2) * 8;
    const int s0 = 7 - (x & 3), s1 = 3 - (x & 3);
    for (int h = 0; h < 2; ++h) {
        uint64_t b0, b1;
        std::memcpy(&b0, rom_spr_[0] + k + 32 * h, 8);
        std::memcpy(&b1, rom_spr_[1] + k + 32 * h, 8);
        col[h] = (((b0 >> s0) & kLanes) << 3) | (((b0 >> s1) & kLanes) << 2) | (((b1 >> s0) & kLanes) << 1) |
                 ((b1 >> s1) & kLanes);
    }
}

// Display line y = board column y (the board's x); display x = 223 - the
// board's line.
void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    uint8_t front[kScreenW];                // a front character's opaque pixel
    std::memset(front, 0, sizeof front);

    // Tiles: board columns of 8 pixels, 36 across; 0-1 and 34-35 are fixed
    // (score / lives), 2-33 scroll down a 60-row map by the scroll value.
    const int col = y >> 3, tx = y & 7;
    const bool fixed = col < 2 || col >= 34;
    const int fc = (col - 2) & 3;
    int hy = 0;
    while (hy < 224) {
        int addr, ty;
        if (fixed) {
            const int row = hy >> 3;
            ty = hy & 7;
            addr = ((row + 2) & 0x0f) + (row & 0x10) + (fc << 5) + 0x780;
        } else {
            int src = hy + vs.scroll;
            if (src >= 480) src -= 480;
            ty = src & 7;
            addr = (col - 2) + (src >> 3) * 32;
        }
        int run = 8 - ty;
        if (run > 224 - hy) run = 224 - hy;
        const uint8_t attr = vs.vram[0x800 + addr];
        const int colour = attr & 0x3f;
        const uint16_t* pens = char_pen_ + colour * 4;
        unsigned bits = unsigned(tile_col_[vs.vram[addr] * 8 + tx]) >> (2 * ty);
        uint16_t* o = out + (223 - hy);
        if (attr & 0x40) {
            const uint8_t clear = char_clear_[colour];
            uint8_t* f = front + (223 - hy);
            for (int i = 0; i < run; ++i, bits >>= 2) {
                const unsigned p = bits & 3;
                o[-i] = pens[p];
                f[-i] = !((clear >> p) & 1);
            }
        } else if (run == 8) {
            o[0] = pens[bits & 3];
            o[-1] = pens[(bits >> 2) & 3];
            o[-2] = pens[(bits >> 4) & 3];
            o[-3] = pens[(bits >> 6) & 3];
            o[-4] = pens[(bits >> 8) & 3];
            o[-5] = pens[(bits >> 10) & 3];
            o[-6] = pens[(bits >> 12) & 3];
            o[-7] = pens[(bits >> 14) & 3];
        } else {
            for (int i = 0; i < run; ++i, bits >>= 2) o[-i] = pens[bits & 3];
        }
        hy += run;
    }

    // Sprites, later ones on top, behind front characters.
    for (int i = 0; i < vs.sprite_count; ++i) {
        const Sprite& s = vs.sprite[i];
        int lx = y - s.x;
        if (lx < 0 || lx >= s.w) continue;
        if (s.flip & 1) lx = s.w - 1 - lx;
        const uint16_t* pens = sprite_pen_ + s.colour * 16;
        const uint16_t opaque = sprite_opaque_[s.colour];
        for (int cy = 0; cy < s.h; cy += 16) {
            // The cell drawn at rows cy..cy+15 of the sprite.
            const int cell_y = (s.flip & 2) ? (s.h - 16 - cy) : cy;
            uint64_t col[2];
            sprite_column(s.code + 2 * (cell_y >> 4) + (lx >> 4), lx & 15, col);
            for (int r = 0; r < 16; ++r) {
                const int line = s.y + cy + r;
                if (line < 0 || line >= 224) continue;
                const int X = 223 - line;
                const int rr = (s.flip & 2) ? 15 - r : r;
                const int p = int(col[rr >> 3] >> (8 * (rr & 7))) & 15;
                if ((opaque & (1 << p)) && !front[X]) out[X] = pens[p];
            }
        }
    }
}

} // namespace mappy
