// Namco Galaxian video hardware, shared by Galaxian and the boards built on
// it (Konami's Amidar), written for this project (MIT). Hardware facts from
// public documentation and MAME's driver (read for facts only).
//
// The board draws a 256-pixel scanline (H) from 264 lines (V), lines 16-239
// shown; the monitor is turned 90 degrees clockwise, so the player sees
// 224 x 256: display line y = hardware pixel y along the scanline, display
// x = 239 - hardware line. Layers, back to front:
//   - the background: black (Galaxian) or a colour from three latch bits
//     (Amidar's board);
//   - the star field (Galaxian): a 17-bit LFSR clocked twice a pixel; a
//     state with bits 16-9 set and bit 0 clear lights the pixel in the
//     colour of its bits 8-3 (inverted), on a checkerboard of 8-pixel
//     blocks (line bit 0 XOR pixel bit 3); the field moves half a pixel a
//     frame;
//   - 32 x 32 tiles of 8 x 8, one colour (3 bits) and one line scroll per
//     tile column (= per display row of tiles), pen 0 clear;
//   - 8 sprites of 16 x 16 (code, flips, colour, X, Y; sprites 0-2 one line
//     lower), not in the first 17 pixels of a scanline;
//   - Galaxian's bullets: 7 white shells and one yellow missile, 4 pixels
//     along the scanline on one line; of the shells only the last one on a
//     line shows.
// Palette: a 32-byte PROM, 8 colours x 4 pens, BBGGGRRR through 1K / 470 /
// 220 ohm (blue 470 / 220) against a 470 ohm load.
//
// Rendered line by line from a VideoState copied at VBLANK, so a device can
// stream lines to the LCD (and draw on another core) without a frame buffer.
#pragma once

#include <cstdint>

namespace galaxian_video {

constexpr int kScreenW = 224;           // hardware lines 16-239
constexpr int kScreenH = 256;           // hardware pixels 0-255 along the scanline
constexpr int kFirstLine = 16;
constexpr int kLastLine = 239;
constexpr int kSpriteClip = 17;         // no sprite pixels before this one on a scanline
constexpr uint32_t kStarPeriod = (1u << 17) - 1;
constexpr int kMaxStars = 256;          // the states that light: 2^17 / 512

// Which parts a board has.
struct Options {
    bool stars;
    bool bullets;
    bool background;    // the colour background (Konami); else black
};

// Display x of hardware line `line` (16-239).
constexpr int display_x(int line) { return kLastLine - line; }
// The hardware line a sprite's Y byte starts it on (sprites 0-2 one lower).
constexpr int sprite_line(uint8_t y, int sprite) { return uint8_t(240 - (y - (sprite < 3 ? 1 : 0))); }
// The hardware line a bullet's Y byte puts it on (entries 0-2 one lower).
constexpr int bullet_line(uint8_t y, int n) { return uint8_t((n < 3 ? 0x100 : 0xff) - y); }
// Steps the star LFSR once.
constexpr uint32_t star_step(uint32_t s) { return (s >> 1) | (((((s >> 12) ^ ~s) & 1u)) << 16); }

// What the video hardware shows: copied at VBLANK.
struct VideoState {
    uint8_t  vram[0x400];       // tile codes, 32 x 32 (row = 8 hardware lines)
    uint8_t  obj[0x80];         // 00-3F column scroll / colour, 40-5F sprites, 60-7F bullets
    uint8_t  background;        // bit 0 red, 1 green, 2 blue
    uint16_t star_count;
    // Stars by display line: line y's are star_x / star_pen [star_at[y],
    // star_at[y + 1]).
    uint16_t star_at[kScreenH + 1];
    uint8_t  star_x[kMaxStars];
    uint8_t  star_pen[kMaxStars];   // index into the star palette (6 bits)
};

class Video {
public:
    // `prom`: the 32-byte palette PROM; `gfx`: 4 KB, the two bit planes of
    // 256 tiles (or 64 sprites) 2 KB apart.
    void init(const uint8_t* prom, const uint8_t* gfx, const Options& options, bool swap_rgb565_bytes);

    // Builds the frame's VideoState: the RAM as it is now plus the stars
    // at `star_origin` (see Machine: the origin steps once a frame).
    void capture(VideoState& vs, const uint8_t* vram, const uint8_t* obj, uint8_t background, bool stars_on,
                 uint32_t star_origin) const;

    // Renders display line y (0-255) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;

    uint16_t pen(int entry) const { return pen_[entry & 31]; }
    uint16_t star_colour(int c) const { return star_rgb_[c & 63]; }

private:
    Options  opt_{};
    // Tiles in display orientation: per (code, pixel x along the scanline)
    // the 8 pixels down the tile's lines, 2 bits each (line j in bits 2j+1..2j).
    uint16_t tile_cols_[256 * 8] = {};
    // Sprites likewise: per (code, x) the 16 pixels down its lines.
    uint32_t sprite_cols_[64 * 16] = {};
    uint16_t pen_[32] = {};
    uint16_t star_rgb_[64] = {};
    uint16_t bg_[8] = {};
    uint16_t shell_ = 0, missile_ = 0;
    // The star states that light (bits 16-0 index, colour in bits 23-18), in
    // the order the LFSR runs through them.
    uint32_t star_states_[kMaxStars] = {};
    int      star_states_n_ = 0;
};

} // namespace galaxian_video
