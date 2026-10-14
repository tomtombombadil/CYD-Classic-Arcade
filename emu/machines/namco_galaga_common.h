// The Namco CPU board shared by Galaga and Dig Dug (and Bosconian / Xevious),
// written for this project (MIT): three Z80s at 3.072 MHz with their own
// program ROMs at 0000-3FFF and everything else in common - 2 KB video RAM,
// three 1 KB RAMs (sprite registers at their top), the 3-voice Namco WSG,
// a 74LS259 latch for interrupts and resets, the 06XX bus to the custom
// I/O chips (51XX controls and credits on every game) and a watchdog. The
// video boards differ; each game's machine adds its own (galaga.*,
// digdug.*) through the hooks below. Hardware facts from public
// documentation and MAME's drivers (read for facts only).
//
// Timing: 18.432 MHz crystal, 6.144 MHz pixel clock, 384 x 264 per frame
// = 60.606 Hz, CPUs at 3.072 MHz = 192 cycles a line, 50688 a frame.
// The main and sub CPUs get an IRQ at VBLANK (line 224) when enabled; the
// sound CPU an NMI at lines 64 and 192 (twice a frame) when enabled; the
// 06XX pulses the main CPU's NMI during transfers.
//
// The CPUs run interleaved in short slices (the main CPU leads, the others
// catch up to it, in the order main, sub, sound); interrupts and the 06XX's
// NMIs fall on slice edges.
//
// Idle skipping (see Speedup): the CPUs spend much of a frame in wait
// loops. A machine names those loops; at the start of each slice, a CPU
// found inside one that would keep spinning skips the whole slice. That
// changes nothing the CPU would have seen: within a slice no other CPU
// writes RAM before this one has run its part (the others either ran
// already or run after it), and interrupts only come at slice edges. A
// loop's check looks only at points of the loop where every decision still
// lies ahead (no register holds an old reading); elsewhere in the loop the
// CPU runs single instructions until it reaches one. A loop that polls no
// RAM (a jump to itself) can only be left by an interrupt, so the CPU
// sleeps until its next one. R advances as the loop would advance it
// (Galaga's main CPU reads R for random numbers).
#pragma once

#include "../cpu/z80.h"
#include "../sound/namco_wsg.h"
#include "namco_io.h"

#include <cstdint>

namespace namco_galaga {

constexpr int      kScreenW = 224;                      // portrait, as the cabinet shows it
constexpr int      kScreenH = 288;
constexpr uint32_t kCpuClock = 3072000;                 // 18.432 MHz / 6
constexpr int      kCyclesPerLine = 192;
constexpr int      kLinesPerFrame = 264;
constexpr int      kVblankLine = 224;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
constexpr uint32_t kWsgClock = kCpuClock / 32;          // 96 kHz

enum Cpu { kMain, kSub, kSound, kCpuCount };

// Cabinet buttons for set_inputs() (pressed = bit set), player 1's panel.
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kFire    = 0x010,   // Galaga fire, Dig Dug pump
    kCoin    = 0x020,
    kStart1  = 0x040,
    kStart2  = 0x080,
    kCoin2   = 0x100,
    kService = 0x200,   // service credit
    kTest    = 0x400,   // service mode switch
};

// A wait loop to skip: CPU `cpu` with its program counter in [pc_lo,
// pc_hi] (the whole loop). `check` says what the loop will do from here,
// from the program counter and RAM (Board::peek, Board::steady); null for
// a loop that polls nothing, which only an interrupt ends. `cycles_per_r`:
// the loop's T-states per opcode fetch (R counts fetches).
class Board;
enum class Idle : uint8_t {
    kNo,        // the loop is about to end (or do work): run
    kYes,       // it would spin through the whole slice: skip it
    kStep,      // not at a check point: run one instruction and ask again
};
struct Speedup {
    uint8_t  cpu;
    uint16_t pc_lo, pc_hi;
    Idle   (*check)(Board& b, uint16_t pc);
    uint8_t  cycles_per_r;
};

// What one frame cost, per CPU: cycles the interpreter ran and cycles
// skipped in wait loops.
struct CpuStats {
    uint32_t run, skipped;
};

class Board {
public:
    virtual ~Board() = default;

    void set_inputs(uint16_t buttons);
    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int  run_frame(int16_t* audio, int cap);

    z80::Cpu& cpu(int n) { return cpu_[n]; }
    uint32_t  frame() const { return frame_; }
    int       watchdog_resets() const { return watchdog_resets_; }
    const CpuStats& stats(int n) const { return stats_[n]; }   // the last frame's
    // Speedups on / off (on by default; off for measurements and tests).
    void set_speedups(bool on) { speedups_on_ = on; }

    const namco_io::Chip51& chip51() const { return chip51_; }
    const namco_wsg::Wsg&   wsg() const { return wsg_; }
    uint8_t latch() const { return latch_; }
    // RAM as the CPUs see it at 8000-87FF / 8800-8BFF / 9000-93FF / 9800-9BFF.
    uint8_t peek(uint16_t addr) const
    {
        return addr < 0x8800 ? vram_[addr & 0x7ff] : ram_[((addr >> 11) & 3) - 1][addr & 0x3ff];
    }

    // For loops that do work when there is some (a speedup's check): true
    // once nothing in the loop's inputs (RAM ranges [lo, hi]) has changed
    // while CPU n ran at least `cycles` inside the loop - enough for whole
    // passes of it, which then found nothing to do and will find nothing
    // again until an input changes. Found outside the loop's speedup
    // ranges at a slice start, or an IRQ to the CPU (its handler may have
    // changed anything), starts the count over; NMIs don't, so this is
    // only for loops whose inputs the NMI handlers leave alone. Up to 64
    // bytes of inputs.
    struct Range {
        uint16_t lo, hi;
    };
    bool steady(int n, const Range* ranges, int count, int cycles);

protected:
    struct BoardRoms {
        const uint8_t* cpu[kCpuCount];
        uint32_t       cpu_size[kCpuCount];     // 4 or 8 KB on the sub CPUs
        const uint8_t* wave;                    // 256-byte WSG waveform PROM
    };
    void board_init(const BoardRoms& roms, uint32_t audio_rate, const Speedup* speedups, int speedup_count);
    void board_reset();

    // --- hooks for the game's video board and extra chips -------------------
    // Reads / writes outside the common map (DIP switches, video latch, ...).
    virtual uint8_t io_read(int cpu, uint16_t addr) { (void)cpu; (void)addr; return 0xff; }
    virtual void    io_write(int cpu, uint16_t addr, uint8_t v) { (void)cpu; (void)addr; (void)v; }
    // 06XX chips 1-3 (chip 0 is the 51XX).
    virtual uint8_t chip_read(int chip) { (void)chip; return 0xff; }
    virtual void    chip_write(int chip, uint8_t v) { (void)chip; (void)v; }
    // Latch bit 3 low holds the sub CPUs and the custom chips in reset.
    virtual void    chips_reset() {}
    // Latch bits 4-7 (Dig Dug: the 53XX mode).
    virtual void    latch_changed(uint8_t latch) { (void)latch; }
    // VBLANK starts: take the video snapshot.
    virtual void    vblank() = 0;
    // The frame ends (the video latch's inputs to the next frame).
    virtual void    frame_end() {}
    // Mixes the game's own sound into the WSG's output (n samples).
    virtual void    mix(int16_t* out, int n) { (void)out; (void)n; }

    // The CPU's time in this frame (cycles from the frame's start), for
    // callbacks.
    int now(int cpu) const { return pos_[cpu] + cpu_[cpu].elapsed(); }
    void audio_to(int cycle);           // renders audio up to this cycle of the frame

    uint8_t vram_[0x800] = {};          // 8000-87FF
    uint8_t ram_[3][0x400] = {};        // 8800-8BFF, 9000-93FF, 9800-9BFF
    namco_io::Bus06  bus06_;
    namco_io::Chip51 chip51_;
    namco_wsg::Wsg   wsg_;
    uint8_t latch_ = 0;                 // 6820-6827

private:
    struct Port {
        Board* board;
        int    n;
    };
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);

    uint8_t read(int n, uint16_t addr);
    void    write(int n, uint16_t addr, uint8_t v);
    void    write_latch(int bit, bool v, int n);
    void    run_cpus(int end);
    void    do_vblank();
    void    nmi(int n);
    const Speedup* idle(int n, int end);
    bool    subs_held() const { return !(latch_ & 0x08); }

    BoardRoms roms_{};
    z80::Cpu cpu_[kCpuCount];
    Port     port_[kCpuCount] = {};
    int      pos_[kCpuCount] = {};      // each CPU's time in the frame
    uint64_t frame_base_ = 0;           // CPU cycles before this frame (06XX time base)
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    uint16_t buttons_ = 0;

    static constexpr int kMaxSpeedups = 16;     // per CPU
    const Speedup* speedups_[kCpuCount][kMaxSpeedups] = {};
    int      speedup_count_[kCpuCount] = {};
    uint16_t speedup_lo_[kCpuCount] = {}, speedup_hi_[kCpuCount] = {};  // all of a CPU's loops
    bool     speedups_on_ = true;
    const Speedup* sleep_[kCpuCount] = {};  // in a loop only an interrupt ends
    bool     nmi_sent_[kCpuCount] = {};     // an NMI the CPU hasn't run since
    uint64_t executed_[kCpuCount] = {};     // cycles run, ever
    uint32_t irqs_[kCpuCount] = {};         // IRQs raised, ever
    struct Watch {                          // steady(): the inputs last seen
        uint8_t  bytes[64];
        uint64_t since;                     // executed_ when they last changed
        uint32_t irqs;
        bool     valid;
    } watch_[kCpuCount] = {};
    CpuStats stats_[kCpuCount] = {};

    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
};

// --- graphics helpers shared by the video boards -------------------------------

// RGB565 of a palette PROM byte (red and green 1K / 470 / 220 ohm, blue
// 470 / 220 ohm), byte-swapped for SPI LCDs if asked.
uint16_t prom_rgb565(uint8_t v, bool swap);
uint16_t rgb565(int r, int g, int b, bool swap);

// 8x8 2-bit characters (the Pac-Man / Galaga layout) decoded into display
// orientation (the monitor is turned 90 degrees clockwise): rows[code * 8
// + y] holds display row y, pixel x in bits 2x+1..2x.
void decode_chars_2bpp(const uint8_t* rom, int count, uint16_t* rows);

// 16x16 2-bit sprites (the Galaga layout), decoded like the characters:
// rows[code * 16 + r], r = the hardware's x (= display line inside the
// sprite), pixel i = hardware y 15 - i (= display x inside the sprite).
void decode_sprites(const uint8_t* rom, int count, uint32_t* rows);

// Video RAM offset of display tile row `row` (0-35) at column 0, and the
// step to the next column: the 36 x 28 Namco layout (rows 0-1 and 34-35
// are the score lines, stored apart).
inline void tile_row(int row, int& off, int& step)
{
    if (row < 2)        { off = 0x3dd + row * 0x20; step = -1; }
    else if (row >= 34) { off = 0x01d + (row - 34) * 0x20; step = -1; }
    else                { off = 0x3a0 + (row - 2); step = -0x20; }
}

// A sprite as the video board shows it, made at the VBLANK snapshot.
struct Sprite {
    int16_t x, y;           // hardware position of the top-left tile (x = display line)
    uint8_t code;           // first tile; a double-size sprite uses code + 0..3
    uint8_t colour;
    uint8_t flags;          // kFlipX, kFlipY, kWide, kTall
};
enum SpriteFlag : uint8_t { kFlipX = 1, kFlipY = 2, kWide = 4, kTall = 8 };

// Draws display line `line` of sprite `s` into `out` (224 pixels, clipped):
// tile pixels from `rows` (decode_sprites; tile codes wrap at `codes`, a
// power of two), pens[pixel value], drawn where bit p of `opaque` is set.
void draw_sprite_line(const Sprite& s, int line, const uint32_t* rows, int codes, const uint16_t* pens,
                      uint8_t opaque, uint16_t* out);

} // namespace namco_galaga
