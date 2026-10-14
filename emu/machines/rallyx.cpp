// Rally-X machine - see rallyx.h.
//
// Memory map (A14 isn't decoded, so 4000-7FFF mirrors 0000-3FFF and
// C000-FFFF mirrors 8000-BFFF):
//   0000-3FFF  program ROM (1B, 1E, 1H, 1K)
//   8000-83FF  radar / score tile codes (columns 0-7 of each 32-byte row);
//              8014-801F sprite codes and X, 8034-803F radar dot X
//   8400-87FF  playfield tile codes (32 x 32)
//   8800-8BFF  radar / score tile attributes; 8814-881F sprite Y and
//              colour, 8834-883F radar dot Y
//   8C00-8FFF  playfield tile attributes
//   9800-9FFF  work RAM
//   A000-BFFF  I/O, A7-A8 pick the block:
//     read   A000 IN0, A080 IN1, A100 DSW
//     write  A000-A00F radar dot attributes (shape, X bit 8)
//            A080 watchdog reset
//            A100-A11F WSG registers, A130 scroll X, A140 scroll Y
//            A180-A187 74LS259 latch, bit = A0-A2: 0 BANG (crash sound),
//            1 IRQ enable, 2 sound on, 3 flip screen, 4-5 start lamps,
//            6 coin lockout, 7 coin counter
//   I/O port 00 write: the byte put on the bus by the interrupt (the game
//   runs in IM 2 and IM 0).
//
// Tile attribute: bits 0-5 colour (of 64, through the lookup PROM), bit 5
// also = the tile covers sprites, bit 6 clear = mirrored left-right, bit 7
// = upside down.
#include "rallyx.h"

#include <cstring>

namespace rallyx {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x4000},
    {"gfx1",    0x1000},
    {"gfx2",    0x0100},
    {"proms",   0x0160},
    {"namco",   0x0200},
};

// CRCs as in MAME's "rallyx" set (older sets name the PROMs im5623.* /
// m3-7603.*; matching is by CRC). The video layout and timing PROMs and
// the sound timing PROM aren't needed.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x1000, 0x5882700d, "1b", false},
    {kRegionCpu,   0x1000, 0x1000, 0xed1eba2b, "rallyxn.1e", false},
    {kRegionCpu,   0x2000, 0x1000, 0x4f98dd1c, "rallyxn.1h", false},
    {kRegionCpu,   0x3000, 0x1000, 0x9aacccf0, "rallyxn.1k", false},
    {kRegionGfx,   0x0000, 0x1000, 0x277c1de5, "8e", false},
    {kRegionDots,  0x0000, 0x0100, 0x3c16f62c, "rx1-6.8m", false},
    {kRegionProms, 0x0000, 0x0020, 0xc7865434, "rx1-1.11n", false},
    {kRegionProms, 0x0020, 0x0100, 0x834d4fda, "rx1-7.8p", false},
    {kRegionProms, 0x0120, 0x0020, 0x8f574815, "rx1-2.4n", true},    // video layout, unused
    {kRegionProms, 0x0140, 0x0020, 0xb8861096, "rx1-3.7k", true},    // video timing, unused
    {kRegionSound, 0x0000, 0x0100, 0x4bad7017, "rx1-5.3p", false},
    {kRegionSound, 0x0100, 0x0100, 0x77245b66, "rx1-4.2m", true},    // timing, unused
};

// Palette PROM bits -> 8-bit levels through the resistor DAC: red and green
// 1K / 470 / 220 ohm, blue 470 / 220 ohm with a 1K pull-down (weights =
// conductance shares, so blue tops out at ~222).
constexpr uint8_t kRGWeights[3] = {33, 71, 151};
constexpr uint8_t kBWeights[2] = {71, 151};

// The watchdog resets the board after 16 frames without a write to A080.
constexpr int kWatchdogFrames = 16;

// The wavetable voices' share of the output: the crash sound is mixed in
// on top (emu/sound/rallyx_bang), so leave it some room.
constexpr int kWsgGain = 60;            // of the WSG's full-scale 85 for 3 voices

int bit_at(const uint8_t* base, int bit) { return (base[bit >> 3] >> (7 - (bit & 7))) & 1; }

// A 2bpp pixel: the two planes of 4 pixels share a byte, plane 0 (the high
// bit of the pixel value) in the upper nibble.
int pixel_at(const uint8_t* base, int bit) { return (bit_at(base, bit) << 1) | bit_at(base, bit + 4); }

} // namespace

const romset::Set kRomSet = {
    "rallyx", nullptr, "Rally X (32k Ver.?)", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = port_read;
    cpu_.port_out = port_write;
    for (uint32_t base = 0; base < 0x10000; base += 0x4000) {
        if (base & 0x8000) {
            cpu_.map_ram(uint16_t(base), 0x1000, vram_);
            cpu_.map_ram(uint16_t(base + 0x1800), 0x800, ram_);
        } else {
            cpu_.map_read(uint16_t(base), 0x4000, roms.cpu);
        }
    }

    wsg_.init(roms.sound, 3, kWsgClock, audio_rate);
    wsg_.set_gain(kWsgGain);
    bang_.init(audio_rate);

    // Palette: 32 entries, 0-15 for tiles and sprites, 16-31 the bank the
    // radar dots switch to.
    for (int e = 0; e < 32; ++e) {
        const uint8_t v = roms.proms[e];
        const int r = kRGWeights[0] * (v & 1) + kRGWeights[1] * ((v >> 1) & 1) + kRGWeights[2] * ((v >> 2) & 1);
        const int g = kRGWeights[0] * ((v >> 3) & 1) + kRGWeights[1] * ((v >> 4) & 1) + kRGWeights[2] * ((v >> 5) & 1);
        const int b = kBWeights[0] * ((v >> 6) & 1) + kBWeights[1] * ((v >> 7) & 1);
        uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        if (swap_rgb565_bytes) px = uint16_t((px >> 8) | (px << 8));
        palette_[e] = px;
    }
    // Colour lookup: 64 colours x 4 pixel values -> one of the first 16
    // entries; entry 0 is the sprites' transparent colour.
    const uint8_t* lut = roms.proms + 0x20;
    for (int c = 0; c < 64; ++c) {
        opaque_[c] = 0;
        for (int p = 0; p < 4; ++p) {
            const uint8_t e = lut[c * 4 + p] & 0x0f;
            entry_[c * 4 + p] = e;
            pen_[c * 4 + p] = palette_[e];
            if (e) opaque_[c] |= uint8_t(1 << p);
        }
    }

    decode_gfx();
    reset();
}

// The monitor isn't turned: display pixel (x, y) is the hardware's.
void Machine::decode_gfx()
{
    // Tiles: 16 bytes each; pixel x 0-3 in bytes 8-15, 4-7 in bytes 0-7
    // (byte = row y).
    for (int code = 0; code < 256; ++code) {
        const uint8_t* src = roms_.gfx + code * 16;
        for (int y = 0; y < 8; ++y) {
            uint16_t bits = 0;
            for (int x = 0; x < 8; ++x) {
                const int xoff = x < 4 ? 64 + x : x - 4;
                bits |= uint16_t(pixel_at(src, xoff + y * 8) << (2 * x));
            }
            tile_rows_[code * 8 + y] = bits;
        }
    }
    // Sprites: the same ROM as 64-byte sprites; x in groups of 4 pixels at
    // byte 8, 16, 24, 0; rows 0-7 then 8-15 32 bytes on.
    static constexpr int kGroup[4] = {64, 128, 192, 0};
    for (int code = 0; code < 64; ++code) {
        const uint8_t* src = roms_.gfx + code * 64;
        for (int y = 0; y < 16; ++y) {
            uint32_t bits = 0;
            for (int x = 0; x < 16; ++x) {
                const int xoff = kGroup[x >> 2] + (x & 3);
                const int yoff = y < 8 ? y * 8 : 256 + (y - 8) * 8;
                bits |= uint32_t(pixel_at(src, xoff + yoff)) << (2 * x);
            }
            sprite_rows_[code * 16 + y] = bits;
        }
    }
    // Radar dots: 8 shapes of 4 x 4, one byte a pixel (x across, rows 4
    // bytes apart, 16 bytes a shape), the pixel value in its low 2 bits.
    for (int d = 0; d < 8; ++d)
        for (int y = 0; y < 4; ++y) {
            uint8_t bits = 0;
            for (int x = 0; x < 4; ++x) bits |= uint8_t((roms_.dots[d * 16 + y * 4 + x] & 3) << (2 * x));
            dot_rows_[d * 4 + y] = bits;
        }
    for (int b = 0; b < 256; ++b)
        rev4_[b] = uint8_t(((b & 3) << 6) | ((b & 0x0c) << 2) | ((b & 0x30) >> 2) | ((b & 0xc0) >> 6));
}

void Machine::reset()
{
    cpu_.reset();
    wsg_.reset();
    bang_.reset();
    latch_ = 0;
    wsg_.set_enabled(false);
    vector_ = 0;
    watchdog_ = 0;
    carry_ = 0;
}

uint8_t Machine::in0() const
{
    uint8_t v = 0xff;                       // active low
    if (buttons_ & kService) v &= uint8_t(~0x01);
    if (buttons_ & kSmoke)   v &= uint8_t(~0x02);
    if (buttons_ & kLeft)    v &= uint8_t(~0x04);
    if (buttons_ & kRight)   v &= uint8_t(~0x08);
    if (buttons_ & kDown)    v &= uint8_t(~0x10);
    if (buttons_ & kUp)      v &= uint8_t(~0x20);
    if (buttons_ & kStart1)  v &= uint8_t(~0x40);
    if (buttons_ & kCoin)    v &= uint8_t(~0x80);
    return v;
}

uint8_t Machine::in1() const
{
    uint8_t v = 0xff;                       // bit 0 = upright, 1-5 player 2's (cocktail) controls
    if (buttons_ & kStart2) v &= uint8_t(~0x40);
    if (buttons_ & kCoin2)  v &= uint8_t(~0x80);
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if ((addr & 0xa000) != 0xa000) return 0xff;     // 9000-97FF: nothing there
    switch ((addr >> 7) & 3) {
    case 0:  return m.in0();
    case 1:  return m.in1();
    case 2:  return m.dsw_;
    default: return 0xff;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xa000) != 0xa000) return;           // ROM, the empty block
    switch ((addr >> 7) & 3) {
    case 0:
        m.dot_attr_[addr & 0x0f] = v;
        break;
    case 1:
        m.watchdog_ = 0;
        break;
    case 2:
        switch ((addr >> 4) & 7) {
        case 0: case 1: m.wsg_.pacman_write(uint8_t(addr & 0x1f), v); break;
        case 3: m.scroll_x_ = v; break;
        case 4: m.scroll_y_ = v; break;
        default: break;
        }
        break;
    default: {
        const uint8_t bit = uint8_t(1 << (addr & 7));
        const uint8_t old = m.latch_;
        m.latch_ = (v & 1) ? uint8_t(old | bit) : uint8_t(old & ~bit);
        if (bit == 0x01 && m.latch_ != old) {
            m.audio_to(m.slice_start_ + m.cpu_.elapsed());    // up to now it sounded as before
            m.bang_.set_trigger(v & 1);
        }
        if (bit == 0x02 && !(v & 1)) m.cpu_.irq(false);    // IRQ enable off clears a pending IRQ
        if (bit == 0x04) m.wsg_.set_enabled(v & 1);         // this set uses it (New Rally-X doesn't)
        break;
    }
    }
}

uint8_t Machine::port_read(void*, uint16_t) { return 0xff; }

void Machine::port_write(void* ctx, uint16_t port, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((port & 0xff) == 0) {
        m.vector_ = v;
        m.cpu_.set_irq_vector(v);
    }
}

void Machine::vblank()
{
    VideoState& vs = video_;
    std::memcpy(vs.bg_code, vram_ + 0x400, 0x400);
    std::memcpy(vs.bg_attr, vram_ + 0xc00, 0x400);
    for (int row = 0; row < 32; ++row) {
        std::memcpy(vs.fg_code + row * 8, vram_ + row * 32, 8);
        std::memcpy(vs.fg_attr + row * 8, vram_ + 0x800 + row * 32, 8);
    }
    std::memcpy(vs.sprite, vram_ + 0x14, 2 * kSprites);
    std::memcpy(vs.sprite2, vram_ + 0x814, 2 * kSprites);
    std::memcpy(vs.dot_x, vram_ + 0x34, kDots);
    std::memcpy(vs.dot_y, vram_ + 0x834, kDots);
    std::memcpy(vs.dot_attr, dot_attr_ + 4, kDots);
    vs.scroll_x = scroll_x_;
    vs.scroll_y = scroll_y_;

    if (latch_ & 0x02) cpu_.irq(true, vector_);
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    int16_t* out = audio_ + audio_done_;
    const int n = target - audio_done_;
    wsg_.render(out, n);
    bang_.mix(out, n);
    audio_done_ = target;
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // Samples for this frame, keeping the remainder so the long-run rate is exact.
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio;
    audio_n_ = audio ? n : 0;
    audio_done_ = 0;

    slice_start_ = carry_;
    int want = kVblankLine * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    vblank();
    slice_start_ = kVblankLine * kCyclesPerLine + carry_;
    want = (kLinesPerFrame - kVblankLine) * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    ++frame_;

    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    return audio ? n : 0;
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Playfield, screen x 0-223: tile by tile from where the scroll puts
    // the left edge (the first and last tiles partly shown).
    const int py = playfield_y(vs.scroll_y, y);
    const int fy = py & 7;
    const uint8_t* codes = vs.bg_code + (py >> 3) * 32;
    const uint8_t* attrs = vs.bg_attr + (py >> 3) * 32;
    const int px0 = playfield_x(vs.scroll_x, 0);
    for (int x = 0, px = px0; x < kPlayfieldW;) {
        const int col = px >> 3, fx = px & 7;
        int n = 8 - fx;
        if (n > kPlayfieldW - x) n = kPlayfieldW - x;
        const uint8_t a = attrs[col];
        uint32_t bits = tile_rows_[codes[col] * 8 + ((a & 0x80) ? 7 - fy : fy)];
        if (!(a & 0x40)) bits = uint32_t((rev4_[bits & 0xff] << 8) | rev4_[bits >> 8]);
        bits >>= 2 * fx;
        const uint16_t* pens = pen_ + (a & 0x3f) * 4;
        uint16_t* o = out + x;
        for (int i = 0; i < n; ++i, bits >>= 2) o[i] = pens[bits & 3];
        x += n;
        px = (px + n) & 0xff;
    }

    // Radar and score, screen x 224-287: fixed tiles.
    const int hy = y + kFirstLine;
    const int fg_row = (hy >> 3) * 8, fg_fy = hy & 7;
    for (int x = kPlayfieldW; x < kScreenW; x += 8) {
        const int idx = fg_row + radar_column(x);
        const uint8_t a = vs.fg_attr[idx];
        uint32_t bits = tile_rows_[vs.fg_code[idx] * 8 + ((a & 0x80) ? 7 - fg_fy : fg_fy)];
        if (!(a & 0x40)) bits = uint32_t((rev4_[bits & 0xff] << 8) | rev4_[bits >> 8]);
        const uint16_t* pens = pen_ + (a & 0x3f) * 4;
        uint16_t* o = out + x;
        for (int i = 0; i < 8; ++i, bits >>= 2) o[i] = pens[bits & 3];
    }

    // Radar dots on this line (drawn last, below).
    int dots[kDots], dot_rows[kDots], dot_count = 0;
    for (int d = 0; d < kDots; ++d) {
        const int r = y - dot_position(vs.dot_x[d], vs.dot_y[d], vs.dot_attr[d]).y;
        if (r >= 0 && r < 4) { dots[dot_count] = d; dot_rows[dot_count++] = r; }
    }
    // Under a dot the board switches to the second palette bank: a sprite
    // pixel keeps its entry in that bank, so remember sprite entries here.
    uint8_t sprite_entry[kScreenW];
    if (dot_count) std::memset(sprite_entry, 0xff, sizeof sprite_entry);

    // Sprites, 16 x 16, slot 5 first so slot 0 ends on top; tiles marked
    // with attribute bit 5 cover them.
    for (int s = kSprites - 1; s >= 0; --s) {
        const uint8_t flags = vs.sprite[2 * s], attr = vs.sprite2[2 * s + 1];
        const Point p = sprite_position(vs.sprite[2 * s + 1], attr, vs.sprite2[2 * s]);
        int r = y - p.y;
        if (r < 0 || r >= 16 || p.x >= kScreenW) continue;
        if (flags & 2) r = 15 - r;
        uint32_t bits = sprite_rows_[(flags >> 2) * 16 + r];
        if (flags & 1) {
            const uint32_t lo = uint32_t((rev4_[bits & 0xff] << 8) | rev4_[(bits >> 8) & 0xff]);
            const uint32_t hi = uint32_t((rev4_[(bits >> 16) & 0xff] << 8) | rev4_[bits >> 24]);
            bits = (lo << 16) | hi;
        }
        const int colour = attr & 0x3f;
        const uint16_t* pens = pen_ + colour * 4;
        const uint8_t* entries = entry_ + colour * 4;
        const uint8_t opaque = opaque_[colour];
        const int end = p.x + 16 < kScreenW ? p.x + 16 : kScreenW;
        for (int x = p.x; x < end; ++x, bits >>= 2) {
            const int v = int(bits & 3);
            if (!(opaque & (1 << v))) continue;
            const uint8_t under = x < kPlayfieldW ? attrs[((x + px0) >> 3) & 31] : vs.fg_attr[fg_row + radar_column(x)];
            if (under & 0x20) continue;
            out[x] = pens[v];
            if (dot_count) sprite_entry[x] = entries[v];
        }
    }

    // Dots: pixel value 3 is transparent; the others take the second bank,
    // with the low 2 bits of the tile's entry replaced by the dot's (over a
    // sprite: the sprite's entry unchanged).
    for (int k = 0; k < dot_count; ++k) {
        const int d = dots[k];
        const Point p = dot_position(vs.dot_x[d], vs.dot_y[d], vs.dot_attr[d]);
        uint8_t bits = dot_rows_[dot_shape(vs.dot_attr[d]) * 4 + dot_rows[k]];
        for (int i = 0; i < 4; ++i, bits >>= 2) {
            const int x = p.x + i, v = bits & 3;
            if (v == 3 || x >= kScreenW) continue;
            int e = sprite_entry[x];
            if (e == 0xff) {
                // The tile pixel's entry, looked up again (a few pixels a line).
                const bool field = x < kPlayfieldW;
                const int tx = field ? (x + px0) & 0xff : 0;
                const int idx = field ? (py >> 3) * 32 + (tx >> 3) : fg_row + radar_column(x);
                const uint8_t a = field ? vs.bg_attr[idx] : vs.fg_attr[idx];
                const uint8_t code = field ? vs.bg_code[idx] : vs.fg_code[idx];
                const int ty = field ? fy : fg_fy, fx = field ? tx & 7 : x & 7;
                const uint16_t row = tile_rows_[code * 8 + ((a & 0x80) ? 7 - ty : ty)];
                const int pv = (row >> (2 * ((a & 0x40) ? fx : 7 - fx))) & 3;
                e = (entry_[(a & 0x3f) * 4 + pv] & 0x0c) | v;
            }
            out[x] = palette_[16 + e];
        }
    }
}

} // namespace rallyx
