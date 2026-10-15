// Mr. Do! machine - see mrdo.h.
//
// Memory map:
//   0000-7FFF  program ROM
//   8000-83FF  back layer attributes     8400-87FF  back layer tile codes
//   8800-8BFF  front layer attributes    8C00-8FFF  front layer tile codes
//              attribute: bits 0-5 colour, 6 the tile covers what's behind
//              it (pixel value 0 drawn too), 7 tile code bit 8
//   9000-90FF  write: sprites, 4 bytes each: code, 256 - y (0 = off),
//              bits 0-3 colour / 4 flip x / 5 flip y, x
//   9800       write: bit 0 flip screen (cocktail; bits 1-3 layer priority,
//              unused by the game)
//   9801 9802  write: the two SN76489s
//   9803       read: the protection PAL (below)
//   A000 A001  read: player 1 / player 2 + coins   A002 A003  read: DIPs
//   E000-EFFF  RAM
//   F000-F7FF  write: back layer scroll X   F800-FFFF  write: scroll Y
//
// Protection: a PAL (U001) latches what the game writes into the front
// layer's RAM and answers at 9803. The game writes a byte of a table to the
// front layer, steps HL to the next table byte, reads 9803 and compares bits
// 1-6 of the answer with (HL) - a mismatch stops it. Its equations aren't
// known well enough to model (its outputs feed back into it), so 9803 reads
// the program ROM byte HL points at, which is the answer the game checks for.
#include "mrdo.h"

#include <cmath>
#include <cstring>

namespace mrdo {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x8000},
    {"gfx1",    0x2000},
    {"gfx2",    0x2000},
    {"gfx3",    0x2000},
    {"proms",   0x0080},
};

// CRCs as in MAME's "mrdo" set.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,     0x0000, 0x2000, 0x03dcfba2, "a4-01.bin", false},
    {kRegionCpu,     0x2000, 0x2000, 0x0ecdd39c, "c4-02.bin", false},
    {kRegionCpu,     0x4000, 0x2000, 0x358f5dc2, "e4-03.bin", false},
    {kRegionCpu,     0x6000, 0x2000, 0xf4190cfc, "f4-04.bin", false},
    {kRegionFg,      0x0000, 0x1000, 0xaa80c5b6, "s8-09.bin", false},
    {kRegionFg,      0x1000, 0x1000, 0xd20ec85b, "u8-10.bin", false},
    {kRegionBg,      0x0000, 0x1000, 0xdbdc9ffa, "r8-08.bin", false},
    {kRegionBg,      0x1000, 0x1000, 0x4b9973db, "n8-07.bin", false},
    {kRegionSprites, 0x0000, 0x1000, 0xe1218cc5, "h5-05.bin", false},
    {kRegionSprites, 0x1000, 0x1000, 0xb1f68b04, "k5-06.bin", false},
    {kRegionProms,   0x0000, 0x0020, 0x238a65d7, "u02--2.bin", false},  // palette, high bits
    {kRegionProms,   0x0020, 0x0020, 0xae263dc0, "t02--3.bin", false},  // palette, low bits
    {kRegionProms,   0x0040, 0x0020, 0x16ee4ca2, "f10--1.bin", false},  // sprite colour lookup
    {kRegionProms,   0x0060, 0x0020, 0xff7fe284, "j10--4.bin", true},   // timing, unused
};

// One channel's level at full volume: eight channels, and the game seldom
// plays more than four loud at once.
constexpr int kChannelLevel = 5800;

uint16_t rgb565(int r, int g, int b, bool swap)
{
    uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return swap ? uint16_t((px >> 8) | (px << 8)) : px;
}

} // namespace

const romset::Set kRomSet = {
    "mrdo", nullptr, "Mr. Do!", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

// Each gun is driven through a resistor and a diode per PROM bit into a
// 220 ohm load: the drive divides as 220 / (220 + R) (R = the bits'
// resistors in parallel), less the diode's 0.7, scaled so all four bits on
// = 255. (The circuit as MAME models it.)
uint8_t level(int bits)
{
    static constexpr double kOhms[4] = {150, 120, 100, 75};
    auto volts = [](int b) {
        double g = 0;
        for (int i = 0; i < 4; ++i)
            if (b & (1 << i)) g += 1.0 / kOhms[i];
        if (g == 0) return 0.0;
        return 220.0 / (220.0 + 1.0 / g) - 0.7;
    };
    const long v = std::lround(255 * volts(bits) / volts(15));
    return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
}

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = port_read;
    cpu_.port_out = port_write;
    cpu_.map_read(0x0000, 0x8000, roms.cpu);
    cpu_.map_ram(0x8000, 0x800, bg_ram_);
    cpu_.map_ram(0x8800, 0x800, fg_ram_);
    cpu_.map_ram(0xe000, 0x1000, ram_);

    for (sn76489::Chip& c : sn_) c.init(kSoundClock, audio_rate, kChannelLevel);

    // Palette: 256 entries (64 colours x 4 pixel values). Entry i takes its
    // high bits from the U2 PROM at (i bits 2-4, 0-1) and its low bits from
    // T2 at (i bits 5-7, 0-1); each PROM byte holds 2 bits per gun (red
    // bits 0-1, green 2-3, blue 4-5).
    const uint8_t* hi = roms.proms;
    const uint8_t* lo = roms.proms + 0x20;
    uint16_t pal[256];
    for (int i = 0; i < 256; ++i) {
        const uint8_t h = hi[(i & 0x1c) | (i & 3)];
        const uint8_t l = lo[((i >> 3) & 0x1c) | (i & 3)];
        int c[3];
        for (int g = 0; g < 3; ++g) c[g] = level(((l >> (2 * g)) & 3) | (((h >> (2 * g)) & 3) << 2));
        pal[i] = rgb565(c[0], c[1], c[2], swap_rgb565_bytes);
    }
    for (int i = 0; i < 256; ++i) tile_pen_[i] = pal[i];
    // Sprites: 16 colours x 4 pixel values through the lookup PROM (colours
    // 0-7 its low nibbles, 8-15 its high ones). An entry's bits 0-3 pick
    // palette bits 0-3, and its bits 2-3 palette bits 5-6 as well.
    const uint8_t* lut = roms.proms + 0x40;
    for (int i = 0; i < 64; ++i) {
        const uint8_t e = (i & 0x20) ? uint8_t(lut[i & 0x1f] >> 4) : uint8_t(lut[i & 0x1f] & 0x0f);
        sprite_pen_[i] = pal[e | ((e & 0x0c) << 3)];
    }

    decode_gfx();
    reset();
}

// Tiles: 8 bytes each (byte = board row y, bit x = pixel x), bit plane 1
// (the pixel value's high bit) in the region's first half. Sprites: 64
// bytes, 4 a row; byte (row, x / 4) holds the high bits of its 4 pixels in
// bits 0-3 and the low bits in bits 4-7.
void Machine::decode_gfx()
{
    auto tiles = [](const uint8_t* src, uint16_t* cols) {
        for (int code = 0; code < 512; ++code) {
            for (int x = 0; x < 8; ++x) {
                uint16_t bits = 0;
                for (int y = 0; y < 8; ++y) {
                    const int hi = (src[code * 8 + y] >> x) & 1;
                    const int lo = (src[0x1000 + code * 8 + y] >> x) & 1;
                    bits |= uint16_t(((hi << 1) | lo) << (2 * y));
                }
                cols[code * 8 + x] = bits;
            }
        }
    };
    tiles(roms_.fg, fg_cols_);
    tiles(roms_.bg, bg_cols_);
    for (int code = 0; code < 128; ++code) {
        for (int x = 0; x < 16; ++x) {
            uint32_t bits = 0;
            for (int y = 0; y < 16; ++y) {
                const uint8_t b = roms_.sprites[code * 64 + y * 4 + (x >> 2)];
                const int k = x & 3;
                const uint32_t v = uint32_t((((b >> k) & 1) << 1) | ((b >> (4 + k)) & 1));
                bits |= v << (2 * y);
            }
            sprite_cols_[code * 16 + x] = bits;
        }
    }
}

void Machine::reset()
{
    cpu_.reset();
    for (sn76489::Chip& c : sn_) c.reset();
    pos_ = 0;
    flip_ = 0;
    scroll_x_ = scroll_y_ = 0;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr) {
    case 0x9803: {
        ++m.protection_reads_;
        const uint16_t hl = m.cpu_.hl.w;
        return hl < 0x8000 ? m.roms_.cpu[hl] : 0xff;
    }
    case 0xa000: {
        uint8_t v = 0xff;
        if (m.buttons_ & kLeft)   v &= uint8_t(~0x01);
        if (m.buttons_ & kDown)   v &= uint8_t(~0x02);
        if (m.buttons_ & kRight)  v &= uint8_t(~0x04);
        if (m.buttons_ & kUp)     v &= uint8_t(~0x08);
        if (m.buttons_ & kFire)   v &= uint8_t(~0x10);
        if (m.buttons_ & kStart1) v &= uint8_t(~0x20);
        if (m.buttons_ & kStart2) v &= uint8_t(~0x40);
        if (m.buttons_ & kTilt)   v &= uint8_t(~0x80);
        return v;
    }
    case 0xa001: {                          // bits 0-4: player 2 (cocktail)
        uint8_t v = 0xff;
        if (m.buttons_ & kCoin)  v &= uint8_t(~0x40);
        if (m.buttons_ & kCoin2) v &= uint8_t(~0x80);
        return v;
    }
    case 0xa002: return m.dsw1_;
    case 0xa003: return kDsw2Default;
    default:     return 0xff;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0xf800) { m.scroll_y_ = v; return; }
    if (addr >= 0xf000) { m.scroll_x_ = v; return; }
    if (addr >= 0x9000 && addr < 0x9100) { m.sprite_ram_[addr & 0xff] = v; return; }
    switch (addr) {
    case 0x9800: m.flip_ = v & 1; break;
    case 0x9801:
    case 0x9802:
        m.sound_to(m.pos_ + m.cpu_.elapsed());
        m.sn_[addr - 0x9801].write(v);
        break;
    default: break;
    }
}

uint8_t Machine::port_read(void*, uint16_t) { return 0xff; }
void    Machine::port_write(void*, uint16_t, uint8_t) {}

// Sound up to CPU cycle `cycle` of this frame: both chips, mixed, through
// the amplifier's coupling capacitor (a high-pass, ~15 Hz at 24 kHz: the
// chips' outputs only go positive; assumed, not taken from the schematic).
void Machine::sound_to(int cycle)
{
    int target = int(int64_t(cycle) * frame_samples_ / kCyclesPerFrame);
    if (target > frame_samples_) target = frame_samples_;
    while (samples_done_ < target) {
        int32_t acc[64];
        const int n = target - samples_done_ < 64 ? target - samples_done_ : 64;
        std::memset(acc, 0, sizeof(int32_t) * size_t(n));
        sn_[0].mix(acc, n);
        sn_[1].mix(acc, n);
        for (int i = 0; i < n; ++i) {
            dc_ += ((int64_t(acc[i]) << 16) - dc_) >> 8;
            int32_t s = acc[i] - int32_t(dc_ >> 16);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            const int at = samples_done_ + i;
            if (audio_ && at < audio_cap_) audio_[at] = int16_t(s);
        }
        samples_done_ += n;
    }
}

// Runs the CPU in slices. Between interrupts the game waits in a jump to
// itself (JR $ at 0154, once its work for the frame is done); a CPU found
// there skips to `cycle` as if it had looped (R counts the loops), since
// only the next VBLANK gets it out.
void Machine::run_to(int cycle)
{
    while (pos_ < cycle) {
        const int left = cycle - pos_;
        pos_ += cpu_.run(left < kSlice ? left : kSlice);
        const uint16_t pc = cpu_.pc.w;
        if (pos_ < cycle && cpu_.rd(pc) == 0x18 && cpu_.rd(uint16_t(pc + 1)) == 0xfe) {
            const int loops = (cycle - pos_ + 11) / 12;    // JR taken: 12 T-states, one M1
            pos_ += loops * 12;
            cpu_.r = uint8_t(cpu_.r + loops);
            idle_cycles_ += uint32_t(loops * 12);
        }
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    frame_samples_ = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    audio_ = audio;
    audio_cap_ = cap;
    samples_done_ = 0;
    pos_ -= kCyclesPerFrame;    // the last frame's overrun (0 or a few cycles past)
    if (pos_ < 0) pos_ = 0;

    // VBLANK starts: the interrupt is held until the CPU takes it. While it
    // waits (interrupts off), the CPU runs an instruction at a time, so the
    // line drops as soon as it's taken (IM 1: to 0038).
    cpu_.irq(true, 0xff);
    while (pos_ < kCyclesPerFrame) {
        const bool ready = cpu_.iff1 != 0;
        pos_ += cpu_.run(1);
        if (ready && !cpu_.iff1 && cpu_.pc.w == 0x0038) break;
    }
    cpu_.irq(false);
    run_to(kCyclesPerFrame);
    sound_to(kCyclesPerFrame);
    audio_ = nullptr;
    list_sprites();
    ++frame_;
    return audio ? (frame_samples_ < cap ? frame_samples_ : cap) : 0;
}

// A layer's tile column `col`, unpacked from its RAM: renders 8 display
// lines in a row from one column, so this is done once for the eight.
void Machine::unpack_column(const uint8_t* vram, int col, Cell* cells) const
{
    for (int row = 0; row < 32; ++row) {
        const int idx = row * 32 + col;
        const uint8_t attr = vram[idx];
        cells[row].code = uint16_t((vram[0x400 + idx] | ((attr & 0x80) << 1)) * 8);
        cells[row].pens = uint8_t((attr & 0x3f) * 4);
        cells[row].cover = (attr & 0x40) != 0;
    }
}

// A layer's board column bx (0-255, scrolled), board rows 32-223 (+
// scroll) into display pixels 0-191. Pixel value 0 is see-through unless
// the tile's attribute bit 6 is set; the back layer (kBack) fills what
// shows through with palette entry 0, so every pixel is written once.
template <bool kBack>
void Machine::draw_layer(const Cell* cells, const uint16_t* cols, int tx, int scroll_y, uint16_t* out) const
{
    const int top = (32 + scroll_y) & 255;
    const uint16_t back = tile_pen_[0];
    int dx = -(top & 7);
    for (int row = top >> 3; dx < kScreenW; dx += 8, ++row) {
        const Cell& c = cells[row & 31];
        const uint32_t bits = cols[c.code + tx];
        const uint16_t* pens = tile_pen_ + c.pens;
        const bool whole = dx >= 0 && dx + 8 <= kScreenW;
        uint16_t* o = out + dx;
        if (kBack || c.cover) {
            const uint16_t pp[4] = {c.cover ? pens[0] : back, pens[1], pens[2], pens[3]};
            if (whole) {
                o[0] = pp[bits & 3];         o[1] = pp[(bits >> 2) & 3];
                o[2] = pp[(bits >> 4) & 3];  o[3] = pp[(bits >> 6) & 3];
                o[4] = pp[(bits >> 8) & 3];  o[5] = pp[(bits >> 10) & 3];
                o[6] = pp[(bits >> 12) & 3]; o[7] = pp[bits >> 14];
                continue;
            }
            for (int p = 0; p < 8; ++p)
                if (dx + p >= 0 && dx + p < kScreenW) o[p] = pp[(bits >> (2 * p)) & 3];
            continue;
        }
        if (!bits) continue;
        if (whole) {
            for (int p = 0; p < 8; ++p) {
                const int v = (bits >> (2 * p)) & 3;
                const uint16_t under = o[p];
                o[p] = v ? pens[v] : under;
            }
            continue;
        }
        for (int p = 0; p < 8; ++p) {
            const int v = (bits >> (2 * p)) & 3;
            if (v && dx + p >= 0 && dx + p < kScreenW) o[p] = pens[v];
        }
    }
}

// The sprites on screen, in drawing order (63 down to 0, so sprite 0 ends
// on top), from the sprite RAM at the start of VBLANK.
void Machine::list_sprites()
{
    sprite_count_ = 0;
    for (int s = 63; s >= 0; --s) {
        const uint8_t* sp = sprite_ram_ + 4 * s;
        if (!sp[1]) continue;
        sprites_[sprite_count_++] = {sp[3], uint8_t(sp[0] & 0x7f), sp[2], sp[1]};
    }
}

void Machine::render_line(int y, uint16_t* out) const
{
    // Display line y is board column 247 - y.
    const int bx = 247 - y;
    const int bgx = (bx + scroll_x_) & 255;
    if (bgx >> 3 != bg_col_ || frame_ != cells_frame_) unpack_column(bg_ram_, bg_col_ = bgx >> 3, bg_cells_);
    if (bx >> 3 != fg_col_ || frame_ != cells_frame_) unpack_column(fg_ram_, fg_col_ = bx >> 3, fg_cells_);
    cells_frame_ = frame_;
    draw_layer<true>(bg_cells_, bg_cols_, bgx & 7, scroll_y_, out);
    draw_layer<false>(fg_cells_, fg_cols_, bx & 7, 0, out);

    // Sprites: 16 x 16 at board (x, 256 - y).
    for (int i = 0; i < sprite_count_; ++i) {
        const Sprite& sp = sprites_[i];
        int px = bx - sp.x;
        if (unsigned(px) > 15) continue;
        if (sp.attr & 0x10) px = 15 - px;
        const uint32_t bits = sprite_cols_[sp.code * 16 + px];
        if (!bits) continue;
        const uint16_t* pens = sprite_pen_ + (sp.attr & 0x0f) * 4;
        const int dx0 = 256 - sp.y - 32;
        const bool flip_y = sp.attr & 0x20;
        for (int py = 0; py < 16; ++py) {
            const int dx = dx0 + py;
            if (dx < 0 || dx >= kScreenW) continue;
            const int v = int(bits >> (2 * (flip_y ? 15 - py : py))) & 3;
            if (v) out[dx] = pens[v];
        }
    }
}

} // namespace mrdo
