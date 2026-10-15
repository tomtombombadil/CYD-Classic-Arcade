// Mr. Do! (Universal, 1982) arcade machine, written for this project (MIT).
// Z80 at 4.1 MHz (8.2 MHz / 2), 59.94 Hz frames (312 x 262 pixel clocks at
// 4.9 MHz), the VBLANK interrupt held until the CPU takes it (IM 1), two
// SN76489 sound chips at 4.1 MHz, two 32 x 32 tile layers of 8 x 8 tiles
// (2 bits a pixel, 512 tiles each; the background one scrolls) and 64
// sprites of 16 x 16 from palette / lookup PROMs. Hardware facts from public
// documentation and MAME's driver (read for facts only).
//
// The monitor is turned a quarter: the board's 240 x 192 picture (x 8-247,
// y 32-223 of 256 x 256) is shown 192 wide and 240 tall, the board's x
// running up the screen. Rendered line by line from the machine's state at
// the start of VBLANK (run_frame() ends there).
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/sn76489.h"

#include <cstdint>

namespace mrdo {

constexpr int      kScreenW = 192;
constexpr int      kScreenH = 240;
constexpr uint32_t kCpuClock = 4100000;             // 8.2 MHz / 2
// 312 x 262 pixel clocks at 4.9 MHz = 16.683 ms; at 4.1 MHz 68,398.7 cycles.
constexpr int      kCyclesPerFrame = 68399;
constexpr int      kVblankCycles = 18274;           // 70 of the 262 lines
constexpr uint32_t kSoundClock = 4100000;

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kLeft    = 0x001,
    kDown    = 0x002,
    kRight   = 0x004,
    kUp      = 0x008,
    kFire    = 0x010,   // throws the power ball
    kStart1  = 0x020,
    kStart2  = 0x040,
    kCoin    = 0x080,
    kCoin2   = 0x100,
    kTilt    = 0x200,
};

// DIP switch bank 1 (read at A002); bank 2 (coinage, A003) stays at
// 1 coin 1 credit.
enum Dip : uint8_t {
    kDipDifficultyMask = 0x03, kDipEasy = 0x03, kDipMedium = 0x02, kDipHard = 0x01, kDipHardest = 0x00,
    kDipRackTestOff    = 0x04,  // clear = rack test (cheat)
    kDipSpecialEasy    = 0x08,
    kDipExtraEasy      = 0x10,
    kDipCocktail       = 0x20,  // clear = upright
    kDipLivesMask      = 0xc0, kDipLives2 = 0x00, kDipLives3 = 0xc0, kDipLives4 = 0x80, kDipLives5 = 0x40,
};
constexpr uint8_t kDipDefault = kDipEasy | kDipRackTestOff | kDipSpecialEasy | kDipExtraEasy | kDipLives3;
constexpr uint8_t kDsw2Default = 0xff;              // 1 coin 1 credit on both chutes

// The ROM set (MAME "mrdo"); regions in this order.
enum Region { kRegionCpu, kRegionFg, kRegionBg, kRegionSprites, kRegionProms, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 32 KB program
    const uint8_t* fg;      // 8 KB: front layer tiles (two 4 KB bit planes)
    const uint8_t* bg;      // 8 KB: back layer tiles
    const uint8_t* sprites; // 8 KB: 128 sprites
    const uint8_t* proms;   // palette high / low bits, sprite lookup, timing
};

// A palette entry's 8-bit level from its four resistor bits (exposed for
// tests): bit 0 150 ohm, 1 120, 2 100, 3 75.
uint8_t level(int bits);

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw1) { dsw1_ = dsw1; }

    int  run_frame(int16_t* audio, int cap);
    void render_line(int y, uint16_t* out) const;

    z80::Cpu& cpu() { return cpu_; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return 0; }  // no watchdog on this board
    const sn76489::Chip& sound(int n) const { return sn_[n]; }
    int       protection_reads() const { return protection_reads_; }
    // CPU cycles skipped in the game's wait loop, ever.
    uint64_t  idle_cycles() const { return idle_cycles_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);

    void decode_gfx();
    void run_to(int cycle);
    void sound_to(int cycle);
    struct Cell {
        uint16_t code;      // tile code x 8
        uint8_t  pens;      // colour x 4
        bool     cover;
    };
    void unpack_column(const uint8_t* vram, int col, Cell* cells) const;
    template <bool kBack>
    void draw_layer(const Cell* cells, const uint16_t* cols, int tx, int scroll_y, uint16_t* out) const;
    void list_sprites();

    Roms     roms_{};
    z80::Cpu cpu_;
    sn76489::Chip sn_[2];

    uint8_t  bg_ram_[0x800] = {};   // 8000-87FF: attributes, then codes
    uint8_t  fg_ram_[0x800] = {};   // 8800-8FFF
    uint8_t  sprite_ram_[0x100] = {};   // 9000-90FF (write only)
    uint8_t  ram_[0x1000] = {};     // E000-EFFF
    uint8_t  scroll_x_ = 0, scroll_y_ = 0;
    uint8_t  flip_ = 0;             // 9800 bit 0 (cocktail only; not drawn)
    uint8_t  dsw1_ = kDipDefault;
    uint16_t buttons_ = 0;
    int      protection_reads_ = 0;

    static constexpr int kSlice = 512;
    int      pos_ = 0;              // CPU cycles into the frame
    uint64_t idle_cycles_ = 0;
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    // This frame's sound, made as the CPU writes the chips.
    int16_t* audio_ = nullptr;
    int      audio_cap_ = 0;
    int      frame_samples_ = 0, samples_done_ = 0;
    int64_t  dc_ = 0;               // the output's coupling capacitor (16.16)

    // Graphics decoded once into display orientation, 2 bits a pixel:
    // tiles by (code, board x within the tile), 8 pixels down the board;
    // sprites by (code, board x), 16 pixels down the board.
    uint16_t fg_cols_[512 * 8] = {};
    uint16_t bg_cols_[512 * 8] = {};
    uint32_t sprite_cols_[128 * 16] = {};
    struct Sprite {
        uint8_t x, code, attr, y;   // y: 256 - board y
    };
    Sprite   sprites_[64] = {};     // on screen at the last VBLANK, in drawing order
    int      sprite_count_ = 0;
    // The tile columns render_line() last unpacked (for this frame).
    mutable Cell     bg_cells_[32] = {}, fg_cells_[32] = {};
    mutable int      bg_col_ = -1, fg_col_ = -1;
    mutable uint32_t cells_frame_ = ~0u;
    uint16_t tile_pen_[256] = {};   // RGB565 per (colour, pixel value)
    uint16_t sprite_pen_[64] = {};
};

} // namespace mrdo
