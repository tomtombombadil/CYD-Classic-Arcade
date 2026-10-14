// Rally-X (Namco, 1980) arcade machine, written for this project (MIT).
// Z80 at 3.072 MHz, 60.606 Hz frames (264 lines x 192 CPU cycles), VBLANK
// interrupt (IM 2 or IM 0, the byte latched by OUT (0)), Namco 3-voice WSG
// (emu/sound/namco_wsg) and the analog crash sound (emu/sound/rallyx_bang).
// Hardware facts from public documentation and MAME's driver (read for
// facts only).
//
// The monitor is upright in a landscape cabinet: 288 x 224 as the player
// sees it, no rotation. The left 224 pixels are a 256 x 256 playfield
// (32 x 32 tiles) that scrolls in X and Y; the right 64 pixels are a fixed
// 8 x 32-tile area with the score and the radar, whose dots (cars, flags)
// are 4 x 4 shapes from a PROM. Six 16 x 16 sprites; tiles can be marked
// to cover them.
//
// Video is rendered from a snapshot taken at VBLANK, one 288-pixel line at
// a time, so a device can stream lines to the LCD without a frame buffer.
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/namco_wsg.h"
#include "../sound/rallyx_bang.h"

#include <cstdint>

namespace rallyx {

constexpr int      kScreenW = 288;
constexpr int      kScreenH = 224;
constexpr uint32_t kCpuClock = 3072000;                 // 18.432 MHz / 6
constexpr int      kCyclesPerLine = 192;                // 384 pixel clocks at 6.144 MHz
constexpr int      kLinesPerFrame = 264;
constexpr int      kFirstLine = 16;                     // first visible line
constexpr int      kVblankLine = 240;                   // after the last visible line
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
constexpr uint32_t kWsgClock = kCpuClock / 32;          // 96 kHz
constexpr int      kPlayfieldW = 224;                   // scrolling part; the radar is right of it
constexpr int      kSprites = 6;                        // sprite slots 0x14-0x1E (even)
constexpr int      kDots = 12;                          // radar dots 0x14-0x1F

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kSmoke   = 0x010,   // the button: smoke screen
    kCoin    = 0x020,
    kStart1  = 0x040,
    kStart2  = 0x080,
    kCoin2   = 0x100,
    kService = 0x200,   // service credit
};

// DIP switch bank (read at A100). Bonus car scores depend on the
// difficulty setting (with the default: 20000 / 40000 / 60000).
enum Dip : uint8_t {
    kDipServiceOff   = 0x01,   // clear = service mode
    kDipBonusMask    = 0x06, kDipBonusNone = 0x00, kDipBonusLow = 0x02, kDipBonusMid = 0x04, kDipBonusHigh = 0x06,
    kDipLevelMask    = 0x38, kDip1CarMedium = 0x10, kDip1CarHard = 0x28, kDip2CarsEasy = 0x00,
                             kDip2CarsMedium = 0x18, kDip2CarsHard = 0x30, kDip3CarsEasy = 0x08,
                             kDip3CarsMedium = 0x20, kDip3CarsHard = 0x38,
    kDipCoinMask     = 0xc0, kDipFreePlay = 0x00, kDip2Coins1Credit = 0x40, kDip1Coin2Credits = 0x80,
                             kDip1Coin1Credit = 0xc0,
};
// 1 coin 1 credit, 3 cars at medium difficulty, bonus car at 20000.
constexpr uint8_t kDipDefault = kDip1Coin1Credit | kDip3CarsMedium | kDipBonusLow | kDipServiceOff;

// The ROM set (MAME "rallyx"); regions in this order.
enum Region { kRegionCpu, kRegionGfx, kRegionDots, kRegionProms, kRegionSound, kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* cpu;     // 16 KB program
    const uint8_t* gfx;     // 4 KB: 256 tiles, or the same bytes as 64 sprites
    const uint8_t* dots;    // 256 B radar dot shapes (8 used)
    const uint8_t* proms;   // 32 B palette + 256 B colour lookup (+ 2 unused PROMs)
    const uint8_t* sound;   // 256 B waveforms (+ unused timing PROM)
};

// The scroll and radar maths, shared by the renderer and the tests.
// The playfield sits 3 pixels left of where the scroll registers alone put
// it (on the real board the visible field starts 3 clocks late).
constexpr int kScrollDx = 3;
// Playfield (tilemap) X shown at screen x (0-223), 0-255.
inline int playfield_x(uint8_t scroll_x, int x) { return (x + scroll_x - kScrollDx) & 0xff; }
// Playfield Y shown on display line y (0-223), 0-255.
inline int playfield_y(uint8_t scroll_y, int y) { return (y + kFirstLine + scroll_y) & 0xff; }
// Radar / score area: the 8-column tilemap column shown at screen x
// (224-287). Its 64 pixels repeat across the 256-pixel line counter, so
// x 224 shows column 4: columns 4-7, then 0-3.
inline int radar_column(int x) { return (x >> 3) & 7; }
// A radar dot's top-left corner on screen, from its X / Y registers and
// its attribute (bit 0 clear = X + 256). y may be off screen.
struct Point { int x, y; };
inline Point dot_position(uint8_t x_reg, uint8_t y_reg, uint8_t attr)
{
    return {x_reg + ((~attr & 1) << 8), 253 - y_reg - kFirstLine};
}
// The dot shape (0-7) an attribute selects.
inline int dot_shape(uint8_t attr) { return ((attr >> 1) & 7) ^ 7; }
// A sprite's top-left corner on screen from its registers (x low byte, the
// attribute with X bit 8 in bit 7, y).
inline Point sprite_position(uint8_t x_lo, uint8_t attr, uint8_t y_reg)
{
    return {x_lo + ((attr & 0x80) << 1), 241 - y_reg - kFirstLine};
}

// What the video hardware shows: copied at VBLANK.
struct VideoState {
    uint8_t bg_code[0x400];     // 8400-87FF: playfield tile codes, 32 x 32
    uint8_t bg_attr[0x400];     // 8C00-8FFF: colour, priority, flips
    uint8_t fg_code[32 * 8];    // 8000-83FF columns 0-7: radar / score tiles
    uint8_t fg_attr[32 * 8];    // 8800-8BFF columns 0-7
    uint8_t sprite[2 * kSprites];       // 8014-801F: code << 2 | flips, x low
    uint8_t sprite2[2 * kSprites];      // 8814-881F: y, colour | x bit 8
    uint8_t dot_x[kDots];               // 8034-803F
    uint8_t dot_y[kDots];               // 8834-883F
    uint8_t dot_attr[kDots];            // A004-A00F
    uint8_t scroll_x, scroll_y;
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw) { dsw_ = dsw; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-223) of a snapshot into 288 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    z80::Cpu& cpu() { return cpu_; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return watchdog_resets_; }
    uint8_t   latch() const { return latch_; }
    const rallyx_bang::Bang& bang() const { return bang_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);

    void decode_gfx();
    void vblank();
    void audio_to(int cycle);
    uint8_t in0() const;
    uint8_t in1() const;

    Roms     roms_{};
    z80::Cpu cpu_;
    namco_wsg::Wsg wsg_;
    rallyx_bang::Bang bang_;

    uint8_t  vram_[0x1000] = {};    // 8000-8FFF: radar tiles + sprites, playfield, their attributes
    uint8_t  ram_[0x800] = {};      // 9800-9FFF
    uint8_t  dot_attr_[16] = {};    // A000-A00F (write only)
    uint8_t  scroll_x_ = 0, scroll_y_ = 0;
    uint8_t  latch_ = 0;            // A180-A187: 0 BANG, 1 IRQ enable, 2 sound on, 3 flip, ...
    uint8_t  vector_ = 0;           // OUT (0): the interrupt's bus byte
    uint8_t  dsw_ = kDipDefault;
    uint16_t buttons_ = 0;
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    int      carry_ = 0;            // cycles run past the last slice
    int      slice_start_ = 0;      // frame cycle the current run() began at
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;      // this frame's audio while it runs
    int      audio_n_ = 0, audio_done_ = 0;

    VideoState video_{};
    // Graphics decoded once, 2 bits a pixel, pixel x in bits 2x+1..2x.
    uint16_t tile_rows_[256 * 8] = {};
    uint32_t sprite_rows_[64 * 16] = {};
    uint8_t  dot_rows_[8 * 4] = {};
    uint8_t  rev4_[256] = {};       // a byte of 4 pixels in reverse order
    uint16_t pen_[64 * 4] = {};     // RGB565 per (colour, pixel value)
    uint8_t  entry_[64 * 4] = {};   // palette entry (0-15) per (colour, pixel value)
    uint16_t palette_[32] = {};     // 0-15 tiles / sprites, 16-31 the dots' bank
    uint8_t  opaque_[64] = {};      // per colour: bit p = pixel value p isn't transparent
};

} // namespace rallyx
