// Williams 6809 boards, written for this project (MIT): Defender (the
// first board: no blitter, ROM banked at C000), Robotron: 2084
// (the second: the SC1 blitter, ROM banked over the video RAM at
// 0000-8FFF).
// Both: a 6809 at 1 MHz (12 MHz / 12), 260 lines of 64 cycles a frame
// (60.10 Hz), a 304 x 256 bitmap of 4-bit pixels in 38 KB of RAM laid out
// in columns (byte = two pixels side by side, 256 bytes from one column of
// pixel pairs to the next), 16 palette registers (BBGGGRRR), two 6821 PIAs
// (inputs; sound command; the interrupts: CB1 = line counter bit 5, CA1 =
// line >= 240), 4-bit CMOS RAM for settings and high scores, a watchdog,
// and the sound board (emu/sound/williams_sound.*). Hardware facts from
// MAME's driver and public documentation (read for facts only).
//
// The monitor is upright: 292 x 240 as the player sees it (MAME's visible
// area). render_line() reads the video RAM as the frame ended; a frame
// runs from line kFrameStartLine to the same line of the next frame (see
// williams.cpp for why that line). Flip screen (cocktail) isn't drawn.
// The CMOS RAM starts empty, as a new board's does: the games restore
// their factory settings at the first boot and go on.
#pragma once

#include "../cpu/m6809.h"
#include "../rom/romset.h"
#include "../sound/williams_sound.h"
#include "pia6821.h"
#include "williams_blitter.h"

#include <cstdint>

namespace williams {

constexpr int      kScreenW = 292;
constexpr int      kScreenH = 240;
constexpr uint32_t kCpuClock = 1000000;                 // 12 MHz / 3 / 4
constexpr int      kCyclesPerLine = 64;                 // 512 pixel clocks at 8 MHz
constexpr int      kLinesPerFrame = 260;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 16640 -> 60.096 Hz
constexpr int      kFirstLine = 7;                      // visible lines 7-246

enum class Board : uint8_t { Defender, Robotron };

// Robotron: 2084 (MAME "robotron", Release 5 / solid blue label).
namespace robotron {
enum Button : uint16_t {
    kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008,
    kFireUp = 0x0010, kFireDown = 0x0020, kFireLeft = 0x0040, kFireRight = 0x0080,
    kStart1 = 0x0100, kStart2 = 0x0200, kCoin = 0x0400, kCoin2 = 0x0800,
    kAdvance = 0x1000, kAutoUp = 0x2000, kHighScoreReset = 0x4000, kTilt = 0x8000,
};
// 0000-8FFF (banked over the video RAM), D000-FFFF, the sound ROM.
enum Region { kRegionBanked, kRegionFixed, kRegionSound, kRegionCount };
extern const romset::Set kRomSet;
} // namespace robotron

// Defender (MAME "defender" Red label or "defenderg" Green label).
namespace defender {
// kLeft / kRight are the stick: the machine turns them into Reverse and
// Thrust by the ship's facing (see williams.cpp). kReverse / kThrust are
// the cabinet's own buttons.
enum Button : uint16_t {
    kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008,
    kFire = 0x0010, kThrust = 0x0020, kSmartBomb = 0x0040, kHyperspace = 0x0080,
    kStart1 = 0x0100, kStart2 = 0x0200, kCoin = 0x0400, kReverse = 0x0800,
    kAdvance = 0x1000, kAutoUp = 0x2000, kHighScoreReset = 0x4000, kTilt = 0x8000,
};
// D000-FFFF, the four ROM banks of C000 (banks 1, 2, 3, 7), the sound ROM.
enum Region { kRegionFixed, kRegionBanked, kRegionSound, kRegionCount };
extern const romset::Set kRomSet;
} // namespace defender

class Machine {
public:
    // `regions` in the board's Region order.
    void init(Board board, const uint8_t* const* regions, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }

    // Runs one frame; writes its audio (signed 16-bit, up to `cap`) and
    // returns the sample count.
    int  run_frame(int16_t* audio, int cap);
    // Line y (0-239) of the picture: 292 RGB565 pixels.
    void render_line(int y, uint16_t* out) const;

    m6809::Cpu&      cpu() { return cpu_; }
    williams_sound::Board& sound() { return sound_; }
    const uint8_t*   ram() const { return ram_; }
    uint8_t*         ram() { return ram_; }
    const uint8_t*   cmos() const { return cmos_; }
    const WilliamsBlitter& blitter() const { return blitter_; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return watchdog_resets_; }
    int      watchdog_max_gap() const { return watchdog_max_; }
    uint32_t sound_commands() const { return sound_commands_; }
    uint32_t idle_skips() const { return idle_skips_; }
    // The frame the first boot's Advance was pressed at (0 = not yet).
    uint32_t advanced_at() const { return advanced_at_; }
    // The line the beam is on now (0-259).
    int      beam_line() const;

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static void    sound_command(void* ctx, uint8_t v);
    static void    main_irq(void* ctx);

    uint8_t io_read(uint16_t addr);
    void    io_write(uint16_t addr, uint8_t v);
    void    defender_bank(uint8_t bank);
    void    cmos_write(int at, uint8_t v);
    void    robotron_rom(bool on);
    void    set_input_ports(uint16_t buttons);
    uint16_t defender_stick(uint16_t buttons);
    int     now() const { return fc_ + cpu_.elapsed(); }
    void    run_main_to(int cycle);
    bool    skip_idle(int cycle);           // true: was in an idle loop

    Board          board_ = Board::Robotron;
    const uint8_t* rom_fixed_ = nullptr;    // D000-FFFF
    const uint8_t* rom_banked_ = nullptr;   // Robotron 0000-8FFF / Defender C000 banks
    m6809::Cpu     cpu_;
    Pia6821        pia_in_;                 // the "widget" PIA: controls (IN0, IN1)
    Pia6821        pia_rom_;                // IN2 (coins, service), sound command, IRQ
    WilliamsBlitter blitter_;
    williams_sound::Board sound_;

    uint8_t  ram_[0xc000] = {};             // 0000-97FF video, 9800-BFFF work RAM
    uint8_t  cmos_[0x400] = {};             // 4-bit: high nibble reads 1 once written
    uint8_t  palette_[16] = {};
    uint8_t  bank_ = 0;                     // Defender's C000 bank, Robotron's ROM enable
    uint16_t buttons_ = 0;
    // Defender's stick: a Reverse press in progress and the frames it has
    // been held.
    uint8_t  reverse_hold_ = 0;
    // The first boot finds the CMOS RAM empty, restores the factory
    // settings and waits for the operator's Advance button: pressed for
    // the player once the CMOS writes have stopped (see williams.cpp).
    uint32_t cmos_writes_ = 0, last_cmos_frame_ = 0, advanced_at_ = 0;
    uint8_t  advance_left_ = 0;
    bool     factory_done_ = false;

    int      fc_ = 0;                       // main CPU cycles into the frame
    int      snd_cycles_ = 0;               // 6808 cycles in this frame
    uint64_t snd_rem_ = 0, audio_rem_ = 0;
    uint32_t audio_rate_ = 24000;
    int      watchdog_ = 0, watchdog_resets_ = 0, watchdog_max_ = 0;
    uint32_t frame_ = 0;
    uint32_t sound_commands_ = 0;
    uint32_t io_accesses_ = 0;              // reads through the I/O callback (idle-loop check)
    uint32_t idle_skips_ = 0;
    uint8_t  probe_wait_ = 0, probe_backoff_ = 0;   // slices before the next idle probe

    uint16_t pen_[16] = {};                 // the palette as the frame ended, RGB565
    uint16_t rgb565_[256] = {};             // BBGGGRRR -> RGB565
    bool     swap_ = false;
};

} // namespace williams
