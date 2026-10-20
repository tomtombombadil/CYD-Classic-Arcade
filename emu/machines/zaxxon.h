// Zaxxon (Sega, 1982) arcade machine, written for this project (MIT).
// Z80 at 3.041 MHz (48.66 MHz / 16), 60.0 Hz frames (264 lines x 192 CPU
// cycles), VBLANK interrupt in IM 1 held until the game turns its enable
// off, sound by discrete circuits that an 8255 PPI switches
// (emu/sound/zaxxon_sound). Hardware facts from public documentation and
// MAME's driver (read for facts only).
//
// The board draws 256 x 224 (lines 16-239) and its monitor is turned 90
// degrees clockwise: 224 x 256 as the player sees it. Three layers:
//   - the background: a 256 x 4096-pixel picture of 8 x 8 tiles (32 x 512,
//     codes and colours from the tile map ROMs, 3 bits a pixel) that the
//     11-bit position register scrolls two lines a step, and that the
//     board skews by half a pixel a line - the isometric view;
//   - 32 sprites of 32 x 32, 3 bits a pixel, with flips;
//   - the foreground: 32 x 32 characters (2 bits) for the score, fuel and
//     altitude meter, colours from a PROM by position.
//
// Video is rendered from a snapshot taken at VBLANK, one 224-pixel display
// line (= one board column) at a time, straight from the ROMs - no frame
// buffer, no pre-drawn background.
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/zaxxon_sound.h"

#include <cstdint>

namespace zaxxon {

constexpr int      kScreenW = 224;                      // as the player sees it
constexpr int      kScreenH = 256;
constexpr uint32_t kCpuClock = 3041250;                 // 48.66 MHz / 16
constexpr int      kCyclesPerLine = 192;                // 384 pixel clocks at 6.08 MHz
constexpr int      kLinesPerFrame = 264;
constexpr int      kFirstLine = 16;                     // first visible board line
constexpr int      kVblankLine = 240;                   // after the last visible line
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.0 Hz
constexpr int      kSprites = 32;

// Buttons for set_inputs() (pressed = bit set). The directions are the
// stick's as the player holds it: up = pushed forward = dive.
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kFire    = 0x010,
    kCoin    = 0x020,
    kStart1  = 0x040,
    kStart2  = 0x080,
    kCoin2   = 0x100,
    kService = 0x200,   // service credit (the third coin latch)
    kTest    = 0x400,   // service switch: an NMI = test mode
};

// DIP switch bank SW1 (read at C002); SW2 (coinage, C003) stays at 1 coin
// 1 credit. "Sound" is no attract-mode switch: the IRQ handler (00DE)
// updates the sound outputs only with it on and the game out of its
// attract mode (6004 bit 0), so the attract mode is silent either way.
enum Dip : uint8_t {
    kDipBonusMask   = 0x03, kDipBonus40000 = 0x00, kDipBonus20000 = 0x01, kDipBonus30000 = 0x02,
                            kDipBonus10000 = 0x03,
    kDipUnused      = 0x0c,   // switches 3-4, not used (on)
    kDipLivesMask   = 0x30, kDipLives3 = 0x30, kDipLives4 = 0x10, kDipLives5 = 0x20, kDipFreePlay = 0x00,
    kDipSound       = 0x40,   // "Sound": off = no sound at all (the attract mode is silent either way)
    kDipCocktail    = 0x80,
};
// 10000 bonus, 3 ships, sound on, upright (the factory setting).
constexpr uint8_t kDipDefault = kDipBonus10000 | kDipUnused | kDipLives3 | kDipSound;
constexpr uint8_t kCoinageDefault = 0x33;              // both slots 1 coin 1 credit

// The ROM set (MAME "zaxxon", set 1 rev D, or "zaxxon2", set 2); regions
// in this order.
enum Region { kRegionCpu, kRegionChars, kRegionBgGfx, kRegionSprites, kRegionMap, kRegionProms, kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* cpu;      // 20 KB program
    const uint8_t* chars;    // 4 KB: 256 characters, 2 planes of 2 KB
    const uint8_t* bg;       // 24 KB: 1024 background tiles, 3 planes of 8 KB
    const uint8_t* sprites;  // 24 KB: 64 sprites of 32 x 32, 3 planes of 8 KB
    const uint8_t* map;      // 32 KB: the background's tile codes, then their high bits + colours
    const uint8_t* proms;    // 256 B palette + 256 B character colours
};

// The background and sprite maths, shared by the renderer and the tests
// (board coordinates: x 0-255 along a line, y the line 16-239).
// The background pixel shown at (x, y) for position register `pos`: row
// `y - 2 pos` of the 4096-line picture, column `x - y / 2 + 63` (the skew).
inline int bg_row(uint16_t pos, int y) { return (y - 2 * pos) & 4095; }
inline int bg_column(int x, int y) { return (x - (y >> 1) + 0x3f) & 255; }
// A sprite's top line and left column from its Y / X bytes (the board
// compares line + Y + 0xF2 with 0xE0-0xFF; the picture starts a line late).
inline int sprite_top(uint8_t y) { return (0xef - y) & 0xff; }
inline int sprite_left(uint8_t x) { return (x + 0xf0) & 0xff; }

// What the video hardware shows: copied at VBLANK.
struct VideoState {
    struct Sprite {
        uint8_t x, y;       // left column, top line (board, wrapping at 256)
        uint8_t code;       // 0-63
        uint8_t flags;      // bit 0 flip x, bit 1 flip y
        uint8_t pens;       // colour * 8
    };
    uint8_t  fg[0x400];     // 8000-83FF: character codes, 32 x 32
    Sprite   sprite[kSprites];  // in drawing order (the last one on top)
    uint8_t  sprite_count;
    uint16_t bg_pos;        // 11 bits
    bool     bg_on;
    uint8_t  bg_bank;       // 0 or 0x80: the palette half the background uses
    uint8_t  fg_bank;       // 0 or 0x80: the characters' half
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons);
    void set_dips(uint8_t sw1) { sw1_ = sw1; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-255) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    z80::Cpu& cpu() { return cpu_; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return 0; }       // the board has none
    const zaxxon_sound::Board& sound() const { return sound_; }
    uint8_t   latch1() const { return latch1_; }
    uint8_t   latch2() const { return latch2_; }
    uint16_t  bg_pos() const { return bg_pos_; }
    uint8_t   ram(uint16_t addr) const { return ram_[addr & 0xfff]; }   // 6000-6FFF
    // Cycles skipped in the main loop's idle passes (tests); idle skipping
    // can be turned off to compare.
    uint64_t  idle_cycles() const { return idle_cycles_; }
    void      set_idle_skip(bool on) { idle_skip_ = on; }
    uint32_t  bg_misses() const { return bg_misses_; }
    uint16_t  pen(int entry) const { return pen_[entry & 0xff]; }      // RGB565 of a palette entry

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);

    void decode_chars();
    void decode_bg_tile(int code, uint32_t* cols) const;
    void vblank();
    void run_to(int cycle);
    bool idle_pass(int& cycles, int& fetches) const;
    void audio_to(int cycle);
    void ppi_write(int reg, uint8_t v);
    uint8_t sw100() const;

    Roms     roms_{};
    z80::Cpu cpu_;
    zaxxon_sound::Board sound_;

    uint8_t  ram_[0x1000] = {};     // 6000-6FFF
    uint8_t  fg_ram_[0x400] = {};   // 8000-83FF (mirrored to 9FFF)
    uint8_t  sprite_ram_[0x100] = {};   // A000-A0FF (mirrored to BFFF)
    uint8_t  latch1_ = 0;           // C000-C007: 0-2 coin / service enables, 3-4 counters, 6 flip
    uint8_t  latch2_ = 0;           // E0F0-E0FB: 0 IRQ enable, 1 fg colour, 6 bg colour, 7 bg on
    uint16_t bg_pos_ = 0;           // E0F8 low 8, E0F9 high 3 bits
    uint8_t  ppi_[3] = {0xff, 0xff, 0xff};  // the 8255's outputs (sound, active low)
    uint8_t  coin_status_ = 0;      // bits 0-2: coin A, coin B, service latched
    uint8_t  sw1_ = kDipDefault;
    uint16_t buttons_ = 0;
    int      pos_ = 0;              // frame cycle reached
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;      // this frame's audio while it runs
    int      audio_n_ = 0, audio_done_ = 0;
    bool     idle_skip_ = true;
    bool     loop_known_ = false;       // the program's main loop is set 1's (idle skipping)
    uint64_t idle_cycles_ = 0;

    VideoState video_{};
    // Characters decoded once, by board column: 8 rows x 2 bits, row y in
    // bits 2y+1..2y.
    uint16_t char_cols_[256 * 8] = {};
    uint16_t pen_[256] = {};        // RGB565 per palette entry
    uint8_t  char_colour_[32 * 8] = {};    // per (column, row / 4): colour * 8
    uint32_t sprite_cols_used_[64] = {};   // per sprite code: bit c = column c draws something
    // Background tiles decoded as drawn (1024 in the ROMs would take 32 KB):
    // 8 words a tile, one a column, row y in bits 4y+2..4y; tag = code + 1.
    static constexpr int kBgCacheSlots = 256;
    mutable uint32_t bg_cache_[kBgCacheSlots * 8] = {};
    mutable uint16_t bg_tag_[kBgCacheSlots] = {};
    mutable uint32_t bg_misses_ = 0;
};

} // namespace zaxxon
