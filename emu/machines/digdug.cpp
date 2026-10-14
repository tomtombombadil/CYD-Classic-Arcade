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
    namco_galaga::decode_chars_2bpp(roms.tiles, 256, tile_rows_);
    namco_galaga::decode_sprites(roms.sprites, 256, sprite_rows_);

    std::memset(earom_, 0xff, sizeof earom_);
    chip53_.set_dips(dips_a_, dips_b_);
    board_init(b, audio_rate, nullptr, 0);
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
        const uint16_t* pens = tile_pen_ + colour * 4;
        uint16_t* p = out + col * 8;
        for (int x = 0; x < 8; ++x) p[x] = pens[(bits >> (2 * x)) & 3];
    }

    // The text layer: pixel 1 in the character's colour (from its code).
    const bool direct = vs.latch & 0x04;
    o = off_v;
    for (int col = 0; col < 28; ++col, o += step) {
        const uint8_t code = vs.vram[o];
        const uint8_t bits = text_rows_[(code & 0x7f) * 8 + ty];
        if (!bits) continue;
        const int colour = direct ? code & 0x0f : ((code >> 4) & 0x0e) | ((code >> 3) & 2);
        const uint16_t pen = text_pen_[colour];
        uint16_t* p = out + col * 8;
        for (int x = 0; x < 8; ++x)
            if (bits & (1 << x)) p[x] = pen;
    }

    // Sprites on top; x wraps at 256, so a sprite near the edge shows on
    // both sides.
    if (y < kSpriteTop || y >= kSpriteBottom) return;
    for (int s = 0; s < vs.sprite_count; ++s) {
        const namco_galaga::Sprite& sp = vs.sprite[s];
        const uint16_t* pens = sprite_pen_ + sp.colour * 4;
        const uint8_t opaque = sprite_opaque_[sp.colour];
        for (int wrap = -256; wrap <= 256; wrap += 256)
            namco_galaga::draw_sprite_line(sp, y - wrap, sprite_rows_, 256, pens, opaque, out);
    }
}

} // namespace digdug
