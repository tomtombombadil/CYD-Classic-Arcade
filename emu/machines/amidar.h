// Amidar (Konami, 1981) arcade machine, written for this project (MIT).
// Konami's board built on Galaxian's video (galaxian_video.*: tiles with
// column scroll, sprites, a background colour from three latch bits; no
// stars or bullets): Z80 at 3.072 MHz, NMI at VBLANK (line 240) while
// enabled, the inputs and the sound command through two 8255 PPIs. The
// sound board runs its own program: Z80 at 1.79 MHz (14.318 MHz / 8), two
// AY-3-8910s at the same clock (emu/sound/ay8910), the command latch on
// the first one's port A and a counter chain on its port B (the music's
// tempo), and an RC low-pass per channel whose capacitors the program
// picks - so the sound is exact but for the filters' resistance. Hardware
// facts from public documentation and MAME's driver (read for facts only).
//
// Portrait 224 x 256 (the monitor turned 90 degrees clockwise).
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/ay8910.h"
#include "galaxian_video.h"

#include <cstdint>

namespace amidar {

constexpr int      kScreenW = galaxian_video::kScreenW;
constexpr int      kScreenH = galaxian_video::kScreenH;
constexpr uint32_t kCpuClock = 3072000;                 // 18.432 MHz / 6
constexpr int      kCyclesPerLine = 192;
constexpr int      kLinesPerFrame = 264;
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
constexpr uint32_t kSoundXtal = 14318181;               // the sound CPU and AYs run at / 8
constexpr int      kWatchdogFrames = 8;
// The sound board's counter chain (port B of the first AY): 16 x 16 x 2 x
// 8 x 5 x 2 crystal clocks a turn.
constexpr uint32_t kTimerPeriod = 16 * 16 * 2 * 8 * 5 * 2;

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kJump    = 0x010,
    kCoin    = 0x020,
    kStart1  = 0x040,
    kStart2  = 0x080,
    kCoin2   = 0x100,
    kService = 0x200,   // service credit
};

// DIP switches for set_dips(): lives (IN1 bits 0-1) and the bonus (IN2 bit
// 2); coinage (IN3) is fixed at 1 coin 1 credit, the cabinet upright, demo
// sounds through set_demo_sounds().
enum Dip : uint8_t {
    kDipLivesMask  = 0x03, kDipLives3 = 0x03, kDipLives4 = 0x02, kDipLives5 = 0x01, kDipLives255 = 0x00,
    kDipBonus50000 = 0x04,   // clear = 30000 and 50000, set = 50000 and every 50000
};
constexpr uint8_t kDipDefault = kDipLives3;

// The ROM set (MAME "amidar", Konami); regions in this order.
enum Region { kRegionCpu, kRegionSound, kRegionGfx, kRegionProms, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 16 KB program
    const uint8_t* sound;   // 8 KB sound program
    const uint8_t* gfx;     // 2 x 2 KB bit planes: 256 tiles / 64 sprites
    const uint8_t* proms;   // 32 B palette
};

using VideoState = galaxian_video::VideoState;

// The sound board's counter chain as port B reads it, from the crystal
// clocks it has counted: bit 7 the last divide-by-2, bits 6 / 5 the
// divide-by-5's top two bits, bit 4 the divide-by-8's top bit, bits 3-1
// high, bit 0 grounded.
uint8_t timer_bits(uint64_t xtal_clocks);

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dips) { dips_ = dips; }
    // IN2 bit 1, clear = on (the factory setting): the attract mode's tunes.
    void set_demo_sounds(bool on) { demo_sounds_ = on; }
    // The sound CPU's waits on the counter chain skipped (exact; on by
    // default - tests compare runs without).
    void set_idle_skip(bool on) { idle_skip_ = on; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    void render_line(const VideoState& vs, int y, uint16_t* out) const { video_.render_line(vs, y, out); }
    void render_line(int y, uint16_t* out) const { video_.render_line(state_, y, out); }
    const VideoState& video() const { return state_; }

    z80::Cpu& cpu() { return cpu_; }
    z80::Cpu& sound_cpu() { return snd_; }
    const ay8910::Chip& ay(int i) const { return ay_[i & 1]; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return watchdog_resets_; }
    uint8_t   sound_latch() const { return latch_; }
    uint32_t  sound_irqs() const { return sound_irqs_; }
    uint16_t  filters() const { return filter_; }
    bool      muted() const { return control_ & 0x10; }
    uint64_t  skipped_cycles() const { return skipped_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t snd_mem_read(void* ctx, uint16_t addr);
    static void    snd_mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t snd_port_read(void* ctx, uint16_t port);
    static void    snd_port_write(void* ctx, uint16_t port, uint8_t v);
    static uint8_t no_in(void*, uint16_t) { return 0xff; }
    static void    no_out(void*, uint16_t, uint8_t) {}

    void vblank();
    uint8_t in(int port) const;
    void sound_control(uint8_t v);
    int  now() const { return slice_start_ + cpu_.elapsed(); }
    int  sound_now() const { return snd_done_ + snd_.elapsed(); }
    // Runs the sound CPU to where the main CPU's frame cycle has reached.
    void sound_to(int main_cycle);
    void skip_timer_wait(int target);
    void audio_to(int snd_cycle);
    void set_filters(uint16_t bits);
    int16_t mix_sample(uint32_t clocks);

    Roms     roms_{};
    z80::Cpu cpu_, snd_;
    galaxian_video::Video video_;
    ay8910::Chip ay_[2];

    uint8_t  ram_[0x800] = {};      // 8000-87FF
    uint8_t  vram_[0x400] = {};     // 9000-93FF (mirrored at 9400)
    uint8_t  obj_[0x100] = {};      // 9800-98FF (mirrored to 9FFF)
    uint8_t  snd_ram_[0x400] = {};  // sound board 8000-83FF
    bool     nmi_enable_ = false;
    uint8_t  background_ = 0;       // bit 0 red, 1 green, 2 blue
    uint8_t  latch_ = 0;            // the sound command (PPI 2 port A)
    uint8_t  control_ = 0;          // PPI 2 port B: bit 3 falling = sound IRQ, bit 4 = mute
    bool     snd_irq_ = false;      // the IRQ flip-flop, cleared when the CPU takes it
    uint32_t sound_irqs_ = 0;
    uint8_t  dips_ = kDipDefault;
    bool     demo_sounds_ = true;
    uint16_t buttons_ = 0;
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    int      carry_ = 0;            // main cycles run past the last slice
    int      slice_start_ = 0;
    uint32_t frame_ = 0;

    // The sound board's time: its cycles this frame (29,531.3 on average,
    // the remainder carried), and the total at the frame's start.
    int      snd_frame_ = 0;
    uint64_t snd_rem_ = 0;
    int      snd_done_ = 0;
    uint64_t snd_base_ = 0;

    // The sound program waits for the counter chain's bit 7 to change in
    // two loops (amidar.cpp); a read from one stops the CPU so the passes
    // still to come before the bit changes can be skipped.
    bool     idle_skip_ = true;
    bool     wait_loops_ = false;   // the program has them where expected
    bool     wait_read_ = false;    // the CPU just read the chain in one
    uint8_t  wait_stay_ = 0;        // the bit 7 that keeps it waiting
    int      wait_at_ = 0;          // the read's cycle (this frame)
    uint64_t skipped_ = 0;

    // Audio: n samples over the frame's sound cycles.
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int      audio_clock_ = 0, sample_start_ = 0;
    uint32_t acc_[6] = {};
    uint16_t filter_ = 0;           // the filter bits (12: two per channel)
    float    lp_a_[6] = {};         // per channel: 0 = no capacitor, else the low-pass step
    float    lp_y_[6] = {};
    float    dc_r_ = 0, dc_x_ = 0, dc_y_ = 0;

    VideoState state_{};
};

} // namespace amidar
