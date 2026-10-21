// Namco's Mappy board (1983-1985: Mappy, Dig Dug II, Motos, The Tower of
// Druaga), written for this project (MIT). Two 6809s at 1.536 MHz - the
// main CPU and the sound CPU, sharing 1 KB of RAM whose first 64 bytes are
// the 15XX's registers (an 8-voice Namco waveform sound, emu/sound/
// namco_wsg) -, two custom I/O chips (56XX / 58XX, behaviour models in
// namco_io.*), a scrolling 36 x 60 tile map of 8 x 8 2-bit characters
// with fixed columns at both sides, and 64 sprites of 16 x 16 4-bit pixels
// (single, double wide / tall). 60.606 Hz frames (264 lines x 96 CPU
// cycles), both CPUs' IRQ at VBLANK. Hardware facts from public
// documentation and MAME's driver (read for facts only).
//
// Portrait 224 x 288: the board's 288 x 224 picture with the monitor
// turned 90 degrees clockwise (as Pac-Man's). A game is a `Board` (its ROM
// set and the few things that differ); Dig Dug II is the first.
#pragma once

#include "../cpu/m6809.h"
#include "../rom/romset.h"
#include "../sound/namco_wsg.h"
#include "namco_io.h"

#include <cstdint>

namespace mappy {

constexpr int      kScreenW = 224;
constexpr int      kScreenH = 288;
constexpr uint32_t kCpuClock = 1536000;                 // 18.432 MHz / 12
constexpr int      kCyclesPerLine = 96;                 // 384 pixel clocks at 6.144 MHz
constexpr int      kLinesPerFrame = 264;
constexpr int      kVblankLine = 224;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 25344 -> 60.606 Hz
constexpr uint32_t kSoundClock = 24000;                 // 15XX: 18.432 MHz / 768
constexpr int      kMaxSpriteColours = 64;              // The Tower of Druaga's 1 KB lookup PROM

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kButton1 = 0x010,   // Dig Dug II: pump
    kButton2 = 0x020,   // Dig Dug II: jackhammer
    kCoin    = 0x040,
    kCoin2   = 0x080,
    kStart1  = 0x100,
    kStart2  = 0x200,
    kService = 0x400,   // service credit
    kTest    = 0x800,   // service mode switch (SW1:1)
};

// The regions of a Mappy-board set, in this order.
enum Region {
    kRegionMain,        // main CPU program (Dig Dug II: 32 KB at 8000)
    kRegionSub,         // sound CPU program (8 KB at E000)
    kRegionTiles,       // characters, 4 KB (stored inverted)
    kRegionSprites0,    // sprite planes 0-1 (the even bytes of MAME's region)
    kRegionSprites1,    // sprite planes 2-3 (the odd bytes)
    kRegionProms,       // palette 32 B, character lookup 256 B, sprite lookup
    kRegionSound,       // waveforms, 256 B
    kRegionCount
};

// What differs between the games on this board.
struct Board {
    const romset::Set* set;
    uint16_t main_rom_base;             // 0x8000 (2 x 16 KB) or 0xA000 (3 x 8 KB)
    namco_io::Chip5x::Type chip[2];     // the I/O chips at 4800 and 4810
    int      watchdog_frames;           // 0 = not used by the game
    int      sprite_colours;            // sprite lookup PROM size / 16
    uint8_t  dsw1_default;
};

namespace digdug2 {
extern const romset::Set kRomSet;
extern const Board kBoard;
// DSW1 (56XX #1 pins 22-29, active low as wired; 1 = off): bit 0 service
// mode, 1 lives (1 = 3, 0 = 5), 2-3 coinage (11 = 1 coin 1 credit), 4-5
// bonus (11 = 30k 80k and every 80k), 6 level select, 7 freeze.
constexpr uint8_t kDipDefault = 0xff;
} // namespace digdug2

// One sprite as the frame's VideoState keeps it (board coordinates: x
// across the 288-pixel lines, y down the 224 lines).
struct Sprite {
    int16_t x, y;           // top left
    uint8_t w, h;           // 16 or 32
    uint8_t code;           // first 16 x 16 cell (size bits cleared)
    uint8_t colour;
    uint8_t flip;           // bit 0 X, bit 1 Y
};

// What the video shows: copied at VBLANK.
struct VideoState {
    uint8_t vram[0x1000];   // codes 000-7FF, attributes 800-FFF
    uint8_t scroll;
    bool    flip;
    uint8_t sprite_count;
    Sprite  sprite[64];     // in drawing order (later ones on top)
};

class Machine {
public:
    void init(const Board& board, const uint8_t* const* regions, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw1) { dsw1_ = dsw1; }
    // Idle skipping (on by default; exact - tests compare runs without it).
    void set_idle_skip(bool on) { idle_skip_ = on; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Display line y (0-287) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    m6809::Cpu& main_cpu() { return main_; }
    m6809::Cpu& sub_cpu() { return sub_; }
    const namco_io::Chip5x& io(int i) const { return io_[i]; }
    const namco_wsg::Wsg& wsg() const { return wsg_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    uint8_t  latch() const { return latch_; }
    uint8_t  scroll() const { return scroll_; }
    uint8_t* ram() { return ram_; }              // 1000-27FF
    uint8_t* shared() { return shared_; }        // 4000-43FF / sound CPU 0000-03FF
    uint8_t* vram() { return vram_; }            // 0000-0FFF
    uint16_t char_pen(int colour, int p) const { return char_pen_[colour * 4 + p]; }
    uint16_t sprite_pen(int colour, int p) const { return sprite_pen_[colour * 16 + p]; }

private:
    static uint8_t main_read(void* ctx, uint16_t addr);
    static void    main_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t sub_read(void* ctx, uint16_t addr);
    static void    sub_write(void* ctx, uint16_t addr, uint8_t v);

    void latch_write(uint16_t addr);
    void vblank();
    void capture();
    void set_ports();
    void run_to(int cycle);
    void sound_regs();
    void audio_to(int cycle);
    // The time of a CPU's memory access (the main CPU's when none runs).
    int  now() const { return cur_ ? cur_base_ + cur_->elapsed() : main_t_; }
    void sprite_column(int code, int x, uint64_t col[2]) const;

    Board          board_{};
    const uint8_t* rom_tiles_ = nullptr;
    const uint8_t* rom_spr_[2] = {};
    m6809::Cpu     main_, sub_;
    m6809::Cpu*    cur_ = nullptr;          // the CPU running now (for now())
    int            cur_base_ = 0;
    namco_io::Chip5x io_[2];
    namco_wsg::Wsg wsg_;

    uint8_t  vram_[0x1000] = {};
    uint8_t  ram_[0x1800] = {};             // 1000-27FF (sprite registers at 1780 1F80 2780)
    uint8_t  shared_[0x400] = {};
    uint8_t  sound_shadow_[0x40] = {};      // the 15XX's registers as last applied
    uint8_t  latch_ = 0;                    // LS259: 0 sub IRQ on, 1 main IRQ on, 2 flip,
                                            // 3 sound on, 4 I/O chips run, 5 sound CPU runs
    uint8_t  scroll_ = 0;
    uint8_t  dsw1_ = 0xff;
    uint16_t buttons_ = 0;
    bool     idle_skip_ = true;
    int      main_t_ = 0, sub_t_ = 0;       // each CPU's time in this frame (may run past)
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;

    VideoState video_{};
    uint16_t tile_col_[256 * 8] = {};       // per character and board column x: 8 pixels down, 2 bits each
    uint16_t char_pen_[64 * 4] = {};
    uint8_t  char_clear_[64] = {};          // bit p: pen p lets sprites show over a front character
    uint16_t sprite_pen_[kMaxSpriteColours * 16] = {};
    uint16_t sprite_opaque_[kMaxSpriteColours] = {};
};

} // namespace mappy
