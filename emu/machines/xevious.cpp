// Xevious machine - see xevious.h.
//
// Xevious' part of the memory map (the rest: namco_galaga_common.cpp), the
// same on all three CPUs:
//   6800-6807  read: bit 0 = DIP bank B bit (A2-A0), bit 1 = bank A's
//   7800-7FFF  RAM                         8000-87FF  RAM (+ 8800 mirror)
//   9000-97FF  RAM (+ 9800 mirror)         A000-A7FF  RAM (+ A800 mirror)
//              sprite registers at 8780 (y, x), 9780 (flags, x bit 8),
//              A780 (code, colour / off)
//   B000-B7FF  characters' attributes      B800-BFFF  background attributes
//   C000-C7FF  characters' codes           C800-CFFF  background codes
//   D000-DFFF  write: A6-A4 = register (0 background x scroll, 1
//              characters' x, 2 background y, 3 characters' y, 7 flip
//              screen), data = the low 8 bits, A0 = bit 8
//   F000-FFFF  write: A0 = BS0 / BS1 (the map ROM address)
//              read: A0 = BB0 / BB1 (the map ROMs' answer)
//   06XX chips: 0 = 51XX, 2 = 50XX, 3 = 54XX
#include "xevious.h"

#include <cstring>

namespace xevious {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x4000},
    {"sub",     0x2000},
    {"sub2",    0x1000},
    {"gfx1",    0x1000},
    {"gfx2",    0x2000},
    {"gfx3",    0x7000},
    {"gfx4",    0x4000},
    {"proms",   0x0b00},
    {"namco",   0x0200},
};

// CRCs as in MAME's "xevious" set (Namco; Tom's zip names the PROMs
// xvi_8bpr.6a ...). The PAL on the video board (xvi-3.1f) and the custom
// chips' own programs (50xx / 51xx / 54xx.bin) aren't needed.
constexpr romset::Rom kRoms[] = {
    {kRegionMain,    0x0000, 0x1000, 0x09964dda, "xvi_1.3p", false},
    {kRegionMain,    0x1000, 0x1000, 0x60ecce84, "xvi_2.3m", false},
    {kRegionMain,    0x2000, 0x1000, 0x79754b7d, "xvi_3.2m", false},
    {kRegionMain,    0x3000, 0x1000, 0xc7d4bbf0, "xvi_4.2l", false},
    {kRegionSub,     0x0000, 0x1000, 0xc85b703f, "xvi_5.3f", false},
    {kRegionSub,     0x1000, 0x1000, 0xe18cdaad, "xvi_6.3j", false},
    {kRegionSound,   0x0000, 0x1000, 0xdd35cf1c, "xvi_7.2c", false},
    {kRegionChars,   0x0000, 0x1000, 0x088c8b26, "xvi_12.3b", false},
    {kRegionTiles,   0x0000, 0x1000, 0xde60ba25, "xvi_13.3c", false},
    {kRegionTiles,   0x1000, 0x1000, 0x535cdbbc, "xvi_14.3d", false},
    {kRegionSprites, 0x0000, 0x2000, 0xdc2c0ecb, "xvi_15.4m", false},
    {kRegionSprites, 0x2000, 0x2000, 0xdfb587ce, "xvi_17.4p", false},
    {kRegionSprites, 0x4000, 0x1000, 0x605ca889, "xvi_16.4n", false},
    {kRegionSprites, 0x5000, 0x2000, 0x02417d19, "xvi_18.4r", false},
    {kRegionMap,     0x0000, 0x1000, 0x57ed9879, "xvi_9.2a", false},
    {kRegionMap,     0x1000, 0x2000, 0xae3ba9e5, "xvi_10.2b", false},
    {kRegionMap,     0x3000, 0x1000, 0x31e244dd, "xvi_11.2c", false},
    {kRegionProms,   0x0000, 0x0100, 0x5cc2727f, "xvi-8.6a", false},
    {kRegionProms,   0x0100, 0x0100, 0x5c8796cc, "xvi-9.6d", false},
    {kRegionProms,   0x0200, 0x0100, 0x3cb60975, "xvi-10.6e", false},
    {kRegionProms,   0x0300, 0x0200, 0x22d98032, "xvi-7.4h", false},
    {kRegionProms,   0x0500, 0x0200, 0x3a7599f0, "xvi-6.4f", false},
    {kRegionProms,   0x0700, 0x0200, 0xfd8b9d91, "xvi-4.3l", false},
    {kRegionProms,   0x0900, 0x0200, 0xbf906d82, "xvi-5.3m", false},
    {kRegionWave,    0x0000, 0x0100, 0x550f06bc, "xvi-2.7n", false},
    {kRegionWave,    0x0100, 0x0100, 0x77245b66, "xvi-1.5n", true},     // timing, unused
};

// The palette PROMs' 4 bits a gun: 2.2K / 1K / 470 / 220 ohm.
int gun(uint8_t v) { return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1); }

// The 8 2-bit pixels of a tile column in the other order (y flip).
inline uint16_t reverse2(uint32_t v)
{
    v = ((v & 0x3333) << 2) | ((v >> 2) & 0x3333);
    v = ((v & 0x0f0f) << 4) | ((v >> 4) & 0x0f0f);
    return uint16_t((v << 8) | (v >> 8));
}
inline uint8_t reverse1(uint32_t v)
{
    v = ((v & 0x55) << 1) | ((v >> 1) & 0x55);
    v = ((v & 0x33) << 2) | ((v >> 2) & 0x33);
    return uint8_t((v << 4) | (v >> 4));
}

// The picture: hardware x 0-287 (display lines), y 0-223 (display x 223 -
// y). The layers' offsets against the scroll registers (MAME's).
constexpr int kBgDx = 20, kBgDy = 16, kFgDx = 32, kFgDy = 18;

// Idle loops (see namco_galaga::Speedup), found by profiling and read in
// the program, all skipped as exact passes (Speedup::pass_cycles) and
// checked against runs without them (arcade_host --hash: every frame's
// picture and sound the same; xevious_tests: every register):
//   sound 0098-0099  JR 0098: the sound CPU's main loop (its work is in the
//                    NMIs; 12 T-states, 1 fetch)
//   main 0437-043C   LD HL,(8000) / DEC L / JR NZ,0437: waits for the IRQ
//                    handler to make 8000 1 (32 T-states, 3 fetches)
//   sub 018D-0190    LD A,(HL) / DEC A / JR NZ,018D with HL = 8002: the same
//                    for 8002 (23 T-states, 3 fetches)
using namco_galaga::Idle;
Idle main_wait(namco_galaga::Board& b, uint16_t pc)
{
    if (pc != 0x0437) return Idle::kStep;
    return b.peek(0x8000) != 1 ? Idle::kYes : Idle::kNo;
}
Idle sub_wait(namco_galaga::Board& b, uint16_t pc)
{
    if (pc != 0x018d || b.cpu(namco_galaga::kSub).hl.w != 0x8002) return Idle::kStep;
    return b.peek(0x8002) != 1 ? Idle::kYes : Idle::kNo;
}

Idle sound_wait(namco_galaga::Board&, uint16_t pc) { return pc == 0x0098 ? Idle::kYes : Idle::kStep; }

constexpr namco_galaga::Speedup kSpeedups[] = {
    {namco_galaga::kSound, 0x0098, 0x0099, sound_wait, 12, 12, 1},
    {namco_galaga::kMain, 0x0437, 0x043c, main_wait, 10, 32, 3},
    {namco_galaga::kSub, 0x018d, 0x0190, sub_wait, 8, 23, 3},
};

// The WSG's three voices at full volume would peak ~30,600 (Xevious keeps
// its music going all through play): scaled to 3/4.
constexpr int kWsgScale = 3;    // / 4

} // namespace

const romset::Set kRomSet = {
    "xevious", nullptr, "Xevious (Namco)", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
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
    noise_.init(audio_rate);

    // Colours: 128 palette entries (red, green, blue PROMs); the
    // background through its lookup (low / high nibble PROMs, 4 per
    // colour), the sprites through theirs (8 per colour; bit 7 clear =
    // transparent), the characters straight (pixel 1 = entry = colour).
    uint16_t pal[128];
    for (int i = 0; i < 128; ++i)
        pal[i] = namco_galaga::rgb565(gun(roms.proms[i]), gun(roms.proms[0x100 + i]), gun(roms.proms[0x200 + i]),
                                      swap);
    // Background attributes (and code bit 7) -> pens, flips (see bg_attr_).
    for (int i = 0; i < 512; ++i) {
        const int attr = i >> 1, colour = ((attr & 0x3c) >> 2) | ((i & 1) << 4) | ((attr & 0x03) << 5);
        bg_attr_[i] = uint16_t(colour * 4 | ((attr & 0x40) ? 7 << 12 : 0) | ((attr & 0x80) ? 0x8000 : 0));
    }
    for (int i = 0; i < 128 * 4; ++i)
        tile_pen_[i] = pal[((roms.proms[0x300 + i] & 0x0f) | ((roms.proms[0x500 + i] & 0x0f) << 4)) & 0x7f];
    for (int c = 0; c < 64; ++c) {
        sprite_opaque_[c] = 0;
        for (int p = 0; p < 8; ++p) {
            const int i = c * 8 + p;
            const int v = (roms.proms[0x700 + i] & 0x0f) | ((roms.proms[0x900 + i] & 0x0f) << 4);
            sprite_pen_[i] = pal[v & 0x7f];
            if (v & 0x80) sprite_opaque_[c] |= uint8_t(1 << p);
        }
        char_pen_[c] = pal[c];
    }

    // Tiles and characters by column: byte = hardware row y, bit 7 - x =
    // pixel x (two planes of 4 KB for the tiles, the first the high bit).
    for (int code = 0; code < 512; ++code)
        for (int x = 0; x < 8; ++x) {
            uint16_t bits = 0;
            for (int y = 0; y < 8; ++y) {
                const int p0 = (roms.tiles[code * 8 + y] >> (7 - x)) & 1;
                const int p1 = (roms.tiles[0x1000 + code * 8 + y] >> (7 - x)) & 1;
                bits |= uint16_t(((p0 << 1) | p1) << (2 * (7 - y)));
            }
            tile_cols_[code * 8 + x] = bits;
        }
    for (int code = 0; code < 256; ++code)
        for (int x = 0; x < 8; ++x) {
            uint8_t bits = 0;
            for (int y = 0; y < 8; ++y)
                if ((roms.chars[code * 8 + y] >> (7 - x)) & 1) bits |= uint8_t(1 << (7 - y));
            char_cols_[code * 8 + x] = bits;
        }
    for (int code = 0; code < 256; ++code) {
        uint8_t any = 0;
        for (int x = 0; x < 8; ++x) any |= char_cols_[code * 8 + x];
        char_blank_[code] = any == 0;
    }

    chip50_.reset();
    chip51_.set_xevious(true);
    board_init(b, audio_rate, kSpeedups, int(sizeof kSpeedups / sizeof kSpeedups[0]));
    // Xevious' RAM (board_init mapped Galaga's): the board's own RAM 1-2
    // and video RAM (4 KB) serve 7800-7FFF and 8000-87FF; A11 isn't
    // decoded above 8000.
    map_shared_ram(0x7800, 0x400, ram_[0]);
    map_shared_ram(0x7c00, 0x400, ram_[1]);
    map_shared_ram(0x8000, 0x800, vram_);
    map_shared_ram(0x8800, 0x800, vram_);
    map_shared_ram(0x9000, 0x800, ram2_);
    map_shared_ram(0x9800, 0x800, ram2_);
    map_shared_ram(0xa000, 0x800, ram3_);
    map_shared_ram(0xa800, 0x800, ram3_);
    map_shared_ram(0xb000, 0x2000, vram_x_);
    std::memset(ram2_, 0, sizeof ram2_);
    std::memset(ram3_, 0, sizeof ram3_);
    std::memset(vram_x_, 0, sizeof vram_x_);
    reset();
}

void Machine::reset()
{
    board_reset();
    bs_[0] = bs_[1] = 0;
    for (uint16_t& s : scroll_) s = 0;
}

void Machine::chips_reset()
{
    noise_.reset();
    chip50_.reset();
}

void Machine::control_written(uint8_t v)
{
    if (v & 0x04) chip50_.select();
}

uint8_t Machine::chip_read(int chip) { return chip == 2 ? chip50_.read() : 0xff; }

void Machine::chip_write(int chip, uint8_t v)
{
    if (chip == 2) {
        chip50_.write(v);
    } else if (chip == 3) {
        audio_to(now(namco_galaga::kMain));
        noise_.write(v);
    }
}

void Machine::mix(int16_t* out, int n)
{
    for (int i = 0; i < n; ++i) out[i] = int16_t(out[i] * kWsgScale / 4);
    noise_.mix(out, n);
}

uint8_t Machine::map_read(int n) const
{
    // xvi_10 (2B) holds 8 bits of a 12-bit map entry, xvi_9 (2A) the other
    // 4 (two entries a byte); the entry picks a 2 x 2 block in xvi_11 (2C)
    // and flips it (bit 10 x, bit 9 y).
    const uint8_t* rom2a = roms_.map;
    const uint8_t* rom2b = roms_.map + 0x1000;
    const uint8_t* rom2c = roms_.map + 0x3000;
    const int a2b = ((bs_[1] & 0x7e) << 6) | ((bs_[0] & 0xfe) >> 1);
    const uint8_t nib = rom2a[a2b >> 1];
    const int entry = ((a2b & 1) ? (nib & 0xf0) << 4 : (nib & 0x0f) << 8) | rom2b[a2b];
    int a2c = ((entry & 0x1ff) << 2) | ((bs_[1] & 1) << 1) | (bs_[0] & 1);
    if (entry & 0x400) a2c ^= 1;
    if (entry & 0x200) a2c ^= 2;
    if (n & 1) return rom2c[a2c | 0x800];
    uint8_t v = rom2c[a2c];
    v = uint8_t((v & 0x3f) | ((v & 0x40) << 1) | ((v & 0x80) >> 1));   // bits 6 and 7 swapped
    if (entry & 0x400) v ^= 0x40;
    if (entry & 0x200) v ^= 0x80;
    return v;
}

uint8_t Machine::io_read(int, uint16_t a)
{
    if ((a & 0xf830) == 0x6800) {
        const int bit = a & 7;
        const uint8_t b = uint8_t(blaster_ ? dips_b_ & ~1 : dips_b_ | 1);
        return uint8_t(((b >> bit) & 1) | (((dips_a_ >> bit) & 1) << 1));
    }
    if (a >= 0xf000) return map_read(a & 1);
    return 0xff;
}

void Machine::io_write(int, uint16_t a, uint8_t v)
{
    if ((a & 0xf000) == 0xd000) {
        const int reg = (a >> 4) & 7;
        if (reg < 4) scroll_[reg] = uint16_t(v | ((a & 1) << 8));
        return;
    }
    if (a >= 0xf000) bs_[a & 1] = v;
}

void Machine::vblank()
{
    VideoState& vs = video_;
    std::memcpy(vs.fg_attr, vram_x_, 0x800);
    std::memcpy(vs.bg_attr, vram_x_ + 0x800, 0x800);
    std::memcpy(vs.fg_code, vram_x_ + 0x1000, 0x800);
    std::memcpy(vs.bg_code, vram_x_ + 0x1800, 0x800);
    for (int c = 0; c < 64; ++c) vs.fg_rows[c] = 0;
    for (int i = 0; i < 0x800; ++i)
        if (!char_blank_[vs.fg_code[i]]) vs.fg_rows[i & 63] |= 1u << (i >> 6);
    vs.bg_x = scroll_[0];
    vs.fg_x = scroll_[1];
    vs.bg_y = scroll_[2];
    vs.fg_y = scroll_[3];

    // Sprites: code at A780, colour / off at A781, y at 8780, x at 8781
    // (bit 8 at 9781), flags at 9780: 0 double width, 1 double height, 2
    // flip x, 3 flip y, 7 = codes 256-319.
    vs.sprite_count = 0;
    for (int b = 0; b < namco_galaga::kBands; ++b) vs.sprite_bands[b] = 0;
    for (int s = 0; s < kSprites; ++s) {
        const uint8_t* r1 = vram_ + 0x780 + 2 * s;
        const uint8_t* r2 = ram2_ + 0x780 + 2 * s;
        const uint8_t* r3 = ram3_ + 0x780 + 2 * s;
        if (r3[1] & 0x40) continue;
        const uint8_t f = r2[0];
        const bool wide = f & 1, tall = f & 2, fx = f & 4, fy = f & 8;
        int code = (f & 0x80) ? 0x100 + (r3[0] & 0x3f) : r3[0];
        if (wide && tall) code &= ~3;
        else if (tall)    code &= ~2;
        else if (wide)    code &= ~1;
        const int x = r1[1] - 40 + 0x100 * (r2[1] & 1);
        const int sy = 28 * 8 - r1[0] - 1;
        // The top: a double-height sprite reaches 16 up; so does a flipped
        // single-height one (its one row is drawn there).
        const int y = (tall || fy) ? sy - 16 : sy;
        const int w = wide ? 32 : 16;
        if (x >= kScreenH || x + w <= 0 || y >= kScreenW || y + (tall ? 32 : 16) <= 0) continue;
        uint8_t flags = 0;
        if (fx) flags |= namco_galaga::kFlipX;
        if (fy) flags |= namco_galaga::kFlipY;
        if (wide) flags |= namco_galaga::kWide;
        if (tall) flags |= namco_galaga::kTall;
        const int n = vs.sprite_count++;
        vs.sprite[n] = {int16_t(x), int16_t(y), uint16_t(code), uint8_t(r3[1] & 0x3f), flags};
        const int lo = x < 0 ? 0 : x, hi = x + w - 1 >= kScreenH ? kScreenH - 1 : x + w - 1;
        for (int band = lo >> 4; band <= hi >> 4; ++band) vs.sprite_bands[band] |= uint64_t(1) << n;
    }
}

void Machine::draw_sprite(const Sprite& s, int line, uint16_t* out) const
{
    const bool wide = s.flags & namco_galaga::kWide, tall = s.flags & namco_galaga::kTall;
    const bool fx = s.flags & namco_galaga::kFlipX, fy = s.flags & namco_galaga::kFlipY;
    const int dx = line - s.x;
    if (dx < 0 || dx >= (wide ? 32 : 16)) return;
    const int tx = (dx >> 4) ^ (wide && fx ? 1 : 0);
    const int col = fx ? 15 - (dx & 15) : dx & 15;
    const int g = (col >> 2) * 8, b = col & 3;
    const uint16_t* pens = sprite_pen_ + s.colour * 8;
    const uint8_t opaque = sprite_opaque_[s.colour];
    for (int ty = 0; ty <= (tall ? 1 : 0); ++ty) {
        const int code = s.code + tx + 2 * (ty ^ (tall && fy ? 1 : 0));
        // Planes 0-1 (the low two bits) in xvi_15 / 17 / 16, plane 2 in
        // xvi_18: codes 0-127 its low nibbles, 128-255 its high ones,
        // none for 256-319.
        const uint8_t* p01 = roms_.sprites + code * 64;
        const uint8_t* p2 = code < 256 ? roms_.sprites + 0x5000 + (code & 127) * 64 : nullptr;
        const int sh2 = (code & 128) ? 7 - b : 3 - b;
        const int top = s.y + 16 * ty;
        for (int r = 0; r < 16; ++r) {
            const int hy = top + r;
            if (hy < 0 || hy >= kScreenW) continue;
            const int row = fy ? 15 - r : r;
            const int k = g + (row < 8 ? row : row + 24);
            const uint8_t v = p01[k];
            int p = (((v >> (7 - b)) & 1) << 1) | ((v >> (3 - b)) & 1);
            if (p2) p |= ((p2[k] >> sh2) & 1) << 2;
            if (opaque >> p & 1) out[kScreenW - 1 - hy] = pens[p];
        }
    }
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // The background: one tile column of the 512 x 256 map per line;
    // display x runs down the hardware y (223 - x): a part of the first
    // tile, whole tiles, a part of the last.
    {
        const int sx = (y + kBgDx + vs.bg_x) & 511;
        const int tc = sx >> 3, px = sx & 7;
        const int sy = (kScreenW - 1 + kBgDy + vs.bg_y) & 255;
        int tr = sy >> 3;
        // The column of the tile at row tr, its pixels from hardware y 7
        // (bits 1-0) to 0, and its pens.
        auto column = [&](int row, const uint16_t*& pens) -> uint32_t {
            const int idx = row * 64 + tc;
            const unsigned code = vs.bg_code[idx], attr = vs.bg_attr[idx];
            const unsigned l = bg_attr_[(attr << 1) | (code >> 7)];
            uint32_t bits = tile_cols_[((code | (attr & 1) << 8) << 3) | (px ^ (l >> 12 & 7))];
            if (l & 0x8000) bits = reverse2(bits);
            pens = tile_pen_ + (l & 0x1ff);
            return bits;
        };
        const uint16_t* pens;
        const int first = (sy & 7) + 1;                 // pixels of the first tile
        uint32_t bits = column(tr, pens) >> (2 * (8 - first));
        for (int i = 0; i < first; ++i, bits >>= 2) out[i] = pens[bits & 3];
        int d = first;
        for (; d + 8 <= kScreenW; d += 8) {
            tr = (tr - 1) & 31;
            const uint32_t b = column(tr, pens);       // sets pens first
            namco_galaga::draw_tile_row_opaque(b, pens, out + d);
        }
        if (d < kScreenW) {
            tr = (tr - 1) & 31;
            bits = column(tr, pens);
            for (; d < kScreenW; ++d, bits >>= 2) out[d] = pens[bits & 3];
        }
    }

    // Sprites, later ones on top.
    for (uint64_t m = vs.sprite_bands[y >> 4]; m; m &= m - 1) draw_sprite(vs.sprite[__builtin_ctzll(m)], y, out);

    // The characters on top (pixel 0 transparent): only the tile rows of
    // this column that hold a character with pixels (VideoState::fg_rows).
    // Tile row r covers hardware y 8r..8r+7; y 7 - j (bit j of a column)
    // falls on display x top + j.
    const int sx = (y + kFgDx + vs.fg_x) & 511;
    const int tc = sx >> 3, px = sx & 7;
    for (uint32_t rows = vs.fg_rows[tc]; rows; rows &= rows - 1) {
        const int tr = __builtin_ctz(rows);
        const int idx = tr * 64 + tc;
        const uint8_t attr = vs.fg_attr[idx];
        uint32_t bits = char_cols_[vs.fg_code[idx] * 8 + ((attr & 0x40) ? 7 - px : px)];
        if (!bits) continue;
        if (attr & 0x80) bits = reverse1(bits);
        const int top = (kScreenW - 1 + kFgDy + vs.fg_y - (8 * tr + 7)) & 255;
        const uint16_t pen = char_pen_[((attr & 3) << 4) | ((attr & 0x3c) >> 2)];
        if (top <= kScreenW - 8) {
            uint16_t* o = out + top;
            for (; bits; bits &= bits - 1) o[__builtin_ctz(bits)] = pen;
        } else {
            for (; bits; bits &= bits - 1) {
                const int d = (top + __builtin_ctz(bits)) & 255;
                if (d < kScreenW) out[d] = pen;
            }
        }
    }
}

} // namespace xevious
