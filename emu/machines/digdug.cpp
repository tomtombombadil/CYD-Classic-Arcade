// Dig Dug machine - see digdug.h.
//
// Dig Dug's part of the memory map (the rest: namco_galaga_common.cpp):
//   8000-83FF  text layer codes (8400-87FF is work RAM)
//   A000-A007  write: video latch, bit = A2-A0 (mirrored through A7FF):
//              0-1 playfield picture, 2 text colour mode, 3 playfield off,
//              4-5 playfield colour bank, 7 flip screen
//   B800-B83F  EAROM: write = address (A5-A0) + data, read = data out
//   B840       EAROM control: bit 0 clock, 1 low = read mode (C1),
//              2 = erase (C2), 3 = chip select
//   latch bits 5-7: the 53XX's mode (it always reads the DIP switches)
//   06XX chips: 0 = 51XX, 1 = 53XX
#include "digdug.h"

#include <cstring>

namespace digdug {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x4000},
    {"sub",     0x2000},
    {"sub2",    0x1000},
    {"gfx1",    0x0800},
    {"gfx2",    0x4000},
    {"gfx3",    0x1000},
    {"gfx4",    0x1000},
    {"proms",   0x0220},
    {"namco",   0x0200},
};

// CRCs as in MAME's "digdug" set (Atari-numbered 136007.1xx files in some
// zips hold the same data). The custom chips' own programs (51xx.bin,
// 53xx.bin) aren't needed.
constexpr romset::Rom kRoms[] = {
    {kRegionMain,    0x0000, 0x1000, 0xa80ec984, "dd1a.1", false},
    {kRegionMain,    0x1000, 0x1000, 0x559f00bd, "dd1a.2", false},
    {kRegionMain,    0x2000, 0x1000, 0x8cbc6fe1, "dd1a.3", false},
    {kRegionMain,    0x3000, 0x1000, 0xd066f830, "dd1a.4", false},
    {kRegionSub,     0x0000, 0x1000, 0x6687933b, "dd1a.5", false},
    {kRegionSub,     0x1000, 0x1000, 0x843d857f, "dd1a.6", false},
    {kRegionSound,   0x0000, 0x1000, 0xa41bce72, "dd1.7", false},
    {kRegionText,    0x0000, 0x0800, 0xf14a6fe1, "dd1.9", false},
    {kRegionSprites, 0x0000, 0x1000, 0xe22957c8, "dd1.15", false},
    {kRegionSprites, 0x1000, 0x1000, 0x2829ec99, "dd1.14", false},
    {kRegionSprites, 0x2000, 0x1000, 0x458499e9, "dd1.13", false},
    {kRegionSprites, 0x3000, 0x1000, 0xc58252a0, "dd1.12", false},
    {kRegionTiles,   0x0000, 0x1000, 0x7b383983, "dd1.11", false},
    {kRegionMap,     0x0000, 0x1000, 0x2cf399c2, "dd1.10b", false},
    {kRegionProms,   0x0000, 0x0020, 0x4cb9da99, "136007.113", false},
    {kRegionProms,   0x0020, 0x0100, 0x00c7c419, "136007.111", false},
    {kRegionProms,   0x0120, 0x0100, 0xe9b3e08e, "136007.112", false},
    {kRegionWave,    0x0000, 0x0100, 0x7a2815b4, "136007.110", false},
    {kRegionWave,    0x0100, 0x0100, 0x77245b66, "136007.109", true},    // timing, unused
};

// Sprites stay inside display lines 16-271 (the hardware's 32-column
// middle, without the score lines).
constexpr int kSpriteTop = 16, kSpriteBottom = 272;

// Idle loops (see namco_galaga::Speedup), found by profiling and read in
// the program; none is entered other than from its top.
//   sound 00B0-00B4  LD SP,8B80 / JR 00B0: the sound CPU's main loop (its
//                    work is in the NMIs)
//   main 0238-027F   the main CPU's main loop (EI, then the work flags):
//                    with neither bit 0 nor bit 1 of 87CC set it only
//                    reads; checked at 0239-0251 (the reads of 8400 /
//                    840D / 840E feed nothing when no work is due), 0272
//                    and 027E
//   sub 0078-007F    LD SP / CALL 1DDC / JR 0078, and 1DDC-1DF5: the sub
//   sub 1DDC-1DF5    CPU's main loop runs task 9B3D (1-4; 0 is a RET, 5 and
//                    up nothing) unless 89CF is set (then it keeps writing
//                    9B3D = 0); idle when 9B3D is 0, or 89CF is clear and
//                    9B3D >= 5; checked at 0078 / 007B / 007E / 1DDC
using namco_galaga::Idle;
Idle main_loop(namco_galaga::Board& b, uint16_t pc)
{
    const bool point = (pc >= 0x0239 && pc <= 0x0251) || pc == 0x0272 || pc == 0x027e;
    if (!point) return Idle::kStep;
    return (b.peek(0x87cc) & 0x03) == 0 ? Idle::kYes : Idle::kNo;
}
Idle sub_loop(namco_galaga::Board& b, uint16_t pc)
{
    if (pc != 0x0078 && pc != 0x007b && pc != 0x007e && pc != 0x1ddc) return Idle::kStep;
    const uint8_t task = b.peek(0x9b3d);
    return (task == 0 || (!b.peek(0x89cf) && task >= 5)) ? Idle::kYes : Idle::kNo;
}

constexpr namco_galaga::Speedup kSpeedups[] = {
    {namco_galaga::kSound, 0x00b0, 0x00b4, nullptr, 11},
    {namco_galaga::kMain, 0x0238, 0x027f, main_loop, 8},
    {namco_galaga::kSub, 0x0078, 0x007f, sub_loop, 9},
    {namco_galaga::kSub, 0x1ddc, 0x1df5, sub_loop, 9},
};

} // namespace

const romset::Set kRomSet = {
    "digdug", nullptr, "Dig Dug (rev 2)", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap)
{
    roms_ = roms;
    BoardRoms b{};
    b.cpu[namco_galaga::kMain] = roms.main;
    b.cpu[namco_galaga::kSub] = roms.sub;
    b.cpu[namco_galaga::kSound] = roms.sound;
    b.cpu_size[namco_galaga::kMain] = 0x4000;
    b.cpu_size[namco_galaga::kSub] = 0x2000;
    b.cpu_size[namco_galaga::kSound] = 0x1000;
    b.wave = roms.wave;

    // Colours: text pixels 1 = palette entry (colour), 0 transparent;
    // sprites use entries 16-31 through their lookup PROM (15 =
    // transparent), the playfield entries 0-15 through its own.
    const uint8_t* pal = roms.proms;
    for (int c = 0; c < 16; ++c) text_pen_[c] = namco_galaga::prom_rgb565(pal[c], swap);
    for (int c = 0; c < 64; ++c) {
        sprite_opaque_[c] = 0;
        for (int p = 0; p < 4; ++p) {
            const uint8_t se = roms.proms[0x20 + c * 4 + p] & 0x0f;
            const uint8_t te = roms.proms[0x120 + c * 4 + p] & 0x0f;
            sprite_pen_[c * 4 + p] = namco_galaga::prom_rgb565(pal[0x10 | se], swap);
            tile_pen_[c * 4 + p] = namco_galaga::prom_rgb565(pal[te], swap);
            if (se != 0x0f) sprite_opaque_[c] |= uint8_t(1 << p);
        }
    }

    // Text characters: 8 bytes each, byte = hardware row y, bit x = pixel
    // x. Display row r is hardware x = r, display x d is hardware y 7 - d.
    for (int code = 0; code < 128; ++code)
        for (int r = 0; r < 8; ++r) {
            uint8_t bits = 0;
            for (int d = 0; d < 8; ++d)
                if ((roms.text[code * 8 + 7 - d] >> r) & 1) bits |= uint8_t(1 << d);
            text_rows_[code * 8 + r] = bits;
        }
    for (int code = 0; code < 128; ++code) {
        uint8_t any = 0;
        for (int r = 0; r < 8; ++r) any |= text_rows_[code * 8 + r];
        text_blank_[code] = any == 0;
    }
    namco_galaga::decode_chars_2bpp(roms.tiles, 256, tile_rows_);
    namco_galaga::decode_sprites(roms.sprites, 256, sprite_rows_);
    namco_galaga::blank_sprites(sprite_rows_, 256, sprite_blank_);

    std::memset(earom_, 0xff, sizeof earom_);
    chip53_.set_dips(dips_a_, dips_b_);
    board_init(b, audio_rate, kSpeedups, int(sizeof kSpeedups / sizeof kSpeedups[0]));
    reset();
}

void Machine::reset()
{
    board_reset();
    video_latch_ = 0;
}

void Machine::chips_reset() { chip53_.reset(); }

uint8_t Machine::chip_read(int chip) { return chip == 1 ? chip53_.read() : 0xff; }

uint8_t Machine::io_read(int, uint16_t a)
{
    if ((a & 0xffc0) == 0xb800) return earom_out_;
    return 0xff;
}

void Machine::io_write(int, uint16_t a, uint8_t v)
{
    if ((a & 0xf800) == 0xa000) {
        const uint8_t bit = uint8_t(1 << (a & 7));
        video_latch_ = (v & 1) ? uint8_t(video_latch_ | bit) : uint8_t(video_latch_ & ~bit);
        return;
    }
    if ((a & 0xffc0) == 0xb800) {
        earom_addr_ = uint8_t(a & 0x3f);
        earom_data_ = v;
        return;
    }
    if (a == 0xb840) {
        // ER2055: selected while bit 3 is set; a falling clock reads (C1
        // set = bit 1 clear) or, with C1 clear, writes (C2 clear; the cell
        // takes the AND, as an unerased cell would) or erases (C2 set).
        const uint8_t old = earom_ctrl_;
        earom_ctrl_ = v;
        const bool selected = v & 0x08;
        const bool c1 = !(v & 0x02), c2 = (v & 0x04) != 0;
        if (!selected) return;
        const bool clock_fell = (old & 0x01) && !(v & 0x01);
        if (clock_fell && c1) earom_out_ = earom_[earom_addr_];
        if (!c1 && (clock_fell || (old & 0x0e) != (v & 0x0e))) {
            if (c2) earom_[earom_addr_] = 0xff;
            else    earom_[earom_addr_] &= earom_data_;
        }
    }
}

void Machine::vblank()
{
    VideoState& vs = video_;
    std::memcpy(vs.vram, vram_, 0x400);
    vs.latch = video_latch_;

    // Sprites: 64 of them, codes / colours at 8B80, position at 9380,
    // flips at 9B80 (two bytes each). Bit 7 of the code makes a 2 x 2
    // sprite (code bits 0-5 then move up two places). x wraps at 256.
    vs.sprite_count = 0;
    for (int s = 0; s < kSprites; ++s) {
        const uint8_t* r1 = ram_[0] + 0x380 + 2 * s;
        const uint8_t* r2 = ram_[1] + 0x380 + 2 * s;
        const uint8_t* r3 = ram_[2] + 0x380 + 2 * s;
        uint8_t code = r1[0];
        const bool big = code & 0x80;
        uint8_t flags = r3[0] & (namco_galaga::kFlipX | namco_galaga::kFlipY);
        if (big) {
            code = uint8_t((code & 0xc0) | ((code & 0x3f) << 2));
            flags |= namco_galaga::kWide | namco_galaga::kTall;
        }
        const int x = (r2[1] - 40 + 1) & 0xff;
        int y = 256 - r2[0] + 1 - (big ? 16 : 0);
        y = (y & 0xff) - 32;
        vs.sprite[vs.sprite_count++] = {int16_t(x), int16_t(y), code, uint8_t(r1[1] & 0x3f), flags};
    }
    namco_galaga::sprite_bands(vs.sprite, vs.sprite_count, 256, sprite_blank_, 256, vs.sprite_bands);

    // The text characters that aren't blank (render_line skips the rest).
    for (int row = 0; row < kScreenH / 8; ++row) {
        int off, step;
        namco_galaga::tile_row(row, off, step);
        uint32_t cols = 0;
        for (int col = 0; col < 28; ++col, off += step)
            if (!text_blank_[vs.vram[off] & 0x7f]) cols |= 1u << col;
        vs.text_cols[row] = cols;
    }
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // The playfield: the map ROM picks the tile and its colour (top 4
    // bits); "off" still draws it, in colour 15 (black).
    const int picture = vs.latch & 3;
    const int bank = vs.latch & 0x30;
    const bool off = vs.latch & 0x08;
    int off_v, step;
    namco_galaga::tile_row(y >> 3, off_v, step);
    const int ty = y & 7;
    const uint8_t* map = roms_.map + (picture << 10);
    int o = off_v;
    for (int col = 0; col < 28; ++col, o += step) {
        const uint8_t code = map[o];
        const int colour = (off ? 0x0f : code >> 4) | bank;
        const uint16_t bits = tile_rows_[code * 8 + ty];
        namco_galaga::draw_tile_row_opaque(bits, tile_pen_ + colour * 4, out + col * 8);
    }

    // The text layer: pixel 1 in the character's colour (from its code).
    const bool direct = vs.latch & 0x04;
    for (uint32_t cols = vs.text_cols[y >> 3]; cols; cols &= cols - 1) {
        const int col = __builtin_ctz(cols);
        const uint8_t code = vs.vram[off_v + col * step];
        const uint8_t bits = text_rows_[(code & 0x7f) * 8 + ty];
        if (!bits) continue;
        const int colour = direct ? code & 0x0f : ((code >> 4) & 0x0e) | ((code >> 3) & 2);
        const uint16_t pen = text_pen_[colour];
        uint16_t* p = out + col * 8;
        for (unsigned b = bits; b; b &= b - 1) p[__builtin_ctz(b)] = pen;
    }

    // Sprites on top; x wraps at 256, so a sprite near the edge shows on
    // both sides.
    if (y < kSpriteTop || y >= kSpriteBottom) return;
    for (uint64_t m = vs.sprite_bands[y >> 4]; m; m &= m - 1) {
        const namco_galaga::Sprite& sp = vs.sprite[__builtin_ctzll(m)];
        const uint16_t* pens = sprite_pen_ + sp.colour * 4;
        const uint8_t opaque = sprite_opaque_[sp.colour];
        // The one wrap that reaches this line (a sprite is at most 32 lines
        // tall, so only one can).
        const int h = (sp.flags & namco_galaga::kWide) ? 32 : 16;
        const int d = y - sp.x;
        const int wrap = d >= 256 ? 256 : d < 0 ? -256 : 0;
        if (d - wrap < h) namco_galaga::draw_sprite_line(sp, y - wrap, sprite_rows_, 256, pens, opaque, out);
    }
}

} // namespace digdug
