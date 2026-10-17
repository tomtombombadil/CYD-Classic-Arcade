// Tempest (Atari, 1981) arcade machine, written for this project (MIT).
// 6502 at 1.512 MHz, IRQ from the 3 kHz clock / 12 (246 Hz, cleared by
// the watchdog write), the colour Analog Vector Generator (atari_avg)
// drawing from vector RAM / ROM through a 16-entry colour RAM, the math
// box (atari_mathbox), two POKEYs for sound and inputs (the spinner, the
// buttons, the switches), and the EAROM for the high scores (kept in RAM
// only). Hardware facts from public documentation and MAME's driver (read
// for facts only).
//
// The picture: the AVG's 580 x 570 screen-unit window at its true, nearly
// square shape (square units: the web is round), 570 units over 240 rows,
// centred in a 320 x 240 landscape frame with black sides (Tom,
// 2026-10-17: the cabinet's portrait tube stretched it tall). Drawn one
// output line at a time from the last finished vector list
// (vector_display). The AVG runs
// in an endless loop on the real board; here the list is drawn from its
// start to its jump back each frame.
#pragma once

#include "../cpu/m6502.h"
#include "../rom/romset.h"
#include "../sound/pokey.h"
#include "atari_avg.h"
#include "atari_mathbox.h"
#include "vector_display.h"

#include <cstdint>

namespace tempest {

constexpr int      kScreenW = 320;     // landscape; the window fills the
constexpr int      kScreenH = 240;     // height, 244 px wide, centred
constexpr uint32_t kCpuClock = 1512000;
constexpr int      kIrqCycles = 6144;                   // 3 kHz / 12
constexpr int      kCyclesPerFrame = 4 * kIrqCycles;    // 24576 -> 61.5 Hz
constexpr int      kWatchdogCycles = 131072;            // 3 kHz / 256
// The AVG's screen (units of 1 at linear scale 0): 580 x 570, centre.
constexpr int      kAvgW = 580, kAvgH = 570;

// Buttons for set_inputs() (pressed = bit set; the board reads fire,
// Superzapper and the starts high when pressed). The spinner: set_spinner()
// / set_analog() in the game table's machine, or the two spin buttons
// (a steady speed).
enum Button : uint16_t {
    kFire      = 0x001,
    kZap       = 0x002,     // Superzapper
    kSpinLeft  = 0x004,     // counter-clockwise
    kSpinRight = 0x008,     // clockwise
    kCoin      = 0x010,     // left coin
    kStart1    = 0x020,
    kStart2    = 0x040,
    kCoin2     = 0x080,     // centre coin
    kTest      = 0x100,     // self-test switch
    kDiagStep  = 0x200,
    kSlam      = 0x400,
};

// The pricing switches (N13, read at 0D00). The game options (L12, 0E00)
// stay at their default: 3 lives, bonus every 20000, English, 1 credit
// minimum; the math box switches medium difficulty, rating 1-9.
enum Dip : uint8_t {
    kDip1Coin1Credit = 0x00, kDip2Coins1Credit = 0x01, kDipFreePlay = 0x02, kDip1Coin2Credits = 0x03,
};
constexpr uint8_t kDipDefault = kDip1Coin1Credit;

// The ROM set: MAME "tempest" (rev 3, 4 KB chips) or "tempest3" (the same
// program on 2 KB chips, older MAME's "tempest") - romset variants 1 and
// 2. The AVG and math box PROMs aren't needed.
enum Region { kRegionCpu, kRegionVector, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 20 KB program at 9000-DFFF (D000-DFFF also at F000)
    const uint8_t* vector;  // 4 KB vector ROM at 3000-3FFF
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw) { dsw1_ = dsw; }
    // The spinner's speed this frame, in 1/256 counts a frame (+ turns the
    // player clockwise on the screen, which counts the knob down); added
    // to the steady speed of the spin buttons.
    void set_spinner(int speed) { spin_speed_ = speed; }

    // Runs one frame (4 IRQs). Writes its audio (about audio_rate / 61.5
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Display line y (0-239) of the last vector list: 320 RGB565 pixels.
    void render_line(int y, uint16_t* out) { display_.render_line(y, out); }
    // A copy of the last picture for drawing on another core (heap, 20
    // bytes a segment of the biggest picture so far; false if it can't be
    // had), and its lines.
    bool snapshot() { return shot_.take(display_); }
    void render_snapshot_line(int y, uint16_t* out) { shot_.render_line(y, out); }

    m6502::Cpu& cpu() { return cpu_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    const vector_display::Display& display() const { return display_; }
    const pokey::Pokey& pokey(int n) const { return pokey_[n & 1]; }
    const atari_mathbox::Mathbox& mathbox() const { return mathbox_; }
    uint8_t  knob() const { return uint8_t(spin_pos_ >> 8) & 15; }
    uint32_t vg_runs() const { return vg_runs_; }
    const uint8_t* earom() const { return earom_; }

    // A spin button's steady speed: 1.5 counts a frame, about a lane every
    // 5 frames (the game reads the 4-bit count's change, so 8 a read at
    // most; 4 counts a frame still tracks cleanly).
    static constexpr int kButtonSpin = 384;

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t vec_read(void* ctx, uint16_t addr);
    static void    vec_line(void* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level);

    uint32_t now() const { return total_ + uint32_t(fc_ + cpu_.elapsed()); }
    void vg_draw();
    bool waiting_for_irq();
    static constexpr int kIdleSlice = 1024;      // cycles between wait-loop checks
    void audio_to(int cycle);
    uint8_t in1() const;
    uint8_t in2() const;
    void earom_control(uint8_t v);

    Roms       roms_{};
    m6502::Cpu cpu_;
    atari_avg::Avg avg_;
    atari_mathbox::Mathbox mathbox_;
    pokey::Pokey pokey_[2];
    vector_display::Display display_;
    vector_display::Copy    shot_;

    uint8_t  ram_[0x800] = {};      // 0000-07FF
    uint8_t  vram_[0x1000] = {};    // 2000-2FFF vector RAM
    uint8_t  colorram_[16] = {};    // 0800-080F
    uint8_t  earom_[64] = {};       // high scores (not kept over power-off)
    uint8_t  earom_addr_ = 0, earom_data_ = 0, earom_ctl_ = 0, earom_out_ = 0;
    uint8_t  dsw1_ = kDipDefault;
    uint16_t buttons_ = 0;
    uint8_t  player2_ = 0;          // 60E0 bit 2: the second player's controls (cocktail)
    bool     swap_ = false;

    int      spin_speed_ = 0;
    uint32_t spin_pos_ = 0;         // knob position, 1/256 counts

    uint32_t total_ = 0;            // CPU cycles before this frame
    int      fc_ = 0;               // cycles into this frame
    uint32_t vg_busy_until_ = 0;
    bool     vg_started_ = false;   // VGGO seen since the last picture
    uint32_t vg_runs_ = 0;
    uint32_t watchdog_at_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    uint16_t pen_[16][16] = {};     // pixel per (colour RAM entry, intensity)

    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    float    dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace tempest
