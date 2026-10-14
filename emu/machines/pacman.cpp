// Pac-Man machine - see pacman.h.
//
// Memory map (A15 isn't wired, so 8000-FFFF mirrors 0000-7FFF; A13 isn't
// decoded above 4000, so 6000-7FFF mirrors 4000-5FFF):
//   0000-3FFF  program ROM (6E 6F 6H 6J)
//   4000-43FF  video RAM (tile codes)      4400-47FF  colour RAM
//   4800-4BFF  nothing (reads 0xBF - the game reads it in the right tunnel)
//   4C00-4FEF  RAM                         4FF0-4FFF  sprite codes / colours
//   5000-503F  write: 74LS259 latch, bit = A0-A2 (0 IRQ enable, 1 sound
//              enable, 3 flip screen, others lamps / coin counter)
//   5040-505F  write: WSG registers        5060-506F  write: sprite X / Y
//   50C0-50FF  write: watchdog reset
//   5000 / 5040 / 5080 / 50C0  read: IN0 / IN1 / DSW1 / DSW2
//   I/O port 00 write: the IM 2 vector the board puts on the bus.
#include "pacman.h"

#include <cstring>

namespace pacman {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x4000},
    {"gfx1",    0x2000},
    {"proms",   0x0120},
    {"namco",   0x0200},
};

// CRCs as in MAME's "pacman" set; the PROMs and sound PROMs are shared with
// "puckman" (where they carry other names), which matching by CRC handles.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x1000, 0xc1e6ab10, "pacman.6e", false},
    {kRegionCpu,   0x1000, 0x1000, 0x1a6fb2d4, "pacman.6f", false},
    {kRegionCpu,   0x2000, 0x1000, 0xbcdd1beb, "pacman.6h", false},
    {kRegionCpu,   0x3000, 0x1000, 0x817d94e3, "pacman.6j", false},
    {kRegionGfx,   0x0000, 0x1000, 0x0c944964, "pacman.5e", false},
    {kRegionGfx,   0x1000, 0x1000, 0x958fedf9, "pacman.5f", false},
    {kRegionProms, 0x0000, 0x0020, 0x2fc650bd, "82s123.7f", false},
    {kRegionProms, 0x0020, 0x0100, 0x3eb3a8e4, "82s126.4a", false},
    {kRegionSound, 0x0000, 0x0100, 0xa9cc86bf, "82s126.1m", false},
    {kRegionSound, 0x0100, 0x0100, 0x77245b66, "82s126.3m", true},   // timing, unused
};

// Palette PROM bits -> 8-bit levels through the resistor DAC: red and green
// 1K / 470 / 220 ohm, blue 470 / 220 ohm (weights = conductance shares).
constexpr uint8_t kRGWeights[3] = {33, 71, 151};
constexpr uint8_t kBWeights[2] = {81, 174};

int bit_at(const uint8_t* base, int bit) { return (base[bit >> 3] >> (7 - (bit & 7))) & 1; }

// A 2bpp pixel: the two planes of 4 pixels share a byte, plane 0 (the high
// bit of the pixel value) in the upper nibble.
int pixel_at(const uint8_t* base, int bit) { return (bit_at(base, bit) << 1) | bit_at(base, bit + 4); }

} // namespace

const romset::Set kRomSet = {
    "pacman", "puckman", "Pac-Man (Midway)", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
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
    for (uint32_t base = 0; base < 0x10000; base += 0x8000) {
        cpu_.map_read(uint16_t(base), 0x4000, roms.cpu);
        for (uint32_t m = 0x4000; m < 0x8000; m += 0x2000) {
            cpu_.map_ram(uint16_t(base + m), 0x800, vram_);
            cpu_.map_ram(uint16_t(base + m + 0xc00), 0x400, ram_);
        }
    }

    wsg_.init(roms.sound, 3, kWsgClock, audio_rate);

    // Colour lookup: 64 colours x 4 pixel values -> one of the first 16
    // palette entries; entry 0 (black) is the sprites' transparent colour.
    const uint8_t* pal = roms.proms;
    const uint8_t* lut = roms.proms + 0x20;
    for (int c = 0; c < 64; ++c) {
        opaque_[c] = 0;
        for (int p = 0; p < 4; ++p) {
            const uint8_t e = lut[c * 4 + p] & 0x0f;
            const uint8_t v = pal[e];
            const int r = kRGWeights[0] * (v & 1) + kRGWeights[1] * ((v >> 1) & 1) + kRGWeights[2] * ((v >> 2) & 1);
            const int g = kRGWeights[0] * ((v >> 3) & 1) + kRGWeights[1] * ((v >> 4) & 1) + kRGWeights[2] * ((v >> 5) & 1);
            const int b = kBWeights[0] * ((v >> 6) & 1) + kBWeights[1] * ((v >> 7) & 1);
            uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            if (swap_rgb565_bytes) px = uint16_t((px >> 8) | (px << 8));
            pen_[c * 4 + p] = px;
            if (e) opaque_[c] |= uint8_t(1 << p);
        }
    }

    decode_gfx();
    reset();
}

// The monitor is turned 90 degrees clockwise: a display pixel (x, y) is the
// hardware's (y, 7 - x) inside a tile, (y, 15 - x) inside a sprite.
void Machine::decode_gfx()
{
    // Tiles: 16 bytes each; hardware pixel x 0-3 in bytes 8-15, 4-7 in
    // bytes 0-7 (byte = row y).
    for (int code = 0; code < 256; ++code) {
        const uint8_t* src = roms_.gfx + code * 16;
        for (int dy = 0; dy < 8; ++dy) {
            uint16_t bits = 0;
            for (int dx = 0; dx < 8; ++dx) {
                const int hx = dy, hy = 7 - dx;
                const int xoff = hx < 4 ? 64 + hx : hx - 4;
                bits |= uint16_t(pixel_at(src, xoff + hy * 8) << (2 * dx));
            }
            tile_rows_[code * 8 + dy] = bits;
        }
    }
    // Sprites: 64 bytes each; hardware x in groups of 4 pixels at byte 8, 16,
    // 24, 0; rows 0-7 then 8-15 32 bytes on.
    static constexpr int kGroup[4] = {64, 128, 192, 0};
    for (int code = 0; code < 64; ++code) {
        const uint8_t* src = roms_.gfx + 0x1000 + code * 64;
        for (int dy = 0; dy < 16; ++dy) {
            uint32_t bits = 0;
            for (int dx = 0; dx < 16; ++dx) {
                const int hx = dy, hy = 15 - dx;
                const int xoff = kGroup[hx >> 2] + (hx & 3);
                const int yoff = hy < 8 ? hy * 8 : 256 + (hy - 8) * 8;
                bits |= uint32_t(pixel_at(src, xoff + yoff)) << (2 * dx);
            }
            sprite_rows_[code * 16 + dy] = bits;
        }
    }
}

void Machine::reset()
{
    cpu_.reset();
    wsg_.reset();
    latch_ = 0;
    wsg_.set_enabled(false);
    vector_ = 0;
    watchdog_ = 0;
    carry_ = 0;
}

uint8_t Machine::in0() const
{
    uint8_t v = 0xff;                       // active low; bit 4 = rack test off
    if (buttons_ & kUp)      v &= uint8_t(~0x01);
    if (buttons_ & kLeft)    v &= uint8_t(~0x02);
    if (buttons_ & kRight)   v &= uint8_t(~0x04);
    if (buttons_ & kDown)    v &= uint8_t(~0x08);
    if (buttons_ & kCoin)    v &= uint8_t(~0x20);
    if (buttons_ & kCoin2)   v &= uint8_t(~0x40);
    if (buttons_ & kService) v &= uint8_t(~0x80);
    return v;
}

uint8_t Machine::in1() const
{
    uint8_t v = 0xff;                       // bits 0-3 player 2 stick (cocktail), 7 = upright
    if (buttons_ & kTest)   v &= uint8_t(~0x10);
    if (buttons_ & kStart1) v &= uint8_t(~0x20);
    if (buttons_ & kStart2) v &= uint8_t(~0x40);
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    const uint16_t a = addr & 0x7fff;
    if ((a & 0x5000) == 0x5000) {           // 5000-5FFF / 7000-7FFF
        switch (a & 0xc0) {
        case 0x00: return m.in0();
        case 0x40: return m.in1();
        case 0x80: return m.dsw1_;
        default:   return 0x00;             // DSW2: not fitted on Pac-Man
        }
    }
    if ((a & 0x4c00) == 0x4800) return 0xbf;  // the empty 4800-4BFF block
    return 0xff;
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const uint16_t a = addr & 0x7fff;
    if ((a & 0x5000) != 0x5000) return;     // ROM, the empty block
    const uint8_t o = a & 0xff;
    if (o < 0x40) {
        const uint8_t bit = uint8_t(1 << (o & 7));
        m.latch_ = (v & 1) ? uint8_t(m.latch_ | bit) : uint8_t(m.latch_ & ~bit);
        if (bit == 0x01 && !(v & 1)) m.cpu_.irq(false);    // IRQ enable off clears a pending IRQ
        if (bit == 0x02) m.wsg_.set_enabled(v & 1);
    } else if (o < 0x60) {
        m.wsg_.pacman_write(uint8_t(o - 0x40), v);
    } else if (o < 0x70) {
        m.sprite_xy_[o - 0x60] = v;
    } else if (o >= 0xc0) {
        m.watchdog_ = 0;
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
    std::memcpy(video_.vram, vram_, 0x400);
    std::memcpy(video_.cram, vram_ + 0x400, 0x400);
    std::memcpy(video_.sprite, ram_ + 0x3f0, 16);
    std::memcpy(video_.sprite_xy, sprite_xy_, 16);
    video_.flip = (latch_ & 0x08) != 0;

    if (latch_ & 0x01) cpu_.irq(true, vector_);
    // The watchdog resets the board after 16 frames without a write to 50C0.
    if (++watchdog_ >= 16) {
        ++watchdog_resets_;
        reset();
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    int want = kVblankLine * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    vblank();
    want = (kLinesPerFrame - kVblankLine) * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    ++frame_;

    // Samples for this frame, keeping the remainder so the long-run rate is exact.
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (!audio) return 0;
    if (n > cap) n = cap;
    wsg_.render(audio, n);
    return n;
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Tile layer, 28 x 36 tiles. Video RAM order: rows 34-35 (bottom) at
    // 000-03F, the 32 middle rows column by column from the right edge at
    // 040-3BF, rows 0-1 (top) at 3C0-3FF; rows 0-1 / 34-35 run right to
    // left and have two hidden tiles at each end. Flip screen turns the
    // whole layer 180 degrees.
    const int ly = vs.flip ? kScreenH - 1 - y : y;
    const int row = ly >> 3, ty = ly & 7;
    int off, step;
    if (row < 2)        { off = 0x3dd + row * 0x20; step = -1; }
    else if (row >= 34) { off = 0x01d + (row - 34) * 0x20; step = -1; }
    else                { off = 0x3a0 + (row - 2); step = -0x20; }
    for (int col = 0; col < 28; ++col, off += step) {
        const uint16_t bits = tile_rows_[vs.vram[off] * 8 + ty];
        const uint16_t* pens = pen_ + (vs.cram[off] & 0x1f) * 4;
        if (!vs.flip) {
            uint16_t* o = out + col * 8;
            for (int x = 0; x < 8; ++x) o[x] = pens[(bits >> (2 * x)) & 3];
        } else {
            uint16_t* o = out + (27 - col) * 8;
            for (int x = 0; x < 8; ++x) o[7 - x] = pens[(bits >> (2 * x)) & 3];
        }
    }

    // Sprites: 16 x 16, only over tile rows 2-33; drawn 7 down to 0 so
    // sprite 0 ends on top. Sprites 0-2 sit one pixel further left on the
    // real board.
    if (y < 16 || y >= 272) return;
    for (int s = 7; s >= 0; --s) {
        const int top = 272 - vs.sprite_xy[2 * s + 1];
        int py = y - top;
        if (py < 0 || py >= 16) {
            py += 256;                       // wraps round from the top
            if (py < 0 || py >= 16) continue;
        }
        const uint8_t attr = vs.sprite[2 * s];
        const uint32_t bits = sprite_rows_[(attr >> 2) * 16 + ((attr & 1) ? 15 - py : py)];
        const int colour = vs.sprite[2 * s + 1] & 0x1f;
        const uint16_t* pens = pen_ + colour * 4;
        const uint8_t opaque = opaque_[colour];
        const int left = 239 - vs.sprite_xy[2 * s] - (s <= 2 ? 1 : 0);
        const bool hflip = attr & 2;
        for (int px = 0; px < 16; ++px) {
            const int x = left + px;
            if (x < 0 || x >= kScreenW) continue;
            const int p = int(bits >> (2 * (hflip ? 15 - px : px))) & 3;
            if (opaque & (1 << p)) out[x] = pens[p];
        }
    }
}

} // namespace pacman
