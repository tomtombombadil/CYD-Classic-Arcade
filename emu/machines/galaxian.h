// Galaxian (Namco, 1979) arcade machine, written for this project (MIT).
// Z80 at 3.072 MHz, 60.606 Hz frames (264 lines x 192 CPU cycles), NMI at
// VBLANK (line 240) while enabled, the Galaxian video (galaxian_video.*:
// tiles with column scroll, sprites, bullets, stars) and the sound board's
// circuits as a model (emu/sound/galaxian_sound). Hardware facts from
// public documentation and MAME's driver (read for facts only).
//
// Portrait 224 x 256 (the monitor turned 90 degrees clockwise).
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/galaxian_sound.h"
#include "galaxian_video.h"

#include <cstdint>

namespace galaxian {

constexpr int      kScreenW = galaxian_video::kScreenW;
constexpr int      kScreenH = galaxian_video::kScreenH;
constexpr uint32_t kCpuClock = 3072000;                 // 18.432 MHz / 6
constexpr int      kCyclesPerLine = 192;                // 384 pixel clocks at 6.144 MHz
constexpr int      kLinesPerFrame = 264;
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
// The watchdog (read 7800) resets the board after 8 frames without a read.
constexpr int      kWatchdogFrames = 8;

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kLeft    = 0x001,
    kRight   = 0x002,
    kFire    = 0x004,
    kCoin    = 0x008,
    kStart1  = 0x010,
    kStart2  = 0x020,
    kCoin2   = 0x040,
    kService = 0x080,   // service credit
    kTest    = 0x100,   // service mode switch
};

// DIP switches (read at 7000, active high; coinage, in 6800, is fixed at
// 1 coin 1 credit).
enum Dip : uint8_t {
    kDipBonusMask = 0x03, kDipBonus7000 = 0x00, kDipBonus10000 = 0x01, kDipBonus12000 = 0x02,
                          kDipBonus20000 = 0x03,
    kDipLives3    = 0x04,   // clear = 2
};
constexpr uint8_t kDipDefault = kDipBonus7000 | kDipLives3;

// The ROM set (MAME "galaxian", Namco set 1); regions in this order.
enum Region { kRegionCpu, kRegionGfx, kRegionProms, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 10 KB program
    const uint8_t* gfx;     // 2 x 2 KB bit planes: 256 tiles / 64 sprites
    const uint8_t* proms;   // 32 B palette
};

using VideoState = galaxian_video::VideoState;

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw) { dsw_ = dsw; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    void render_line(const VideoState& vs, int y, uint16_t* out) const { video_.render_line(vs, y, out); }
    void render_line(int y, uint16_t* out) const { video_.render_line(state_, y, out); }
    const VideoState& video() const { return state_; }

    z80::Cpu& cpu() { return cpu_; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return watchdog_resets_; }
    uint32_t  star_origin() const { return star_origin_; }
    bool      nmi_enabled() const { return nmi_enable_; }
    uint8_t   pitch() const { return pitch_; }
    uint8_t   sound_control() const { return sound_ctrl_; }
    uint8_t   lfo() const { return lfo_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);

    void vblank();
    void audio_to(int cycle);
    int  now() const { return slice_start_ + cpu_.elapsed(); }
    uint8_t in0() const;
    uint8_t in1() const;

    Roms     roms_{};
    z80::Cpu cpu_;
    galaxian_video::Video video_;
    galaxian_sound::Sound sound_;

    uint8_t  ram_[0x400] = {};      // 4000-43FF (mirrored at 4400)
    uint8_t  vram_[0x400] = {};     // 5000-53FF (mirrored at 5400)
    uint8_t  obj_[0x100] = {};      // 5800-58FF (mirrored to 5FFF)
    bool     nmi_enable_ = false;   // 7001
    bool     stars_on_ = false;     // 7004
    uint8_t  pitch_ = 0xff;         // 7800 (written)
    uint8_t  sound_ctrl_ = 0;       // 6800-6807
    uint8_t  lfo_ = 0;              // 6004-6007
    uint32_t star_origin_ = 0;
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

    VideoState state_{};
};

} // namespace galaxian
