// Burger Time (Data East, 1982) arcade machine, written for this project
// (MIT). Hardware facts from public documentation and MAME's driver (read
// for facts only, never copied).
//
// Main CPU: Data East's "DECO CPU-7", an epoxy block holding a 6502 at
// 1.5 MHz (12 MHz / 8) and logic that scrambles opcodes: an opcode fetched
// right after an instruction that wrote to memory, from an address with
// bits 8 and 2 set, has its bits swapped (deco7_decrypt). There is no
// frame interrupt: the program polls VBLANK (DSW1 bit 7); a coin raises
// IRQ.
//
// Sound: a second 6502 at 500 kHz with 1 KB of RAM and two AY-3-8910s at
// 1.5 MHz. The main CPU's command latch raises its IRQ (cleared when it
// reads the latch); its NMI comes from the video's 8V line (every 16 lines,
// ~976 Hz) gated by an enable latch. Five of the six AY channels are mixed
// straight; channel A of the second AY goes through an op-amp band-pass
// (~190 Hz, part values from the board) and all through the speaker's
// coupling capacitor (~530 Hz high-pass) - the board's analog part, here
// a small model.
//
// Video: 6 MHz pixel clock, 384 x 272 per frame (57.44 Hz), 240 x 240
// shown. A 32 x 32 character layer (3 bits a pixel, 1024 characters),
// 8 sprites of 16 x 16 kept in video RAM (offsets 0x00, 0x20, 0x40...: a
// column that isn't shown), and a background of 16 x 16 tiles laid out by
// a map ROM (eight 256 x 256 pages, one shown). 16 colours from palette RAM (BGR 2-3-3, inverted):
// 0-7 for characters and sprites, 8-15 for the background. The monitor is
// turned 90 degrees counter-clockwise (upright cabinet, portrait).
// render_line() reads the live state (the picture as it is when run_frame()
// returns, at the start of the visible frame) - no frame buffer, no copy.
#pragma once

#include "../cpu/m6502.h"
#include "../rom/romset.h"
#include "../sound/ay8910.h"

#include <cstdint>

namespace btime {

constexpr int      kScreenW = 240;
constexpr int      kScreenH = 240;
constexpr uint32_t kCpuClock = 1500000;                 // 12 MHz / 8
constexpr int      kCyclesPerLine = 96;                 // 384 pixels at 6 MHz
constexpr int      kLinesPerFrame = 272;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;  // 26112 -> 57.44 Hz
constexpr int      kVisibleLines = 240;                 // lines 8-247; VBLANK 248-271, 0-7
constexpr int      kSoundDivider = 3;                   // the sound CPU: 500 kHz
constexpr int      kSoundCyclesPerFrame = kCyclesPerFrame / kSoundDivider;     // 8704

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kRight  = 0x001,
    kLeft   = 0x002,
    kUp     = 0x004,
    kDown   = 0x008,
    kPepper = 0x010,
    kCoin   = 0x020,
    kCoin2  = 0x040,
    kStart1 = 0x080,
    kStart2 = 0x100,
};

// DIP switches: set_dips() sets DSW2 (the game's options). DSW1 stays at
// 1 coin 1 credit for both slots, upright cabinet.
enum Dip : uint8_t {
    kDipLives3     = 0x01,      // clear = 5 lives
    kDipBonusMask  = 0x06, kDipBonus10000 = 0x06, kDipBonus15000 = 0x04, kDipBonus20000 = 0x02,
                           kDipBonus30000 = 0x00,
    kDipEnemies4   = 0x08,      // clear = 6 enemies
    kDipNoLevelPepper = 0x10,   // set = no extra pepper at the end of a level
    kDipUnused     = 0xe0,      // "should be off" (= set)
};
constexpr uint8_t kDipDefault = kDipLives3 | kDipBonus20000 | kDipEnemies4 | kDipUnused;
constexpr uint8_t kDsw1 = 0x3f;     // coins 1/1, "leave off" off, upright; bit 7 = VBLANK

// The ROM set: MAME "btime" (Burger Time, Data East set 1).
enum Region { kRegionCpu, kRegionSound, kRegionGfx, kRegionTiles, kRegionMap, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 16 KB at C000-FFFF (encrypted opcodes)
    const uint8_t* sound;   // 4 KB at E000 (and F000)
    const uint8_t* gfx;     // characters / sprites: 3 planes of 8 KB
    const uint8_t* tiles;   // background tiles: 3 planes of 2 KB
    const uint8_t* map;     // background map: 8 pages of 16 x 16 tile codes
};

// The DECO CPU-7's opcode scramble (a fact of the chip): `op` fetched at
// `addr`; `wrote` = the instruction before wrote to memory.
inline uint8_t deco7_decrypt(uint16_t addr, uint8_t op, bool wrote)
{
    if (!wrote || (addr & 0x0104) != 0x0104) return op;
    // Bits 7..0 come from bits 6 5 3 4 2 7 1 0.
    return uint8_t(((op & 0x40) << 1) | ((op & 0x20) << 1) | ((op & 0x08) << 2) | (op & 0x10) |
                   ((op & 0x04) << 1) | ((op & 0x80) >> 5) | (op & 0x03));
}

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons);
    void set_dips(uint8_t dsw2) { dsw2_ = dsw2; }

    // Runs one frame (from the first visible line to the next frame's).
    // Writes the frame's audio (about audio_rate / 57.44 samples) to
    // `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-239) into 240 RGB565 pixels.
    void render_line(int y, uint16_t* out) const;
    int  watchdog_resets() const { return 0; }     // the board has none

    m6502::Cpu& cpu() { return cpu_; }
    m6502::Cpu& sound_cpu() { return snd_; }
    ay8910::Chip& ay(int i) { return ay_[i]; }
    uint32_t frame() const { return frame_; }
    // Diagnostics for tests and the PC tool.
    uint8_t  sound_latch() const { return latch_; }
    uint32_t sound_nmis() const { return nmis_; }
    uint32_t coin_irqs() const { return coin_irqs_; }
    uint8_t  bg_control() const { return bg_ctrl_; }
    bool     flip() const { return flip_; }
    uint32_t main_cycles() const { return main_cycles_; }  // run last frame (VBLANK waits skipped)
    uint32_t glyph_decodes() const { return decodes_; }    // characters decoded into the cache, ever

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t opcode(void* ctx, uint16_t addr, uint8_t op, bool wrote);
    static uint8_t snd_opcode(void* ctx, uint16_t addr, uint8_t op, bool wrote);
    static uint8_t snd_read(void* ctx, uint16_t addr);
    static void    snd_write(void* ctx, uint16_t addr, uint8_t v);

    uint8_t read_dsw1();
    // A character (0-1023) or tile quarter (1024-1279), decoded (see the
    // cache below).
    const uint8_t* glyph(int id) const
    {
        const int s = slot_of_[id];
        if (!s) return glyph_miss(id);
        return cache_ + (s - 1) * 24;
    }
    const uint8_t* glyph_miss(int id) const;
    void update_pens();
    int  now() const { return fc_ + cpu_.elapsed(); }      // main CPU cycle in this frame
    void sound_to(int main_cycle);      // runs the sound CPU up to this point of the frame
    void audio_to(int main_cycle);      // renders output samples up to this point
    void set_vck(bool v);
    int16_t mix_sample(const uint32_t* acc, uint32_t clocks);

    Roms       roms_{};
    m6502::Cpu cpu_;
    m6502::Cpu snd_;
    ay8910::Chip ay_[2];

    uint8_t  ram_[0x800] = {};      // 0000-07FF
    uint8_t  pal_[0x100] = {};      // 0C00-0C0F (the page, for direct access)
    uint8_t  vram_[0x400] = {};     // 1000-13FF (also 1800-1BFF with x / y swapped)
    uint8_t  cram_[0x400] = {};     // 1400-17FF (1C00-1FFF): bits 0-1 = character bank
    uint8_t  snd_ram_[0x400] = {};  // sound CPU 0000-03FF (mirrored to 1FFF)
    uint8_t  dsw2_ = kDipDefault;
    uint16_t buttons_ = 0;

    uint8_t  latch_ = 0;            // 4003: command for the sound CPU
    bool     flip_ = false;         // 4002 bit 0
    uint8_t  bg_ctrl_ = 0;          // 4004: bit 4 background on, bit 2 / bits 0-1 the page
    bool     coin_held_ = false;    // the coin IRQ, held until the CPU takes it
    uint16_t irq_vector_ = 0;       // where the CPU goes on IRQ (to see it taken)
    bool     fresh_reset_ = true;   // the first fetch after reset decrypts as "no write"
    bool     nmi_enable_ = false;   // sound CPU NMI gate (C000 bit 0)
    bool     vck_ = false;          // the 8V line
    uint32_t nmis_ = 0, coin_irqs_ = 0;

    int      fc_ = 0;               // main CPU cycles into the frame
    int      stall_ = 0;            // cycles of a skipped VBLANK wait, added after the run
    bool     snd_idle_ = false;     // the sound CPU reached its idle loop
    bool     snd_nmi_due_ = false;  // an NMI raised, not yet run into
    int      snd_done_ = 0;         // sound CPU cycles into the frame
    uint32_t main_cycles_ = 0;
    uint32_t frame_ = 0;

    // Audio: AY clocks (= main CPU cycles) into the frame; sample k ends
    // at cycle (k + 1) x kCyclesPerFrame / n.
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int      audio_clock_ = 0, sample_start_ = 0;
    uint32_t acc_[6] = {};
    // The board's mixer: channel 2A's band-pass (biquad) and the output's
    // high-pass (the speaker's coupling capacitor).
    float    bp_b0_ = 0, bp_a1_ = 0, bp_a2_ = 0;
    float    bp_x1_ = 0, bp_x2_ = 0, bp_y1_ = 0, bp_y2_ = 0;
    float    dc_r_ = 0, dc_x_ = 0, dc_y_ = 0;
    static constexpr uint32_t kInvFirst = 56, kInvCount = 16;   // clocks a sample at 24 kHz: 62-63
    float    inv_[kInvCount] = {};

    // Graphics in display orientation, decoded as drawn into a small cache
    // (see glyph()): per 8 x 8 character / tile quarter and display line,
    // the 8 pixels of 3 bits packed into 24 bits (pixel k at bits 3k), as
    // 3 bytes. A 16 x 16 sprite / tile is 4 of them: right half top, right
    // bottom, left top, left bottom.
    static constexpr int kTileGlyphs = 1024;     // ids 0-1023 characters, then 256 tile quarters
    static constexpr int kSlots = 255;
    static constexpr uint16_t kNoGlyph = 0xffff;
    mutable uint8_t  cache_[kSlots * 24] = {};
    mutable uint8_t  slot_of_[kTileGlyphs + 256] = {};  // slot + 1, 0 = not cached
    mutable uint16_t owner_[kSlots] = {};
    mutable uint8_t  hand_ = 0;
    mutable uint32_t decodes_ = 0;
    uint16_t pen_[16] = {};         // RGB565 per palette entry
    uint16_t over_[64] = {};        // character pixel x 8 + background pixel -> RGB565
    bool     swap_ = false;
};

} // namespace btime
