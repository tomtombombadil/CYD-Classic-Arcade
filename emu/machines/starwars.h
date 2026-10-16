// Star Wars (Atari, 1983) arcade machine, written for this project (MIT).
// Main board: a 6809E at 1.512 MHz (IRQ every 6144 cycles from the 3 kHz
// clock / 12, cleared by a write), ROM banked at 6000-7FFF, the colour AVG
// (atari_avg, Star Wars variant) drawing from vector RAM / ROM, the matrix
// processor, divider and random numbers (starwars_mathbox), the yoke's two
// pots through an ADC0809, the X2212 NOVRAM (kept in RAM only) and the
// watchdog. Sound board: a 6809E at 1.512 MHz, a 6532 RIOT (RAM, its timer
// IRQ, the latches' flags, the speech chip's strobes and data bus), four
// POKEYs and a TMS5220 (tms5220) fed by the sound CPU from its ROMs. The two
// boards talk through a latch each way. Hardware facts from MAME's driver
// and public schematics (read for facts only).
//
// The picture: a 4:3 colour vector monitor standing landscape; the AVG's
// 250 x 280 screen-unit window stretched over 320 x 240 as the tube does
// (MAME shows it the same way). A picture is drawn when the main CPU starts
// the AVG (it halts at the list's end); the last finished one is shown.
#pragma once

#include "../cpu/m6809.h"
#include "../rom/romset.h"
#include "../sound/pokey.h"
#include "../sound/tms5220.h"
#include "atari_avg.h"
#include "riot6532.h"
#include "starwars_mathbox.h"
#include "vector_display.h"

#include <cstdint>

namespace starwars {

constexpr int      kScreenW = 320;
constexpr int      kScreenH = 240;
constexpr uint32_t kCpuClock = 1512000;                 // both 6809Es: 12.096 MHz / 8
constexpr int      kIrqCycles = 6144;                   // 3 kHz / 12
constexpr int      kCyclesPerFrame = 4 * kIrqCycles;    // 24576 -> 61.5 Hz
constexpr int      kWatchdogCycles = 65536;             // 3 kHz / 128
constexpr uint32_t kSpeechClock = 672000;               // 12.096 MHz / 2 / 9
// The AVG's window in screen units, and its centre.
constexpr int      kAvgW = 250, kAvgH = 280;
// Segments a picture may hold: the flight instructions page has ~1660
// (the high scores ~1070, the logo ~1180, play up to ~900); 16 bytes each.
// At most kMaxActive cross one row (166 seen in 9000 frames).
constexpr int      kMaxSegments = 1700;
constexpr int      kMaxActive = 256;

// Buttons for set_inputs() (pressed = bit set). The yoke: set_yoke() / the
// four sight buttons (a steady speed).
enum Button : uint16_t {
    kFire1     = 0x0001,    // right trigger (IN0 bit 7) - also starts the game
    kFire2     = 0x0002,    // left trigger (IN1 bit 5)
    kFire3     = 0x0004,    // right thumb (IN1 bit 4)
    kFire4     = 0x0008,    // left thumb (IN0 bit 6)
    kCoin      = 0x0010,    // left coin
    kCoin2     = 0x0020,    // right coin
    kAux       = 0x0040,    // auxiliary coin
    kTest      = 0x0080,    // self-test switch
    kDiagStep  = 0x0100,
    kTilt      = 0x0200,
    kUp        = 0x0400,    // the sights: up / down / left / right at a steady speed
    kDown      = 0x0800,
    kLeft      = 0x1000,
    kRight     = 0x2000,
};

// The game options (10D, read at 4340): starting shields 6, difficulty
// hard, 1 bonus shield, demo sounds on, freeze off (MAME's defaults; the
// operator's manual's). Pricing (10E/F) is fixed at 1 coin 1 credit.
constexpr uint8_t kDipDefault = 0x98;
constexpr uint8_t kDipPricing = 0x02;

// The ROM set: MAME "starwars" (set 1); the math box PROMs either as today
// (variant 1) or as old MAME's dumps (variant 2: stray high nibbles). The
// AVG PROM isn't needed.
enum Region {
    kRegionRom0,    // 136021.214: 6000-7FFF bank 0, then bank 1 (16 KB)
    kRegionRom1,    // 136021.102: 8000-9FFF
    kRegionRom2,    // 136021.203: A000-BFFF
    kRegionRom3,    // 136021.104: C000-DFFF
    kRegionRom4,    // 136021.206: E000-FFFF
    kRegionVector,  // 136021.105: 3000-3FFF
    kRegionSound0,  // 136021.107: 4000-5FFF (and C000)
    kRegionSound1,  // 136021.208: 6000-7FFF (and E000)
    kRegionMath,    // 136021.110-113, 1 KB each
    kRegionCount
};
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* rom[5];
    const uint8_t* vector;
    const uint8_t* sound[2];
    const uint8_t* math;
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw) { dsw0_ = dsw; }
    // The yoke from a stick: -512..511 each way (+x right, +y down), added
    // to where the sight buttons have moved it.
    void set_yoke(int x, int y) { stick_x_ = x; stick_y_ = y; }

    // Runs one frame (4 IRQs). Writes its audio (about audio_rate / 61.5
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Display line y (0-239) of the last picture: 320 RGB565 pixels.
    void render_line(int y, uint16_t* out) { display_.render_line(y, out); }

    m6809::Cpu& cpu() { return cpu_; }
    m6809::Cpu& sound_cpu() { return snd_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    const vector_display::Base& display() const { return display_; }
    const tms5220::Chip& speech() const { return tms_; }
    const starwars_math::MatrixProcessor& matrix() const { return mp_; }
    const pokey::Pokey& pokey(int n) const { return pokey_[n & 3]; }
    uint32_t vg_runs() const { return vg_runs_; }
    uint32_t sound_commands() const { return sound_commands_; }
    uint8_t  adc(int ch) const;     // the ADC's value for channel 0 (pitch) / 1 (yaw)
    // Cycles each CPU really ran last frame (the rest was skipped idling).
    int      main_run() const { return main_run_; }
    int      sound_run() const { return snd_run_; }
    uint32_t math_runs() const { return math_runs_; }
    uint8_t* ram() { return ram_; }
    uint8_t* math_ram() { return math_ram_; }
    uint8_t* sound_ram() { return snd_ram_; }

    // Idle skipping (on by default; tests compare runs without it).
    void set_idle_skip(bool on) { idle_skip_ = on; }

    // The sight buttons' steady speed: ADC counts a frame (full travel,
    // 128 counts from the centre, in ~0.9 s).
    static constexpr int kSightSpeed = 2;

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t snd_read(void* ctx, uint16_t addr);
    static void    snd_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t vec_read(void* ctx, uint16_t addr);
    static void    vec_line(void* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level);

    // Main CPU cycles of all time / into this frame; sound CPU cycles of
    // all time / into this frame (a CPU's elapsed() counts only inside its
    // run).
    int64_t now() const { return total_ + cycle(); }
    int     cycle() const { return fc_ + (in_main_ ? cpu_.elapsed() : 0); }
    int64_t snd_now() const { return total_ + snd_cycle(); }
    int     snd_cycle() const { return snd_done_ + (in_snd_ ? snd_.elapsed() : 0); }
    int     run_main(int n)
    {
        in_main_ = true;
        const int r = cpu_.run(n);
        in_main_ = false;
        return r;
    }
    int     run_sound(int n)
    {
        in_snd_ = true;
        const int r = snd_.run(n);
        in_snd_ = false;
        return r;
    }
    void vg_draw();
    void set_bank(int b);
    void outlatch(int bit, bool v);
    void sound_to(int cycle);                // runs the sound board up to this point of the frame
    void audio_to(int cycle);                // renders its output up to this point
    void render_to(int cycle);
    void riot_irq();
    void riot_pa_inputs();
    void speech_strobes();
    // Idle skipping (see the .cpp): main_idle returns the cycles skipped
    // (whole turns of a wait loop, up to budget); sound_idle moves the
    // sound CPU on itself and says whether it did.
    int  main_idle(int budget);
    bool sound_idle(int budget);
    static constexpr int kIdleSlice = 512;
    uint8_t in0() const;
    uint8_t in1();

    Roms           roms_{};
    m6809::Cpu     cpu_;
    m6809::Cpu     snd_;
    atari_avg::Avg avg_;
    starwars_math::MatrixProcessor mp_;
    starwars_math::Divider div_;
    riot6532::Riot riot_;
    tms5220::Chip  tms_;
    pokey::Pokey   pokey_[4];
    vector_display::Sized<kMaxSegments, kMaxActive> display_;

    uint8_t  ram_[0x3000] = {};     // 0000-2FFF (vector RAM and work RAM)
    uint8_t  ram2_[0x800] = {};     // 4800-4FFF
    uint8_t  math_ram_[0x1000] = {};// 5000-5FFF, shared with the matrix processor
    uint8_t  snd_ram_[0x800] = {};  // sound board 2000-27FF
    uint8_t  nv_ram_[256] = {};     // X2212 static RAM (4 bits a cell)
    uint8_t  nv_store_[256] = {};   // ... its EEPROM half (not kept over power-off)

    uint8_t  dsw0_ = kDipDefault;
    uint16_t buttons_ = 0;
    bool     swap_ = false;
    bool     idle_skip_ = true;
    bool     frame_wait_ok_ = false, flag_wait_ok_ = false, math_wait_ok_ = false, sound_wait_ok_ = false;
    int      pen_key_ = -1;         // the last vector's colour / intensity, and its pixel
    uint16_t pen_ = 0;
    int      bank_ = 0;
    uint8_t  latch_bits_ = 0;       // the LS259 outputs
    int      adc_channel_ = 0;
    int      stick_x_ = 0, stick_y_ = 0;
    int      sight_x_ = 0, sight_y_ = 0;    // where the sight buttons moved the yoke, ADC counts
    uint32_t lfsr_ = 0;
    int64_t  lfsr_at_ = 0;          // main CPU cycle the LFSR state is for

    // The latches: main -> sound (sound command), sound -> main.
    uint8_t  sound_latch_ = 0, main_latch_ = 0;
    bool     sound_pending_ = false, main_pending_ = false;
    uint32_t sound_commands_ = 0;

    int64_t  total_ = 0;            // cycles before this frame (both CPUs)
    int      fc_ = 0;               // main CPU cycles into this frame
    bool     in_main_ = false, in_snd_ = false;     // inside a CPU's run()
    int      snd_done_ = 0;         // sound CPU cycles into this frame
    int      main_run_ = 0, snd_run_ = 0;
    int64_t  vg_busy_until_ = 0;
    int64_t  math_busy_until_ = 0;
    uint32_t vg_runs_ = 0, math_runs_ = 0;
    int64_t  watchdog_at_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;

    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    float    dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
    float    lp_b0_ = 0, lp_b1_ = 0, lp_b2_ = 0, lp_a1_ = 0, lp_a2_ = 0, lp_z1_ = 0, lp_z2_ = 0;
};

} // namespace starwars
