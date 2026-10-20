// Battlezone (Atari, 1980) arcade machine, written for this project (MIT).
// 6502 at 1.512 MHz, NMI from the 3 kHz clock / 12 (246 Hz, off while the
// self-test switch is on), the Analog Vector Generator in its Battlezone
// form (atari_avg: monochrome, with the clip window) drawing from vector
// RAM / ROM, the math box (atari_mathbox, as Tempest's), one POKEY (sound,
// and the tread sticks / fire / starts on ALLPOT) and the analog sound
// circuits (bzone_sound). Hardware facts from public documentation and
// MAME's driver (read for facts only).
//
// The picture: the AVG's 580 x 400 screen-unit window at its true shape
// (square units: the radar is round), 580 units over the 320 columns and
// 400 over 221 rows - landscape, as the cabinet's monitor. The cabinet's
// screen had a colour overlay: red over the top fifth (radar, scores,
// messages), green below; the lines are drawn in those colours. The game
// starts a new picture every 6 NMIs (41 a second, AVG GO in the NMI
// handler); each is drawn whole into a vector list (vector_display) when
// it starts.
#pragma once

#include "../cpu/m6502.h"
#include "../rom/romset.h"
#include "../sound/bzone_sound.h"
#include "../sound/pokey.h"
#include "atari_avg.h"
#include "atari_mathbox.h"
#include "vector_display.h"

#include <cstdint>

namespace bzone {

constexpr int      kScreenW = 320;     // landscape: the 580 x 400 window
constexpr int      kScreenH = 221;     // at its shape
constexpr uint32_t kCpuClock = 1512000;
constexpr int      kNmiCycles = 6144;                   // 3 kHz / 12
constexpr int      kCyclesPerFrame = 4 * kNmiCycles;    // 24576 -> 61.5 Hz
constexpr int      kWatchdogCycles = 131072;            // 3 kHz / 256 (assumed, as Asteroids)
// The AVG's screen (units of 1 at linear scale 0): 580 x 400; the red
// overlay covers its top fifth.
constexpr int      kAvgW = 580, kAvgH = 400;
constexpr int      kRedUnits = kAvgH / 5;

// Buttons for set_inputs() (pressed = bit set). The treads: each stick
// forward or back; a tread with both pressed counts as neither.
enum Button : uint16_t {
    kFire       = 0x001,
    kLeftFwd    = 0x002,
    kLeftBack   = 0x004,
    kRightFwd   = 0x008,
    kRightBack  = 0x010,
    kCoin       = 0x020,    // coin 1
    kStart1     = 0x040,
    kStart2     = 0x080,
    kCoin2      = 0x100,
    kTest       = 0x200,    // self-test switch
    kDiagStep   = 0x400,
    kSlam       = 0x800,
};

// The game options (M10, read at 0A00): lives (bits 0-1: 2-5), the missile
// (bits 2-3: at 5000 / 10000 / 20000 / 30000), bonus tanks (bits 4-5:
// none / 15k / 25k / 50k, and 100k), language (bits 6-7: English, German,
// French, Spanish). Default: the manual's - 3 lives, missile at 10000,
// bonus at 15000 and 100000, English. The pricing switches (P10, 0C00)
// stay at 1 coin 1 credit.
constexpr uint8_t kDipDefault = 0x15;
constexpr uint8_t kPricing1Coin1Credit = 0x02;

// The ROM set: MAME "bzone" (Rev 2) or "bzonea" (Rev 1, Tom's; older
// MAME's "bzone", files named 036414.01 ...) - romset variants 1 and 2;
// only the first program chip differs. The AVG and math box PROMs aren't
// needed.
enum Region { kRegionCpu, kRegionVector, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 12 KB program at 5000-7FFF (A15 not decoded)
    const uint8_t* vector;  // 4 KB vector ROM at 3000-3FFF
};

// The tread switches from the controller (Battlezone's games.cpp entry):
// `buttons` = what the d-pad / face buttons press (the d-pad drives: up =
// both forward, left = left back + right forward, ...), `ly` / `ry` = the
// left and right sticks' Y (-512..511, up negative). A stick pushed past a
// third (as the firmware's d-pad bits) makes the sticks the treads, tank
// style - left stick = left tread, right stick = right tread - and the
// d-pad's tread bits (the left stick sets them too) are dropped.
uint16_t treads(uint16_t buttons, int ly, int ry);

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw) { dsw0_ = dsw; }

    // Runs one frame (4 NMIs). Writes its audio (about audio_rate / 61.5
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Display line y (0-220) of the last vector list: 320 RGB565 pixels.
    void render_line(int y, uint16_t* out) { display_.render_line(y, out); }
    // A copy of the last picture for drawing on another core (from
    // emu::alloc, 20 bytes a segment of the display's capacity; false if
    // it can't be had), and its lines.
    bool snapshot() { return shot_.take(display_); }
    void render_snapshot_line(int y, uint16_t* out) { shot_.render_line(y, out); }

    // The pots byte the game reads at 1828 (ALLPOT): 0 right tread back, 1
    // right forward, 2 left back, 3 left forward, 4 fire, 5 start 1, 6
    // start 2 - high when pressed.
    uint8_t in3() const;

    m6502::Cpu& cpu() { return cpu_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    const vector_display::Display& display() const { return display_; }
    const pokey::Pokey& pokey() const { return pokey_; }
    const bzone_sound::Board& sound() const { return sound_; }
    const atari_mathbox::Mathbox& mathbox() const { return mathbox_; }
    const atari_avg::Avg& avg() const { return avg_; }
    uint32_t vg_runs() const { return vg_runs_; }
    // Idle skipping (tests): off = every cycle run.
    void set_idle_skip(bool on) { idle_skip_ = on; }
    uint32_t idle_cycles() const { return idle_cycles_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t vec_read(void* ctx, uint16_t addr);

    uint32_t now() const { return total_ + uint32_t(fc_ + cpu_.elapsed()); }
    void vg_go();
    void skip_idle(int target);
    static constexpr int kIdleSlice = 1024;      // cycles between wait-loop checks
    void audio_to(int cycle);

    Roms       roms_{};
    m6502::Cpu cpu_;
    atari_avg::Avg avg_;
    atari_mathbox::Mathbox mathbox_;
    pokey::Pokey pokey_;
    bzone_sound::Board sound_;
    vector_display::Display display_;
    vector_display::Copy    shot_;

    uint8_t  ram_[0x400] = {};      // 0000-03FF
    uint8_t  vram_[0x1000] = {};    // 2000-2FFF vector RAM
    uint8_t  dsw0_ = kDipDefault;
    uint16_t buttons_ = 0;
    bool     swap_ = false;
    uint16_t pen_[2][16] = {};      // pixel per (red / green, intensity)

    uint32_t total_ = 0;            // CPU cycles before this frame
    int      fc_ = 0;               // cycles into this frame
    uint32_t vg_busy_until_ = 0;
    uint32_t vg_runs_ = 0;
    uint32_t watchdog_at_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    bool     idle_skip_ = true;
    uint32_t idle_cycles_ = 0;

    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
};

} // namespace bzone
