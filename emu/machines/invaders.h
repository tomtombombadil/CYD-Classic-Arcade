// Space Invaders (Midway, PCB 739) arcade machine, written for this project
// (MIT). 8080 at 1.9968 MHz (19.968 MHz / 10), 59.54 Hz frames (262 lines
// x 128 CPU cycles), RST 1 at line 96 and RST 2 at line 224 (VBLANK start),
// a 1-bit 256 x 224 bitmap, the MB14241 barrel shifter on ports 2 / 3 / 4,
// and analog sound circuits switched by ports 3 and 5 (emu/sound/
// invaders_sound.*, or the optional MAME sample set). Hardware facts from
// public documentation and MAME's driver (read for facts only).
//
// The monitor is turned 90 degrees counter-clockwise in the cabinet, so the
// game is shown 224 wide x 256 tall. render_line() makes one display line
// straight from video RAM - no frame buffer - and colours it like the
// cabinet's cellophane overlay strips on the black-and-white monitor.
#pragma once

#include "../cpu/i8080.h"
#include "../rom/romset.h"
#include "../sound/invaders_sound.h"
#include "../sound/samples.h"

#include <cstdint>

namespace invaders {

constexpr int      kScreenW = 224;
constexpr int      kScreenH = 256;
constexpr uint32_t kCpuClock = 1996800;                 // 19.968 MHz / 10
constexpr int      kCyclesPerLine = 128;                // 320 pixels at 4.992 MHz
constexpr int      kLinesPerFrame = 262;
constexpr int      kMidLine = 96;                       // RST 1
constexpr int      kVblankLine = 224;                   // RST 2
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 33536 -> 59.54 Hz

// Buttons for set_inputs() (pressed = bit set). The upright cabinet has
// one control panel that both players use in turn; the player-2 buttons
// are an extra set (a second controller) that also drives player 2.
enum Button : uint16_t {
    kCoin    = 0x001,
    kStart1  = 0x002,
    kStart2  = 0x004,
    kFire    = 0x008,
    kLeft    = 0x010,
    kRight   = 0x020,
    kFire2   = 0x040,
    kLeft2   = 0x080,
    kRight2  = 0x100,
};

// DIP switches (read on port 2).
enum Dip : uint8_t {
    kDipShipsMask   = 0x03, kDipShips3 = 0x00, kDipShips4 = 0x01, kDipShips5 = 0x02, kDipShips6 = 0x03,
    kDipBonus1000   = 0x08,     // extra ship at 1000 (clear = 1500)
    kDipNoCoinInfo  = 0x80,     // hide the coin info on the attract screen
};
// 3 ships, extra ship at 1500, coin info shown.
constexpr uint8_t kDipDefault = kDipShips3;

// The ROM set (MAME "invaders"): one region.
enum Region { kRegionCpu, kRegionCount };
extern const romset::Set kRomSet;

// MAME's sample set "invaders": file n.wav plays invaders_sound::Effect n
// ("0" UFO ... "9" extra ship). Newer sets have no 0.wav; the UFO is then
// synthesised like every sound whose file is missing.
constexpr int kSampleCount = invaders_sound::kEffectCount;
extern const char* const kSampleNames[kSampleCount];

struct Roms {
    const uint8_t* cpu;     // 8 KB program (kept by pointer, may be in flash)
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dips) { dips_ = dips; }

    // Optional recordings, indexed by invaders_sound::Effect (null = use
    // the model). The WAVs are kept by pointer.
    void set_samples(const samples::Wav* const* wavs);

    // Runs one frame (from line 96 to line 96, so the picture is read
    // between the two halves the game redraws). Writes the frame's audio
    // (about audio_rate / 59.54 samples) to `audio` (up to `cap`; null =
    // none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-255) into 224 RGB565 pixels.
    void render_line(int y, uint16_t* out) const;

    i8080::Cpu& cpu() { return cpu_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    uint8_t  sound_port(int n) const { return n == 3 ? port3_ : port5_; }

    // The barrel shifter (MB14241), public for tests.
    struct Shifter {
        uint16_t reg = 0;
        uint8_t  count = 0;
        void data(uint8_t v) { reg = uint16_t((reg >> 8) | (v << 8)); }
        uint8_t result() const { return uint8_t(reg >> (8 - count)); }
    };

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);
    static void    irq_ack(void* ctx);

    void sound_write(int port, uint8_t v);
    void audio_to(int cycle);           // renders audio up to this cycle of the frame
    uint8_t in0() const;
    uint8_t in1() const;
    uint8_t in2() const;

    Roms       roms_{};
    i8080::Cpu cpu_;
    Shifter    shifter_;
    invaders_sound::Synth synth_;
    samples::Player player_;
    const samples::Wav* wav_[kSampleCount] = {};

    uint8_t  ram_[0x2000] = {};     // 2000-3FFF: work RAM, video RAM from 2400
    uint8_t  dips_ = kDipDefault;
    uint16_t buttons_ = 0;
    uint8_t  port3_ = 0, port5_ = 0;
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    int      carry_ = 0;            // cycles run past the last slice
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;

    // Audio of the frame being run (written as the sound ports change).
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int      slice_start_ = 0;      // frame cycle at which the current cpu_.run() began

    uint16_t black_ = 0;
    uint16_t pen_[3] = {};          // white, red, green (the overlay strips)
};

} // namespace invaders
