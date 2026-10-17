// Dig Dug (Namco, rev 2) arcade machine, written for this project (MIT).
// The Namco three-Z80 CPU board (namco_galaga_common.*) with Dig Dug's
// video board: a playfield layer drawn from a map ROM (four pictures,
// 2-bit tiles, 4 colour banks), a 1-bit text layer on top, and 64 sprites
// (2-bit, 16 x 16 or 32 x 32, flips); the DIP switches are read through
// the 53XX (namco_io.h), and the high scores live in a 64-byte EAROM.
// Hardware facts from public documentation and MAME's driver (read for
// facts only).
//
// Video is rendered from a snapshot taken at VBLANK, one 224-pixel line at
// a time, in portrait (the cabinet's monitor is turned 90 degrees).
// Cocktail mode (flip screen) isn't drawn: the upright cabinet never
// flips. The EAROM starts blank (factory high scores) on every power-on.
#pragma once

#include "../rom/romset.h"
#include "namco_galaga_common.h"

#include <cstdint>

namespace digdug {

using namco_galaga::kScreenW;
using namco_galaga::kScreenH;
using namco_galaga::kCpuClock;
using namco_galaga::kCyclesPerFrame;

// Buttons for set_inputs(): namco_galaga::Button (4-way stick, pump =
// kFire, coins, starts, service, test).

// DIP switch bank A (MAME "DSWA"), set_dips(): bits as wired, high = set.
enum Dip : uint8_t {
    kDipCoinBMask = 0x07, kDipCoinB1Coin1Credit = 0x01,
    kDipBonusMask = 0x38, kDipBonus20k60k = 0x18, kDipBonusNone = 0x00,   // (with 1-3 lives)
    kDipLivesMask = 0xc0, kDipLives1 = 0x00, kDipLives2 = 0x40, kDipLives3 = 0x80, kDipLives5 = 0xc0,
};
// Coin B 1 coin 1 credit, bonus at 20K and 60K (Namco's factory setting),
// 3 lives.
constexpr uint8_t kDipDefault = kDipCoinB1Coin1Credit | kDipBonus20k60k | kDipLives3;

// Bank B ("DSWB"), set_dips_b(): bits 6-7 coin A (0 = 1 coin 1 credit),
// bit 5 set = no freeze, bit 4 set = silent attract mode, bit 3 set = no
// continue, bit 2 set = upright, bits 0-1 difficulty (0 = the easiest,
// the factory setting). Default: 1 coin 1 credit, sound in the attract
// mode, continue allowed, upright, easiest.
constexpr uint8_t kDipBDefault = 0x24;
constexpr uint8_t kDipBDemoSoundsOff = 0x10;     // bank B bit 4 (SWB:4)

// The ROM set (MAME "digdug"); regions in this order.
enum Region { kRegionMain, kRegionSub, kRegionSound, kRegionText, kRegionSprites, kRegionTiles, kRegionMap,
              kRegionProms, kRegionWave, kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* main;        // 16 KB
    const uint8_t* sub;         // 8 KB
    const uint8_t* sound;       // 4 KB
    const uint8_t* text;        // 2 KB: 256 1-bit characters (the second 128 mirrored, for flip)
    const uint8_t* sprites;     // 16 KB: 256 sprites
    const uint8_t* tiles;       // 4 KB: 256 playfield tiles
    const uint8_t* map;         // 4 KB: the four playfield pictures (tile codes, colour in the top 4 bits)
    const uint8_t* proms;       // 32 B palette, 256 B sprite lookup, 256 B playfield lookup
    const uint8_t* wave;        // 256 B waveforms (+ unused timing PROM)
};

constexpr int kSprites = 64;

// What the video board shows: copied at VBLANK (about 1.5 KB).
struct VideoState {
    uint8_t vram[0x400];                        // text layer codes
    namco_galaga::Sprite sprite[kSprites];      // on-screen ones, in drawing order
    uint8_t sprite_count;
    uint64_t sprite_bands[namco_galaga::kBands];    // sprites per 16 lines (namco_galaga::sprite_bands)
    uint32_t text_cols[kScreenH / 8];           // per tile row: bit c = column c's character isn't blank
    uint8_t latch;                              // the video latch (playfield picture, colours)
};

class Machine : public namco_galaga::Board {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_dips(uint8_t bank_a) { dips_a_ = bank_a; chip53_.set_dips(dips_a_, dips_b_); }
    void set_dips_b(uint8_t bank_b) { dips_b_ = bank_b; chip53_.set_dips(dips_a_, dips_b_); }
    // The attract mode's sounds (bank B's Demo Sounds switch).
    void set_demo_sounds(bool on)
    {
        set_dips_b(uint8_t(on ? dips_b_ & ~kDipBDemoSoundsOff : dips_b_ | kDipBDemoSoundsOff));
    }

    // Renders display line y (0-287) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    uint8_t video_latch() const { return video_latch_; }
    const uint8_t* earom() const { return earom_; }

private:
    uint8_t io_read(int cpu, uint16_t addr) override;
    void    io_write(int cpu, uint16_t addr, uint8_t v) override;
    uint8_t chip_read(int chip) override;
    void    chips_reset() override;
    void    vblank() override;

    Roms     roms_{};
    namco_io::Chip53 chip53_;
    uint8_t  video_latch_ = 0;      // A000-A007: 0-1 picture, 2 text colour mode, 3 playfield off,
                                    // 4-5 playfield colour bank, 7 flip
    uint8_t  dips_a_ = kDipDefault, dips_b_ = kDipBDefault;

    // ER2055 EAROM: 64 bytes, an address / data latch and a control port.
    uint8_t  earom_[64] = {};
    uint8_t  earom_addr_ = 0, earom_data_ = 0, earom_out_ = 0, earom_ctrl_ = 0;

    VideoState video_{};
    // Graphics decoded once into display orientation.
    uint8_t  text_rows_[128 * 8] = {};      // 1 bit a pixel
    uint8_t  text_blank_[128] = {};         // per character: no pixel set
    uint16_t tile_rows_[256 * 8] = {};      // 2 bits a pixel
    uint32_t sprite_rows_[256 * 16] = {};
    uint8_t  sprite_blank_[256] = {};       // per sprite tile: no pixel set
    uint16_t text_pen_[16] = {};            // RGB565 per colour (pixel value 1)
    uint16_t tile_pen_[64 * 4] = {};        // per (colour, pixel value)
    uint16_t sprite_pen_[64 * 4] = {};
    uint8_t  sprite_opaque_[64] = {};       // per colour: bit p = pixel value p isn't transparent
};

} // namespace digdug
