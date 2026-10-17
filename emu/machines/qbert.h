// Q*bert (Gottlieb, 1982) arcade machine, written for this project (MIT).
// The Gottlieb "GG" board: an 8088 at 5 MHz (15 MHz / 3), 256 x 240
// pixels at 5 MHz with 318 clocks a line and 256 lines (61.42 Hz), NMI at
// VBLANK (line 240), a 32 x 32 tile background from ROM (Q*bert's board
// has the tile ROMs jumpered in place of the character RAM), 62-ish
// sprites of 16 x 16 in 15 colours, 16 colours of palette RAM. The rev. 1
// sound board: a 6502 at 3.579545 MHz / 4 with a 6532 RIOT and an 8-bit
// DAC, running its program from the ROM set (emulated, so the effects
// are exact), and a Votrax SC-01 speech chip, whose own ROM isn't in the
// set: our own small model of its sounds stands in (emu/sound/qbert_sound.*,
// Q*bert's "swearing" is random phonemes anyway). The cabinet's knocker
// (a solenoid hitting the cabinet when Q*bert falls off) is our own short
// thunk model. Hardware facts from public documents and MAME's driver
// (read for facts only).
//
// The monitor is turned 90 degrees counter-clockwise (MAME's ROT270), so
// the 256 x 240 picture is shown 240 wide x 256 tall: display line y is
// hardware column 255 - y, display x is hardware line x. render_line()
// makes one display line from a snapshot taken as VBLANK starts - no frame
// buffer.
#pragma once

#include "../cpu/i8088.h"
#include "../cpu/m6502.h"
#include "../rom/romset.h"
#include "../sound/qbert_sound.h"

#include <cstdint>

namespace qbert {

constexpr int      kScreenW = 240;
constexpr int      kScreenH = 256;
constexpr uint32_t kCpuClock = 5000000;                // 15 MHz / 3
constexpr int      kCyclesPerLine = 318;               // the pixel clock is 5 MHz too
constexpr int      kLinesPerFrame = 256;
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 81408 -> 61.42 Hz
// The 6502 runs at 3.579545 MHz / 4 = 894,886 Hz: 14,570 cycles a frame.
constexpr uint32_t kSoundClock = 894886;
constexpr int      kSoundCyclesPerFrame = 14570;

// Buttons for set_inputs() (pressed = bit set). The joystick is mounted
// turned 45 degrees: its four switches move Q*bert diagonally.
enum Button : uint16_t {
    kUpRight   = 0x001,     // IN4 bit 2 ("up")
    kDownRight = 0x002,     // IN4 bit 0 ("right")
    kDownLeft  = 0x004,     // IN4 bit 3 ("down")
    kUpLeft    = 0x008,     // IN4 bit 1 ("left")
    kCoin      = 0x010,
    kCoin2     = 0x020,
    kStart1    = 0x040,
    kStart2    = 0x080,
    kTest      = 0x100,     // the service (test mode) switch
    kSelect    = 0x200,     // "select" in the test mode
};

// DIP switches (DSW, read at 5800).
enum Dip : uint8_t {
    kDipDemoSoundsOff = 0x01,
    kDipKicker        = 0x02,       // the knocker on
    kDipCocktail      = 0x04,
    kDipDemoMode      = 0x08,       // unlimited lives, Start advances (a test aid)
    kDipFreePlay      = 0x10,
};
// Demo sounds on, knocker on, upright, no demo mode, coins.
constexpr uint8_t kDipDefault = kDipKicker;

// The ROM set: MAME "qbert" (US set 1).
enum Region { kRegionMain, kRegionSound, kRegionTiles, kRegionSprites, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* main;    // 24 KB: A000-FFFF
    const uint8_t* sound;   // 4 KB: 6502 7000-7FFF
    const uint8_t* tiles;   // 8 KB: 256 tiles of 8 x 8, 4 bits a pixel, packed
    const uint8_t* sprites; // 32 KB: 4 bit planes of 8 KB, 256 sprites of 16 x 16
};

// What the video hardware shows: copied as VBLANK starts.
struct VideoState {
    uint8_t  vram[0x400];
    uint16_t pens[16];          // RGB565 per colour
    struct Sprite {
        int16_t x, y;           // hardware position of the top-left pixel
        uint8_t code;
    };
    Sprite  sprites[64];
    uint8_t sprite_count;
    bool    bg_on_top;          // 5803 bit 0: the background covers sprites
    bool    flip_x, flip_y;
};

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    // Idle-loop skipping (on by default; off for tests that compare).
    void set_idle_skip(bool on) { idle_skip_ = on; }
    void set_dips(uint8_t dsw) { dsw_ = dsw; }
    // The attract mode's sounds (the Demo Sounds switch).
    void set_demo_sounds(bool on)
    {
        dsw_ = uint8_t(on ? dsw_ & ~kDipDemoSoundsOff : dsw_ | kDipDemoSoundsOff);
    }

    // Runs one frame (line 0 to line 0). Writes the frame's audio (about
    // audio_rate / 61.4 samples) to `audio` (up to `cap`; null = none) and
    // returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-255) of a snapshot into 240 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    i8088::Cpu& cpu() { return cpu_; }
    m6502::Cpu& sound_cpu() { return snd_; }
    uint32_t    frame() const { return frame_; }
    int         watchdog_resets() const { return watchdog_resets_; }
    // Diagnostics for tests and the PC tool.
    uint32_t    sound_commands() const { return sound_commands_; }
    uint32_t    dac_writes() const { return dac_writes_; }
    uint32_t    phonemes() const { return speech_.phonemes(); }
    uint32_t    knocks() const { return knocks_; }
    int         main_cycles_run() const { return main_run_; }    // last frame, after idle skipping
    int         sound_cycles_run() const { return snd_run_; }
    const uint8_t* ram() const { return ram_; }

private:
    static uint8_t mem_read(void* ctx, uint32_t addr);
    static void    mem_write(void* ctx, uint32_t addr, uint8_t v);
    static uint8_t snd_read(void* ctx, uint16_t addr);
    static void    snd_write(void* ctx, uint16_t addr, uint8_t v);

    void decode_pen(int i);
    void snapshot();
    int  now() const { return fc_ + cpu_.elapsed(); }     // main CPU cycle in this frame
    void run_main_to(int cycle);
    void sound_to(int main_cycle);      // runs the 6502 up to this point of the frame
    void sound_run_to(int snd_cycle);
    void audio_to(int snd_cycle);       // renders output samples up to this 6502 cycle
    void sound_command(uint8_t v);
    void riot_update_irq();
    uint8_t riot_timer() const;

    Roms       roms_{};
    i8088::Cpu cpu_;
    m6502::Cpu snd_;
    qbert_sound::Speech  speech_;
    qbert_sound::Knocker knocker_;

    uint8_t  ram_[0x3000] = {};         // 0000-2FFF (0000-0FFF battery backed on the board)
    uint8_t  sprite_ram_[0x100] = {};   // 3000-30FF (seen to 37FF), write only
    uint8_t  vram_[0x400] = {};         // 3800-3BFF (seen to 3FFF)
    uint8_t  palette_[0x20] = {};       // 5000-501F (seen to 57FF), write only
    uint16_t pens_[16] = {};
    uint8_t  dsw_ = kDipDefault;
    uint16_t buttons_ = 0;
    uint8_t  out_ = 0;                  // 5803: video control, coin counter, knocker
    bool     swap_ = false;
    bool     idle_skip_ = true;

    // The sound board. RIOT: 128 bytes of RAM (zero page and stack - the
    // 6502 sees them at 0000-007F and 0100-017F alike), port A = the sound
    // command, port B = test switches and the speech chip's request line,
    // the edge detector on PA7 and the timer.
    uint8_t  snd_ram_[0x100] = {};      // 0x00-0x7F used; mapped at page 0 and 1
    uint8_t  pa_in_ = 0xff;
    uint8_t  pa_out_ = 0, pa_ddr_ = 0, pb_out_ = 0, pb_ddr_ = 0;
    bool     pa7_ = true, edge_pos_ = false, edge_ie_ = false, edge_flag_ = false;
    bool     timer_ie_ = false, timer_flag_ = false;
    int      timer_shift_ = 10;
    int64_t  timer_end_ = 0;            // 6502 cycle (of all time) the count reaches 0
    int64_t  snd_time_ = 0;             // 6502 cycles of all time up to this frame
    bool     nmi_line_ = false;

    int      fc_ = 0;                   // main CPU cycles into the frame
    int      snd_done_ = 0;             // 6502 cycles into the frame
    int      main_run_ = 0, snd_run_ = 0;
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    uint32_t sound_commands_ = 0, dac_writes_ = 0, knocks_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;

    // Audio of the frame being run: sample k covers 6502 cycles
    // [k, k + 1) x 14570 / n; positions are in 6502 cycles x n.
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int64_t  audio_pos_ = 0;
    int64_t  dac_acc_ = 0;
    uint8_t  dac_ = 0;
    float    hp_in_ = 0, hp_out_ = 0;   // DC block (the board's output capacitor)

    VideoState video_{};
};

} // namespace qbert
