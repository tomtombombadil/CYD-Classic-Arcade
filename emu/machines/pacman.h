// Pac-Man (Midway) arcade machine, written for this project (MIT).
// Z80 at 3.072 MHz, 60.606 Hz frames (264 lines x 192 CPU cycles), VBLANK
// interrupt in IM 2 with the vector latched by OUT (0), Namco 3-voice WSG.
// Hardware facts from public documentation and MAME's driver (read for
// facts only).
//
// Video is rendered from a snapshot taken at VBLANK, one 224-pixel line at
// a time, in portrait (as the cabinet's rotated monitor shows it), so a
// device can stream lines to the LCD without a 129 KB frame buffer.
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/namco_wsg.h"

#include <cstdint>

namespace pacman {

constexpr int      kScreenW = 224;
constexpr int      kScreenH = 288;
constexpr uint32_t kCpuClock = 3072000;                 // 18.432 MHz / 6
constexpr int      kCyclesPerLine = 192;                // 384 pixel clocks at 6.144 MHz
constexpr int      kLinesPerFrame = 264;
constexpr int      kVblankLine = 224;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
constexpr uint32_t kWsgClock = kCpuClock / 32;          // 96 kHz

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kCoin    = 0x010,
    kStart1  = 0x020,
    kStart2  = 0x040,
    kCoin2   = 0x080,
    kService = 0x100,   // service credit
    kTest    = 0x200,   // service mode switch (held = test screens)
};

// DIP switch bank (DSW1, read at 5080).
enum Dip : uint8_t {
    kDipCoinMask     = 0x03, kDipFreePlay = 0x00, kDip1Coin1Credit = 0x01,
                             kDip1Coin2Credits = 0x02, kDip2Coins1Credit = 0x03,
    kDipLivesMask    = 0x0c, kDipLives1 = 0x00, kDipLives2 = 0x04, kDipLives3 = 0x08, kDipLives5 = 0x0c,
    kDipBonusMask    = 0x30, kDipBonus10000 = 0x00, kDipBonus15000 = 0x10,
                             kDipBonus20000 = 0x20, kDipBonusNone = 0x30,
    kDipNormal       = 0x40,   // clear = hard
    kDipGhostNames   = 0x80,   // clear = alternate names
};
// 1 coin 1 credit, 3 lives, bonus at 10000, normal difficulty, normal names.
constexpr uint8_t kDipDefault = kDip1Coin1Credit | kDipLives3 | kDipBonus10000 | kDipNormal | kDipGhostNames;

// The ROM set (MAME "pacman", a clone of "puckman"); regions in this order.
enum Region { kRegionCpu, kRegionGfx, kRegionProms, kRegionSound, kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* cpu;     // 16 KB program
    const uint8_t* gfx;     // 4 KB tiles + 4 KB sprites
    const uint8_t* proms;   // 32 B palette + 256 B colour lookup
    const uint8_t* sound;   // 256 B waveforms (+ unused timing PROM)
};

// What the video hardware shows: copied at VBLANK.
struct VideoState {
    uint8_t vram[0x400];
    uint8_t cram[0x400];
    uint8_t sprite[16];     // 4FF0-4FFF: code << 2 | flips, colour
    uint8_t sprite_xy[16];  // 5060-506F
    bool    flip;
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw1) { dsw1_ = dsw1; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-287) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    z80::Cpu& cpu() { return cpu_; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return watchdog_resets_; }

protected:  // Ms. Pac-Man's auxiliary board (mspacman.h) builds on these
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);

    void decode_gfx();
    void vblank();
    uint8_t in0() const;
    uint8_t in1() const;

    Roms     roms_{};
    z80::Cpu cpu_;
    namco_wsg::Wsg wsg_;

    uint8_t  vram_[0x800] = {};     // 4000-47FF: tile codes, then colours
    uint8_t  ram_[0x400] = {};      // 4C00-4FFF, sprite codes / colours at 4FF0
    uint8_t  sprite_xy_[16] = {};   // 5060-506F (write only)
    uint8_t  latch_ = 0;            // 5000-5007: bit 0 IRQ enable, 1 sound, 3 flip
    uint8_t  vector_ = 0;           // OUT (0): IM 2 vector
    uint8_t  dsw1_ = kDipDefault;
    uint16_t buttons_ = 0;
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    int      carry_ = 0;            // cycles run past the last slice
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;

    VideoState video_{};
    // Graphics decoded once into display orientation, 2 bits a pixel,
    // pixel x in bits 2x+1..2x.
    uint16_t tile_rows_[256 * 8] = {};
    uint32_t sprite_rows_[64 * 16] = {};
    uint16_t pen_[64 * 4] = {};     // RGB565 per (colour, pixel value)
    uint8_t  opaque_[64] = {};      // per colour: bit p = pixel value p isn't transparent
};

} // namespace pacman
