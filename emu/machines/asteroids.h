// Asteroids (Atari, 1979) arcade machine, written for this project (MIT).
// 6502 at 1.512 MHz (12.096 MHz / 8), NMI from the 3 kHz clock / 12 (246
// Hz, off in self-test), the Digital Vector Generator (atari_dvg) drawing
// from vector RAM / ROM, and the analog sound board (our own models,
// emu/sound/asteroids_sound). Hardware facts from public documentation
// and MAME's driver (read for facts only).
//
// The picture: the DVG's square is 1024 x 1024 with Y up; the game draws
// in X 0-1023, Y 96-927 (the score at the top, the copyright line at the
// bottom). Shown 320 x 240 (landscape cabinet, no rotation): Y 88-935 fills
// the height and X keeps the same scale (rocks stay round), centred with
// thin black side bars - drawn one output line at a time from the last
// finished vector list (vector_display).
#pragma once

#include "../cpu/m6502.h"
#include "../rom/romset.h"
#include "../sound/asteroids_sound.h"
#include "atari_dvg.h"
#include "vector_display.h"

#include <cstdint>

namespace asteroids {

constexpr int      kScreenW = 320;
constexpr int      kScreenH = 240;
constexpr uint32_t kCpuClock = 1512000;
constexpr int      kNmiCycles = 6144;                   // 4096 x 12 master clocks / 8
constexpr int      kCyclesPerFrame = 4 * kNmiCycles;    // 24576 -> 61.5 Hz (the game's own frame)
constexpr int      kWatchdogCycles = 131072;            // 3 kHz / 256
// The window shown: DVG Y kTopY down to kTopY - kViewH, X 0-1023 centred.
constexpr int      kTopY = 935;
constexpr int      kViewH = 848;

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kLeft      = 0x001,
    kRight     = 0x002,
    kThrust    = 0x004,
    kFire      = 0x008,
    kHyperspace = 0x010,
    kCoin      = 0x020,     // left coin
    kStart1    = 0x040,
    kStart2    = 0x080,
    kCoin2     = 0x100,     // centre coin
    kTest      = 0x200,     // self-test switch
    kDiagStep  = 0x400,
    kSlam      = 0x800,
};

// The DIP switches (SW1-8). Language 0x03, ships 0x04 (set = 3), centre
// coin 0x08, right coin 0x30, coinage 0xc0.
enum Dip : uint8_t {
    kDipEnglish = 0x00, kDipGerman = 0x01, kDipFrench = 0x02, kDipSpanish = 0x03,
    kDip3Ships = 0x04,
    kDipFreePlay = 0x00, kDip1Coin2Credits = 0x40, kDip1Coin1Credit = 0x80, kDip2Coins1Credit = 0xc0,
};
constexpr uint8_t kDipDefault = kDipEnglish | kDip3Ships | kDip1Coin1Credit;

// The ROM set: MAME "asteroid" (rev 4) or "asteroid2" (rev 2, older
// MAME's "asteroid") - romset variants 1 and 2. The DVG's PROM isn't
// needed.
enum Region { kRegionCpu, kRegionVector, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 6 KB program at 6800-7FFF
    const uint8_t* vector;  // 2 KB vector ROM at 5000-57FF
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

    // Runs one frame (4 NMIs). Writes its audio (about audio_rate / 61.5
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Display line y (0-239) of the last vector list: 320 RGB565 pixels.
    void render_line(int y, uint16_t* out) { display_.render_line(y, out); }

    m6502::Cpu& cpu() { return cpu_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    const vector_display::Display& display() const { return display_; }
    const asteroids_sound::Board& sound() const { return sound_; }
    uint32_t vg_runs() const { return vg_runs_; }
    int      vg_clocks() const { return vg_clocks_; }   // the last list's drawing time
    uint8_t  thump_value() const { return thump_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t vec_read(void* ctx, uint16_t addr);
    static void    vec_line(void* ctx, int x0, int y0, int x1, int y1, int intensity);

    uint32_t now() const { return total_ + uint32_t(fc_ + cpu_.elapsed()); }
    void map_ram_banks();
    void vg_go();
    void check_idle(int bit);
    void audio_to(int cycle);

    Roms       roms_{};
    m6502::Cpu cpu_;
    atari_dvg::Dvg dvg_;
    asteroids_sound::Board sound_;
    vector_display::Display display_;

    uint8_t  ram_[0x400] = {};      // 0000-03FF; 0200 / 0300 swap with RAMSEL (player 2)
    uint8_t  vram_[0x800] = {};     // 4000-47FF vector RAM
    uint8_t  dsw_ = kDipDefault;
    uint16_t buttons_ = 0;
    uint8_t  outlatch_ = 0;         // 3200: lamps, RAMSEL (bit 2), coin counters
    uint8_t  thump_ = 0;
    bool     swap_ = false;

    uint32_t total_ = 0;            // CPU cycles before this frame
    int      fc_ = 0;               // cycles into this frame
    uint32_t vg_busy_until_ = 0;
    uint32_t vg_runs_ = 0;
    int      vg_clocks_ = 0;
    bool     idle_ = false;         // the program is in a wait loop (see check_idle)
    uint32_t idle_until_ = 0;
    uint32_t watchdog_at_ = 0;      // cycle of the last watchdog write
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    uint16_t gray_[16] = {};        // pixel per intensity

    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
};

} // namespace asteroids
