// Missile Command (Atari, 1980) arcade machine, written for this project
// (MIT). One 6502 at 1.25 MHz (10 MHz / 8; half that while the beam draws
// lines 224-255), 16 KB of RAM that is also the picture: 256 x 256 pixels
// of 2 bits (four pixels a byte, one bit of each in either nibble), plus a
// third colour bit for the bottom 32 lines kept elsewhere in the same RAM.
// The CPU reaches pixels through MADSEL: an opcode whose low five bits are
// 00001 (the (zp,X) instructions) sends its data access, five cycles
// later, to the pixel logic - the address is the pixel (Y high, X low),
// data bits 7-5 its colour. An 8-entry colour RAM (1 bit each of red,
// green, blue), one POKEY for sound (its ALLPOT reads the game option
// switches), the trackball (two 4-bit counters), three fire buttons, the
// IRQ from the line counter (32V, latched at each opcode fetch), and the
// watchdog. Hardware facts from public documentation and MAME's driver
// (read for facts only).
//
// The picture: lines 25-255 (VBLANK ends at 24), 256 x 231, landscape. The
// cocktail flip isn't drawn (the upright cabinet never sets it).
#pragma once

#include "../cpu/m6502.h"
#include "../rom/romset.h"
#include "../sound/pokey.h"

#include <cstdint>

namespace missile {

constexpr int      kScreenW = 256;
constexpr int      kScreenH = 231;
constexpr int      kFirstLine = 25;         // the first line shown
// Time is counted in 1.25 MHz ticks (the POKEY's clock, and the CPU's but
// for lines 224-255, where a CPU cycle takes two): 80 a line (320 pixels
// at 5 MHz), 256 lines a frame -> 61.04 Hz.
constexpr uint32_t kClock = 1250000;
constexpr int      kLineTicks = 80;
constexpr int      kTicksPerFrame = 256 * kLineTicks;  // 20480
constexpr int      kSlowLine = 224;         // lines 224-255: the CPU at half speed
constexpr int      kWatchdogFrames = 8;

// Buttons for set_inputs() (pressed = bit set). The trackball: set_trackball()
// for the stick, or the four direction buttons (a steady speed).
enum Button : uint16_t {
    kFireLeft   = 0x0001,   // the left base (Alpha)
    kFireCentre = 0x0002,   // the centre base (Delta)
    kFireRight  = 0x0004,   // the right base (Omega)
    kCoin       = 0x0008,   // left coin
    kStart1     = 0x0010,
    kStart2     = 0x0020,
    kCoin2      = 0x0040,   // centre coin
    kTest       = 0x0080,   // self-test switch
    kTilt       = 0x0100,   // slam switch
    kUp         = 0x0200,   // the trackball at a steady speed
    kDown       = 0x0400,
    kLeft       = 0x0800,
    kRight      = 0x1000,
};

// The pricing switches (R10, read at 4A00): coinage in bits 0-1, the coin
// mechanisms' multipliers, the language (bits 5-6: English). The game
// options (R8, POKEY's ALLPOT) stay at MAME's defaults: 6 cities, the
// bonus credit for 4 coins, the mini trackball, a bonus city every 10000,
// upright.
enum Dip : uint8_t {
    kDip1Coin1Credit = 0x80, kDip2Coins1Credit = 0x81, kDipFreePlay = 0x82, kDip1Coin2Credits = 0x83,
};
constexpr uint8_t kDipDefault = kDip1Coin1Credit;
constexpr uint8_t kGameOptions = 0x73;

// The ROM set: MAME "missile" (rev 3), "missile2" (rev 2, Tom's) or
// "missile1" (rev 1) - romset variants 1-3, all six 2 KB chips at
// 5000-7FFF. The write-enable PROM (035826-01) isn't needed.
enum Region { kRegionCpu, kRegionCount };
extern const romset::Set kRomSet;

class Machine {
public:
    Machine() = default;
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    ~Machine();

    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const uint8_t* rom, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw) { r10_ = dsw; }
    // The trackball's speed this frame, in 1/256 counts a frame (+ = right /
    // up on the screen); added to the steady speed of the direction buttons.
    void set_trackball(int dx, int dy)
    {
        track_vx_ = dx;
        track_vy_ = dy;
    }

    // Runs one frame (8 changes of the IRQ line). Writes its audio (about
    // audio_rate / 61.04 samples) to `audio` (up to `cap`; null = none) and
    // returns the count.
    int run_frame(int16_t* audio, int cap);

    // Shown line y (0-230 = lines 25-255): 256 RGB565 pixels.
    void render_line(int y, uint16_t* out) const { draw_line(vram_ + 0x200, pen_, y, out); }
    // A copy of the picture (15.5 KB from emu::alloc at the first call;
    // false if it can't be had) for drawing on another core, and its lines.
    bool snapshot();
    void render_snapshot_line(int y, uint16_t* out) const;

    // Skips the passes of two wait loops (on by default; exact - see
    // idle_cycles()). Tests turn it off to compare.
    void set_idle_skip(bool on) { idle_skip_ = on; }
    uint32_t idle_cycles_skipped() const { return idle_skipped_; }

    m6502::Cpu& cpu() { return cpu_; }
    uint8_t* vram() { return vram_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    const pokey::Pokey& pokey() const { return pokey_; }
    uint16_t pen(int i) const { return pen_[i & 7]; }
    bool     flipped() const { return flip_; }
    uint32_t madsel_count() const { return madsel_count_; }
    // The trackball's counters now (4 bits each, as the CPU reads them).
    uint8_t  track_x() const { return uint8_t(track_x_ >> 8) & 15; }
    uint8_t  track_y() const { return uint8_t(track_y_ >> 8) & 15; }

    // Where the third colour bit of pixel address `pix` (Y << 8 | X, Y >=
    // 224) lives in the RAM: bit (X & 7) of this byte.
    static uint16_t bit3_addr(uint16_t pix)
    {
        return uint16_t(((pix & 0x0800) >> 1) | ((~pix & 0x0800) >> 2) | ((pix & 0x07f8) >> 2) |
                        ((pix & 0x1000) >> 12));
    }

    // The d-pad's steady speed: 3 counts a frame (in 1/256 counts). A count
    // moves the crosshair a pixel (the mini trackball setting); the game
    // reads the counters at each IRQ, four times a frame, and takes their
    // 4-bit change, so up to ~28 counts a frame would still read right.
    static constexpr int kButtonSpeed = 768;

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t on_opcode(void* ctx, uint16_t addr, uint8_t op, bool wrote);
    // `ram` = the RAM from 0200 on (the third colour bits' area, then the
    // picture), live or the snapshot's copy.
    static void    draw_line(const uint8_t* ram, const uint16_t* pen, int y, uint16_t* out);

    uint8_t  pixel_read(uint16_t pix);
    void     pixel_write(uint16_t pix, uint8_t v);
    // Ticks into the frame now (the instruction running started there).
    int      now() const { return tick_ + cpu_.elapsed() * rate_; }
    int      idle_cycles(int left);
    static constexpr int kSlice = 256;      // cycles between wait-loop checks
    uint8_t  in0() const;
    uint8_t  in1() const;
    void     audio_to(int tick);

    const uint8_t* rom_ = nullptr;
    m6502::Cpu   cpu_;
    pokey::Pokey pokey_;

    uint8_t  vram_[0x4000] = {};    // 0000-3FFF: work RAM and the picture
    uint16_t pen_[8] = {};          // the colour RAM as RGB565
    uint8_t* shot_ = nullptr;       // snapshot(): the picture's RAM 0200-3FFF + pens

    uint16_t buttons_ = 0;
    uint8_t  r10_ = kDipDefault;
    bool     swap_ = false;
    bool     ctrld_ = false;        // 4800 bit 0: read the trackball, not IN0
    bool     flip_ = false;         // cocktail flip (not drawn)
    bool     madsel_ = false;       // this instruction's data access is a pixel's
    bool     irq_state_ = false;    // the 32V flip-flop / acknowledge
    bool     irq_pin_ = false;      // latched at each opcode fetch
    uint32_t madsel_count_ = 0;
    bool     idle_skip_ = true;
    uint32_t idle_skipped_ = 0;

    int      track_vx_ = 0, track_vy_ = 0;
    uint32_t track_x_ = 0, track_y_ = 0;           // counters, 1/256 counts, at the frame's start
    int      frame_vx_ = 0, frame_vy_ = 0;         // this frame's speeds

    uint32_t total_ = 0;            // ticks before this frame (the POKEY's clock)
    int      tick_ = 0;             // ticks into this frame at the run's start
    int      rate_ = 1;             // ticks a CPU cycle now (2 on lines 224-255)
    uint32_t frame_ = 0;
    int      watchdog_frames_ = 0;
    int      watchdog_resets_ = 0;

    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    float    dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace missile
