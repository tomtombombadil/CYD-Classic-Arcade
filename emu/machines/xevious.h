// Xevious (Namco, 1982) arcade machine, written for this project (MIT).
// The Namco three-Z80 CPU board (namco_galaga_common.*) with Xevious' RAM
// map and video board: a scrolling 64 x 32 background of 2-bit tiles (512
// of them, 128 colours through the lookup PROMs), a scrolling 64 x 32
// layer of 1-bit characters on top, 64 sprites (3-bit, 16 x 16, double
// width / height, flips) between them, and the map ROMs the game reads
// its terrain through (xvi_9 / 10 / 11, "BS" / "BB" at F000). The custom
// chips: 51XX (controls, credits) and 50XX (a protection check at power-
// on) are behaviour models (namco_io.h), the 54XX explosions our own
// sound model (emu/sound/namco54.*). Hardware facts from public
// documentation and MAME's driver (read for facts only).
//
// Video is rendered from a snapshot taken at VBLANK, one 224-pixel line at
// a time, in portrait (the cabinet's monitor is turned 90 degrees), so a
// device can stream lines to the LCD without a frame buffer. Cocktail mode
// (flip screen) isn't drawn: the upright cabinet never flips. No stars.
#pragma once

#include "../rom/romset.h"
#include "../sound/namco54.h"
#include "namco_galaga_common.h"

#include <cstdint>

namespace xevious {

using namco_galaga::kScreenW;
using namco_galaga::kScreenH;
using namco_galaga::kCpuClock;
using namco_galaga::kCyclesPerFrame;

// Buttons for set_inputs(): namco_galaga::Button (8-way stick, kFire = the
// Zapper - the air shot -, coins, starts, service, test) plus the Blaster
// (the bomb), which the board reads with the DIP switches.
constexpr uint16_t kBlaster = 0x800;

// DIP switch bank A (MAME "DSWA"), set_dips(): bits as read, high = off.
// Bits 0-1 coin A (3 = 1 coin 1 credit), 2-4 bonus lives (7 = 20K, 60K
// and every 60K - the factory setting), 5-6 lives (3 = 3), 7 set =
// upright.
constexpr uint8_t kDipDefault = 0xff;
// Bank B ("DSWB"), set_dips_b(): bit 0 is the Blaster button (low =
// pressed, set by set_inputs), 1 set = flags award a bonus life, 2-3 coin
// B, 5-6 difficulty (3 = normal), 7 set = no freeze.
constexpr uint8_t kDipBDefault = 0xff;

// The ROM set (MAME "xevious", the Namco version); regions in this order.
enum Region { kRegionMain, kRegionSub, kRegionSound, kRegionChars, kRegionTiles, kRegionSprites, kRegionMap,
              kRegionProms, kRegionWave, kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* main;        // 16 KB
    const uint8_t* sub;         // 8 KB
    const uint8_t* sound;       // 4 KB
    const uint8_t* chars;       // 4 KB: 512 1-bit characters (the second 256 mirrored, for flip)
    const uint8_t* tiles;       // 8 KB: 512 2-bit background tiles (two planes of 4 KB)
    const uint8_t* sprites;     // 28 KB: xvi_15 / 17 / 16 (planes 0-1) and xvi_18 (plane 2)
    const uint8_t* map;         // 16 KB: xvi_9 (4 KB), xvi_10 (8 KB), xvi_11 (4 KB)
    const uint8_t* proms;       // 3 x 256 B palette, 2 x 512 B tile lookup, 2 x 512 B sprite lookup
    const uint8_t* wave;        // 256 B waveforms (+ unused timing PROM)
};

constexpr int kSprites = 64;

// A sprite as the snapshot keeps it: hardware position (x = display line)
// of its 16 x 16 tile at code, the others of a double one beside it.
struct Sprite {
    int16_t  x, y;
    uint16_t code;
    uint8_t  colour;
    uint8_t  flags;             // kFlipX, kFlipY, kWide, kTall (as namco_galaga)
};

// What the video board shows: copied at VBLANK (about 8.8 KB).
struct VideoState {
    uint8_t  fg_attr[0x800], bg_attr[0x800], fg_code[0x800], bg_code[0x800];   // B000-CFFF
    uint16_t bg_x, bg_y, fg_x, fg_y;            // scroll registers (9 bits)
    uint32_t fg_rows[64];                       // per character column: bit r = row r's isn't blank
    Sprite   sprite[kSprites];                  // shown ones, in drawing order
    uint8_t  sprite_count;
    uint64_t sprite_bands[namco_galaga::kBands];    // bit s = sprite s reaches a line of the band
};

class Machine : public namco_galaga::Board {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_dips(uint8_t bank_a) { dips_a_ = bank_a; }
    void set_dips_b(uint8_t bank_b) { dips_b_ = bank_b; }
    // namco_galaga::Button | kBlaster.
    void set_inputs(uint16_t buttons)
    {
        Board::set_inputs(buttons);
        blaster_ = (buttons & kBlaster) != 0;
    }

    // Renders display line y (0-287) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    const namco54::Chip&   noise() const { return noise_; }
    const namco_io::Chip50& chip50() const { return chip50_; }
    // The map ROMs as the game reads them at F000 / F001 (BB0 / BB1) after
    // writing BS0 / BS1 there.
    uint8_t map_read(int n) const;

private:
    uint8_t io_read(int cpu, uint16_t addr) override;
    void    io_write(int cpu, uint16_t addr, uint8_t v) override;
    uint8_t chip_read(int chip) override;
    void    chip_write(int chip, uint8_t v) override;
    void    chips_reset() override;
    void    control_written(uint8_t v) override;
    void    vblank() override;
    void    mix(int16_t* out, int n) override;

    void draw_sprite(const Sprite& s, int line, uint16_t* out) const;

    Roms     roms_{};
    namco54::Chip    noise_;
    namco_io::Chip50 chip50_;
    uint8_t  dips_a_ = kDipDefault, dips_b_ = kDipBDefault;
    bool     blaster_ = false;
    uint8_t  bs_[2] = {};                       // F000 / F001: the map ROM address
    uint16_t scroll_[4] = {};                   // D000-D07F: bg x, fg x, bg y, fg y

    // RAM beyond the board's (7800-87FF use its RAM): 9000-97FF, A000-A7FF
    // (sprite registers at their tops) and the video RAM B000-CFFF.
    uint8_t  ram2_[0x800] = {};
    uint8_t  ram3_[0x800] = {};
    uint8_t  vram_x_[0x2000] = {};

    VideoState video_{};
    // Graphics decoded once into display order: per tile and hardware x
    // column (= display line), its 8 pixels along hardware y.
    uint16_t tile_cols_[512 * 8] = {};          // 2 bits a pixel, hardware y 7 - r at bits 2r
    uint8_t  char_cols_[256 * 8] = {};          // 1 bit a pixel, hardware y 7 - r at bit r
    uint8_t  char_blank_[256] = {};             // per character: no pixel set
    uint16_t tile_pen_[128 * 4] = {};           // RGB565 per (colour, pixel value)
    // Per background attribute byte << 1 | code bit 7: the colour's offset
    // in tile_pen_ (bits 0-8), 7 when flipped in x (bits 12-14: the column
    // is 7 - x), flipped in y (bit 15).
    uint16_t bg_attr_[512] = {};
    uint16_t char_pen_[64] = {};
    uint16_t sprite_pen_[64 * 8] = {};
    uint8_t  sprite_opaque_[64] = {};           // per colour: bit p = pixel value p isn't transparent
};

} // namespace xevious
