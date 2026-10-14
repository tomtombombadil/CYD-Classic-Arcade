// Galaga (Namco rev. B) arcade machine, written for this project (MIT).
// The Namco three-Z80 CPU board (namco_galaga_common.*) with Galaga's video
// board: a 36 x 28 character layer (2-bit, 64 colours), 64 sprites (2-bit,
// 16 x 16, double width / height, flips) and the 05XX starfield
// (starfield05.*); the 54XX noise chip (the explosions) is our own sound
// model (emu/sound/namco54.*), its program (54xx.bin) not being in the
// usual sets. Hardware facts from public documentation and MAME's driver
// (read for facts only).
//
// Video is rendered from a snapshot taken at VBLANK, one 224-pixel line at
// a time, in portrait (the cabinet's monitor is turned 90 degrees), so a
// device can stream lines to the LCD without a frame buffer. Cocktail mode
// (flip screen) isn't drawn: the upright cabinet never flips.
#pragma once

#include "../rom/romset.h"
#include "../sound/namco54.h"
#include "namco_galaga_common.h"
#include "starfield05.h"

#include <cstdint>

namespace galaga {

using namco_galaga::kScreenW;
using namco_galaga::kScreenH;
using namco_galaga::kCpuClock;
using namco_galaga::kCyclesPerFrame;

// Buttons for set_inputs(): namco_galaga::Button (left / right, fire,
// coins, starts, service, test).

// DIP switch bank B (the board's SWA; MAME "DSWB"), set_dips().
enum Dip : uint8_t {
    kDipCoinMask  = 0x07, kDipFreePlay = 0x00, kDip2Coins3Credits = 0x01, kDip3Coins1Credit = 0x02,
                          kDip1Coin2Credits = 0x03, kDip4Coins1Credit = 0x04, kDip1Coin3Credits = 0x05,
                          kDip2Coins1Credit = 0x06, kDip1Coin1Credit = 0x07,
    kDipBonusMask = 0x38, kDipBonusNone = 0x00, kDipBonus20k70kEvery70k = 0x10,   // (with 2-4 lives)
    kDipLivesMask = 0xc0, kDipLives2 = 0x00, kDipLives4 = 0x40, kDipLives3 = 0x80, kDipLives5 = 0xc0,
};
// 1 coin 1 credit, bonus at 20K, 70K and every 70K (the factory setting),
// 3 lives.
constexpr uint8_t kDipDefault = kDip1Coin1Credit | kDipBonus20k70kEvery70k | kDipLives3;

// Bank A (SWB; MAME "DSWA"), set_dips_a(): bits 0-1 difficulty (both set =
// rank A, the factory setting), bit 3 set = silent attract mode, 4 clear =
// freeze, 5 clear = rack test, 7 set = upright cabinet; 2 and 6 unused.
// Default: rank A, sound in the attract mode, upright.
constexpr uint8_t kDipADefault = 0xf7;

// The ROM set (MAME "galaga"); regions in this order.
enum Region { kRegionMain, kRegionSub, kRegionSound, kRegionChars, kRegionSprites, kRegionProms, kRegionWave,
              kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* main;        // 16 KB
    const uint8_t* sub;         // 4 KB
    const uint8_t* sound;       // 4 KB
    const uint8_t* chars;       // 4 KB: 256 characters (the second 128 mirrored, for flip)
    const uint8_t* sprites;     // 8 KB: 128 sprites
    const uint8_t* proms;       // 32 B palette, 256 B character lookup, 256 B sprite lookup
    const uint8_t* wave;        // 256 B waveforms (+ unused timing PROM)
};

constexpr int kSprites = 64;
constexpr int kMaxStars = starfield05::kMaxShown;

// What the video board shows: copied at VBLANK (about 3 KB).
struct VideoState {
    uint8_t vram[0x400];                        // character codes
    uint8_t cram[0x400];                        // character colours
    namco_galaga::Sprite sprite[kSprites];      // on-screen ones, in drawing order
    uint8_t sprite_count;
    struct Star {
        uint16_t line;                          // display line
        uint8_t  x;                             // display x
        uint8_t  colour;                        // BBGGRR
    } star[kMaxStars];                          // by line
    uint8_t star_first[kScreenH + 1];           // index of the first star on / after each line
};

class Machine : public namco_galaga::Board {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_dips(uint8_t bank_b) { dips_b_ = bank_b; }
    void set_dips_a(uint8_t bank_a) { dips_a_ = bank_a; }

    // Renders display line y (0-287) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    const namco54::Chip& noise() const { return noise_; }
    uint8_t video_latch() const { return video_latch_; }

private:
    uint8_t io_read(int cpu, uint16_t addr) override;
    void    io_write(int cpu, uint16_t addr, uint8_t v) override;
    void    chip_write(int chip, uint8_t v) override;
    void    chips_reset() override;
    void    vblank() override;
    void    frame_end() override;
    void    mix(int16_t* out, int n) override;

    Roms     roms_{};
    starfield05::Generator stars_;
    namco54::Chip noise_;
    uint8_t  video_latch_ = 0;      // A000-A007: 0-2 star scroll, 3-4 star sets, 5 stars on, 7 flip
    uint8_t  dips_a_ = kDipADefault, dips_b_ = kDipDefault;

    VideoState video_{};
    // Graphics decoded once into display orientation (2 bits a pixel).
    uint16_t char_rows_[128 * 8] = {};
    uint32_t sprite_rows_[128 * 16] = {};
    uint16_t char_pen_[64 * 4] = {};    // RGB565 per (colour, pixel value)
    uint16_t sprite_pen_[64 * 4] = {};
    uint16_t star_pen_[64] = {};
    uint8_t  char_opaque_[64] = {};     // per colour: bit p = pixel value p isn't transparent
    uint8_t  sprite_opaque_[64] = {};
};

} // namespace galaga
