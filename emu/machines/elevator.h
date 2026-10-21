// Elevator Action (Taito, 1983) arcade machine, written for this project
// (MIT). Taito's "SJ" system: a Z80 at 4 MHz with a 68705P3 protection MCU
// (emu/cpu/m68705) that talks to it through two latches and takes its bus
// to read and write its memory; a sound board with a Z80 at 3 MHz, three
// AY-3-8910s and an 8-bit DAC (a fourth AY sits on the main board).
// Hardware facts from public documentation and MAME's taitosj driver (read
// for facts only).
//
// Video (256 x 224, lines 16-239 of 264, 59.19 Hz - landscape, the
// cabinet's monitor stands horizontal): three 32 x 32 playfields of 8 x 8
// characters, each with a horizontal scroll and a vertical scroll per
// column; 24 sprites of 16 x 16; the characters and sprites are 3-bit
// pictures in RAM (two banks of 256 characters / 64 sprites), which the
// program fills from the graphics ROMs it reads through a port. The
// palette is 64 entries of 9 bits in RAM; a PROM picks, pixel by pixel,
// which of the four planes shows from the planes that aren't blank there.
//
// The picture is drawn a line at a time from the video RAM as it is when
// VBLANK starts (a frame here runs from VBLANK to VBLANK), straight from
// the character RAM - no frame buffer, nothing decoded ahead.
#pragma once

#include "../cpu/m68705.h"
#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/ay8910.h"

#include <cstdint>

namespace elevator {

constexpr int      kScreenW = 256;
constexpr int      kScreenH = 224;
constexpr int      kFirstLine = 16;                     // first visible line
constexpr uint32_t kCpuClock = 4000000;                 // 8 MHz / 2
constexpr int      kCyclesPerLine = 256;                // 384 pixel clocks at 6 MHz
constexpr int      kLinesPerFrame = 264;
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 67584 -> 59.19 Hz
// Time is kept in 12 MHz ticks: a main CPU cycle is 3, a sound CPU cycle
// 4, an MCU machine cycle 16 (3 MHz / 4), an AY clock 8 (12 MHz / 8).
constexpr int      kTicksPerFrame = kCyclesPerFrame * 3;                // 202752
constexpr int      kMainTicks = 3, kSoundTicks = 4, kMcuTicks = 16, kAyTicks = 8;
// The sound CPU's IRQ: 12 MHz / (2 x 4 x 16 x 16 x 10 x 16) = 36.62 Hz.
constexpr int      kSoundIrqTicks = 2 * 4 * 16 * 16 * 10 * 16;
constexpr int      kWatchdogFrames = 128;

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kFire    = 0x010,   // button 1: shoot
    kJump    = 0x020,   // button 2: jump
    kCoin    = 0x040,
    kCoin2   = 0x080,
    kStart1  = 0x100,
    kStart2  = 0x200,
    kService = 0x400,   // service credit
    kTilt    = 0x800,
};

// DIP switch bank A for set_dips() (bank B, the coinage, is fixed at 1 coin
// 1 credit for both slots; bank C at its factory setting: easiest, coins /
// credits shown, year shown, normal hit detection, two coin slots).
enum Dip : uint8_t {
    kDipBonusMask = 0x03, kDipBonus10000 = 0x03, kDipBonus15000 = 0x02, kDipBonus20000 = 0x01,
                          kDipBonus25000 = 0x00,
    kDipNoFreePlay = 0x04,
    kDipLivesMask = 0x18, kDipLives3 = 0x18, kDipLives4 = 0x10, kDipLives5 = 0x08, kDipLives6 = 0x00,
    kDipUnused    = 0x20,
    kDipNoFlip    = 0x40,
    kDipCocktail  = 0x80,
};
constexpr uint8_t kDipDefault = kDipBonus10000 | kDipNoFreePlay | kDipLives3 | kDipUnused | kDipNoFlip;
constexpr uint8_t kDsw2Default = 0x00;                  // 1 coin 1 credit, both slots
constexpr uint8_t kDsw3Default = 0xff;

// The ROM set (MAME "elevator": 2764s, or "elevatora": 2732s - Tom's);
// regions in this order.
enum Region { kRegionCpu, kRegionSound, kRegionMcu, kRegionGfx, kRegionProm, kRegionCount };
extern const romset::Set kRomSet;

// Pointers to the loaded regions (RAM or flash; kept, not copied).
struct Roms {
    const uint8_t* cpu;     // 32 KB program (0000-7FFF)
    const uint8_t* sound;   // 8 KB sound program
    const uint8_t* mcu;     // 2 KB: the 68705's whole address space
    const uint8_t* gfx;     // 32 KB of graphics, read through D404
    const uint8_t* prom;    // 256 B: the plane priority PROM
};

// Everything the picture is made of, as the program wrote it (the machine
// keeps its live video RAM in one of these; a copy is a snapshot).
struct VideoState {
    uint8_t  chars[0x3000];     // 9000-BFFF: two banks of 0x1800 (planes at +0 / +0x800 / +0x1000)
    uint8_t  vram[3][0x400];    // C400-CFFF: the three playfields
    uint8_t  colscroll[0x60];   // D000-D05F: 32 column scrolls per playfield
    uint8_t  sprites[0x100];    // D100-D1FF: two banks of 32 x 4 bytes
    uint8_t  palette[0x80];     // D200-D27F
    uint16_t pens[64];          // the palette as RGB565 (kept up to date as it's written)
    uint8_t  scroll[6];         // D500-D505: H / V per playfield
    uint8_t  colorbank[2];      // D506-D507
    uint8_t  priority = 0;      // D300
    uint8_t  mode = 0;          // D600: flips, sprite bank, plane enables
};

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dips) { dips_ = dips; }
    // The idle loops skipped: the MCU's, the main CPU's frame wait, the
    // sound CPU's main loop (exact; on by default - tests compare runs
    // without).
    void set_idle_skip(bool on) { idle_skip_ = on; }

    // Runs one frame (VBLANK to VBLANK). Writes its audio (about
    // audio_rate / 59.19 samples) to `audio` (up to `cap`; null = none)
    // and returns the count.
    int run_frame(int16_t* audio, int cap);

    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    z80::Cpu&     cpu() { return cpu_; }
    z80::Cpu&     sound_cpu() { return snd_; }
    m68705::Cpu&  mcu() { return mcu_; }
    const ay8910::Chip& ay(int i) const { return ay_[i & 3]; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    uint64_t mcu_skipped() const { return mcu_skipped_; }
    uint64_t main_skipped() const { return main_skipped_; }  // cycles of the main CPU's frame wait skipped
    uint64_t sound_skipped() const { return snd_skipped_; }  // cycles of the sound CPU's main loop skipped
    uint32_t bus_grants() const { return bus_grants_; }
    uint32_t host_writes() const { return host_writes_; }
    uint8_t  sound_latch() const { return latch_; }
    bool     sound_on() const { return bank_ & 0x02; }
    uint8_t  dac() const { return dac_; }

private:
    // main CPU
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t no_in(void*, uint16_t) { return 0; }
    static void    no_out(void*, uint16_t, uint8_t) {}
    // sound CPU
    static uint8_t snd_read(void* ctx, uint16_t addr);
    static void    snd_write(void* ctx, uint16_t addr, uint8_t v);
    // MCU
    static uint8_t mcu_port_in(void* ctx, int port);
    static void    mcu_port_out(void* ctx, int port, uint8_t latch, uint8_t ddr);

    int  now() const { return slice_start_ + cpu_.elapsed() * kMainTicks; }
    int  snd_now() const { return snd_t_ + snd_.elapsed() * kSoundTicks; }
    uint8_t in(int reg);
    void palette_write(int offset, uint8_t v);

    // The MCU interface (taitosj's security board): latches both ways, the
    // address counter, BUSRQ.
    uint8_t bus_value() const;
    void    mcu_to(int tick);
    bool    mcu_idle(int* wake) const;
    int     mcu_skip(int budget);
    void    mcu_port_b(uint8_t v);

    void sound_to(int tick);
    void sound_nmi_update();
    void ay_write(int chip, bool data, uint8_t v);
    void audio_to(int tick);
    int16_t mix_sample(uint32_t clocks);

    Roms     roms_{};
    z80::Cpu cpu_, snd_;
    m68705::Cpu mcu_;
    ay8910::Chip ay_[4];            // 0 on the main board (D40E / D40F), 1-3 on the sound board

    uint8_t  ram_[0x800] = {};      // 8000-87FF
    uint8_t  ram_c000_[0x400] = {}; // C000-C3FF
    uint8_t  ram_d060_[0xa0] = {};  // D060-D0FF
    uint8_t  snd_ram_[0x400] = {};  // sound 4000-43FF
    uint16_t gfx_ptr_ = 0;          // D509 / D50A, counts up on each D404 read
    uint8_t  bank_ = 0;             // D50E: bit 1 sound on, bit 7 ROM bank
    uint8_t  dips_ = kDipDefault;
    uint16_t buttons_ = 0;
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    bool     irq_held_ = false;     // VBLANK IRQ waiting to be taken

    // Main CPU time this frame (ticks), the start of the slice running.
    int      main_t_ = 0, slice_start_ = 0;

    // MCU interface
    int      mcu_t_ = 0;            // MCU time (ticks, this frame)
    uint16_t mcu_addr_ = 0xffff;
    uint8_t  mcu_data_ = 0xff;      // MCU -> Z80 latch
    uint8_t  host_data_ = 0xff;     // Z80 -> MCU latch
    uint8_t  read_data_ = 0xff;     // what the MCU read from the Z80's bus
    bool     zaccept_ = true;       // the Z80 has read the MCU's byte
    bool     zready_ = false;       // the Z80 has written a byte for the MCU
    uint8_t  pa_ = 0xff, pb_ = 0xff;
    bool     busrq_ = false;        // the MCU holds the Z80's bus (BUSAK at once)
    int      busrq_at_ = 0;         // MCU tick of the request
    int      stall_ = 0;            // main CPU ticks still owed to bus requests
    uint32_t bus_grants_ = 0;
    uint32_t host_writes_ = 0;      // bytes the Z80 wrote to the MCU
    bool     idle_skip_ = true;
    bool     loop180_ = false;      // the MCU program has its idle loop at 0180
    bool     main_wait_ = false;    // the main program has its frame wait at 73F2
    bool     sound_loop_ = false;   // the sound program has its main loop at 0164
    uint64_t main_skipped_ = 0, snd_skipped_ = 0;
    bool     main_primed_ = false;  // the main CPU has just run a whole pass of its frame wait
    bool     snd_primed_ = false;   // the sound CPU, of its main loop
    uint64_t mcu_skipped_ = 0;

    // Sound board
    int      snd_t_ = 0;            // sound CPU time (ticks, this frame)
    int      snd_irq_at_ = 0;       // next periodic IRQ (ticks, this frame)
    bool     snd_irq_held_ = false;
    uint8_t  latch_ = 0xff;         // the command
    bool     latch_full_ = false;   // set by the main CPU's write, cleared by the read
    bool     sem2_ = false;         // D50C's bit
    bool     nmi_en_ = false;       // AY 3 port B bit 0, low = on
    bool     nmi_line_ = false;
    bool     snd_nmi_pending_ = false;
    uint8_t  in4_ = 0;              // AY 2 port A (read by the main CPU at D40D)
    uint8_t  dac_ = 0x80;           // AY 1 port A
    float    dac_vol_ = 0;          // AY 1 port B through the ladder (0-1)

    // Audio
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int      audio_clock_ = 0, sample_start_ = 0;   // AY clocks this frame
    uint32_t acc_[12] = {};
    float    dac_acc_ = 0;
    float    dc_r_ = 0, dc_x_ = 0, dc_y_ = 0;

    bool     swap_ = false;
    VideoState video_{};
};

} // namespace elevator
