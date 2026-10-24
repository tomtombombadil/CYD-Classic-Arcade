// 1942 (Capcom, 1984) arcade machine, written for this project (MIT).
// Hardware facts from public documentation and MAME's driver (read for
// facts only; Galagino's port was looked at for which shortcuts it takes,
// nothing taken from it):
// - main Z80 at 3 MHz (12 MHz / 4): ROM 0000-7FFF, a 16 KB bank at
//   8000-BFFF (C806 picks one of three chips), inputs C000-C004, the sound
//   commands C800 / C801, the background scroll C802-C803, control C804
//   (bit 4 holds the sound CPU in reset), the background palette bank
//   C805, sprites CC00-CC7F, characters D000-D7FF, background D800-DBFF,
//   RAM E000-EFFF; interrupt mode 0, the board puts an RST on the bus at
//   the lines the timing PROM (sb-1) marks: RST 08 at line 109 and RST 10
//   at line 240 (VBLANK), each held until taken;
// - sound Z80 at 3 MHz: ROM 0000-3FFF, RAM 4000-47FF, the two command
//   latches 6000 / 6001, two AY-3-8910s at 1.5 MHz (8000 / C000); its IRQ
//   four times a frame from the same PROM (lines 44, 109, 175, 240), held
//   until taken;
// - 262 lines of 384 pixel clocks at 6 MHz (59.64 Hz), lines 16-239 shown;
// - characters 8 x 8, 2 bits, 32 x 32 (pen 0 clear), on top; the
//   background 32 x 16 tiles of 16 x 16, 3 bits, scrolled along the
//   board's lines (9 bits; on the turned monitor: upwards), flips, a colour
//   lookup PROM and the palette bank; 32 sprites of 16 x 16 (16, 32, 64 or
//   256 lines tall), 4 bits, pen 15 clear, slot 0 on top, slots 16-23 only
//   above line 129 and 24-31 only below it; three 4-bit colour PROMs
//   through 2.2k / 1k / 470 / 220 ohm.
//
// Portrait 224 x 256: the board's 256-pixel lines 16-239 shown as columns
// (the monitor turned as MAME's ROT270: display x = line - 16, display y =
// 255 - pixel). A display line is one pixel column of the board.
#pragma once

#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/ay8910.h"

#include <cstdint>

namespace c1942 {

constexpr int      kScreenW = 224;
constexpr int      kScreenH = 256;
constexpr uint32_t kCpuClock = 3000000;                 // both Z80s
constexpr int      kCyclesPerLine = 192;                // 384 pixel clocks
constexpr int      kLinesPerFrame = 262;
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50304 -> 59.64 Hz

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kFire    = 0x010,   // button 1
    kLoop    = 0x020,   // button 2: the roll
    kCoin    = 0x040,
    kStart1  = 0x080,
    kStart2  = 0x100,
    kCoin2   = 0x200,
    kService = 0x400,
};

// DIP switches for set_dips(): DSWA's bonus (bits 4-5) and lives (bits
// 6-7), as the board reads them; coinage is fixed at 1 coin 1 credit, the
// cabinet upright, DSWB at its defaults (normal difficulty).
enum Dip : uint8_t {
    kDipBonusMask = 0x30, kDipBonus20k80k = 0x30, kDipBonus20k100k = 0x20, kDipBonus30k80k = 0x10,
    kDipBonus30k100k = 0x00,
    kDipLivesMask = 0xc0, kDipLives3 = 0xc0, kDipLives1 = 0x80, kDipLives2 = 0x40, kDipLives5 = 0x00,
};
constexpr uint8_t kDipDefault = kDipBonus20k80k | kDipLives3;

// The ROM set (MAME "1942" = Revision B, "1942a" = Revision A, Tom's,
// "1942b" = the first version, as variants); regions in this order. The
// program is split by chip so the loader can keep the most of it in RAM.
enum Region {
    kRegionMain0, kRegionMain1, kRegionBank0, kRegionBank1, kRegionBank2, kRegionSound,
    kRegionChars, kRegionTiles, kRegionSprites, kRegionProms, kRegionCount
};
extern const romset::Set kRomSet;

// The PROM region: 256 bytes each.
enum PromOffset : uint32_t {
    kPromChars = 0x000, kPromIrq = 0x100, kPromTiles = 0x200, kPromRed = 0x300, kPromGreen = 0x400,
    kPromBlue = 0x500, kPromSprites = 0x600, kPromSize = 0x700,
};

struct Roms {
    const uint8_t* main0;    // 0000-3FFF
    const uint8_t* main1;    // 4000-7FFF
    const uint8_t* bank[3];  // 16 / 8 / 16 KB at 8000
    const uint8_t* sound;    // 16 KB
    const uint8_t* chars;    // 8 KB: 512 characters
    const uint8_t* tiles;    // 48 KB: 512 tiles, three 16 KB bit planes
    const uint8_t* sprites;  // 64 KB: 512 sprites, two 32 KB halves
    const uint8_t* proms;    // kPromSize
};

// What a picture needs, copied at VBLANK (the snapshot for core 0).
struct alignas(4) VideoState {
    uint8_t  fg[0x800];      // character codes, then attributes
    uint8_t  bg[0x400];      // per column: 16 codes, 16 attributes (read as words: 4-aligned)
    uint8_t  spr[0x80];
    uint16_t scroll = 0;
    uint8_t  palette_bank = 0;
};

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dips) { dips_ = dips; }
    // The exact idle skips (on by default; tests and --hash runs compare
    // runs without them).
    void set_idle_skip(bool on) { idle_skip_ = on; }

    int run_frame(int16_t* audio, int cap);

    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(state_, y, out); }
    const VideoState& video() const { return state_; }

    // For tests.
    z80::Cpu& cpu() { return cpu_; }
    z80::Cpu& sound_cpu() { return snd_; }
    const ay8910::Chip& ay(int i) const { return ay_[i & 1]; }
    uint16_t pen(int i) const { return palette_[i & 0xff]; }
    uint32_t frame() const { return frame_; }
    uint8_t  bank() const { return bank_; }
    bool     sound_in_reset() const { return snd_reset_; }
    uint32_t main_irqs() const { return main_irqs_; }
    uint32_t sound_irqs() const { return snd_irqs_; }
    uint64_t skipped_cycles() const { return skipped_; }
    int      watchdog_resets() const { return 0; }   // the board has none
    uint8_t* ram() { return ram_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t snd_mem_read(void* ctx, uint16_t addr);
    static void    snd_mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t no_in(void*, uint16_t) { return 0xff; }
    static void    no_out(void*, uint16_t, uint8_t) {}

    void set_bank(uint8_t v);
    void control(uint8_t v);
    int  now() const { return main_done_ + cpu_.elapsed(); }
    int  sound_now() const { return snd_done_ + snd_.elapsed(); }
    void main_to(int cycle);
    void sound_to(int cycle);
    void audio_to(int snd_cycle);
    int16_t mix_sample(uint32_t clocks);
    bool skip_main_wait(int target);
    bool skip_sound_wait(int target);

    void decode_strip(const VideoState& vs, int col, int half) const;

    Roms     roms_{};
    z80::Cpu cpu_, snd_;
    ay8910::Chip ay_[2];

    uint8_t  ram_[0x1000] = {};     // E000-EFFF
    uint8_t  fg_[0x800] = {};       // D000-D7FF
    uint8_t  bg_[0x400] = {};       // D800-DBFF
    uint8_t  spr_[0x80] = {};       // CC00-CC7F
    uint8_t  snd_ram_[0x800] = {};  // sound 4000-47FF
    uint8_t  latch_[2] = {};
    uint8_t  scroll_[2] = {};
    uint8_t  control_ = 0;
    uint8_t  palette_bank_ = 0;
    uint8_t  bank_ = 0;
    uint8_t  dips_ = kDipDefault;
    uint16_t buttons_ = 0;

    // The timing PROM's interrupts: lines and what each raises.
    struct Event { int16_t line; uint8_t main_vector; bool sound; };
    Event    events_[8] = {};
    int      event_count_ = 0;
    bool     main_irq_ = false, snd_irq_ = false;    // raised, not yet taken
    bool     snd_reset_ = false;
    uint32_t main_irqs_ = 0, snd_irqs_ = 0;

    int      main_done_ = 0, snd_done_ = 0;           // cycles into the frame
    uint64_t snd_base_ = 0;                           // sound cycles before this frame
    uint32_t frame_ = 0;

    bool     idle_skip_ = true;
    bool     main_wait_ok_ = false, snd_wait_ok_ = false;
    uint64_t skipped_ = 0;

    // Audio: n samples over the frame's cycles; the AYs at half the clock.
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int      audio_clock_ = 0, sample_start_ = 0;
    uint32_t acc_[6] = {};
    float    dc_r_ = 0, dc_x_ = 0, dc_y_ = 0;

    // Pens (RGB565): the palette, the characters' and sprites' lookups;
    // the tile lookup PROM (indices into the palette's first 64).
    uint16_t palette_[256] = {};
    uint16_t char_pens_[256] = {};
    uint16_t spr_pens_[256] = {};
    uint8_t  tile_lut_[256] = {};
    uint8_t  char_cols_[512] = {};  // bit k: pixel column k of the character draws

    // Half a background column (8 pixel columns) being drawn, decoded once
    // for its 8 display lines: the RGB565 pens of its 8 pixel columns over
    // the 224 shown lines; valid while its column's 32 bytes and the bank
    // are the same (strip_key_, the bytes as words).
    alignas(4) mutable uint16_t strip_[8][kScreenW] = {};
    mutable uint32_t strip_key_[8] = {};
    mutable int      strip_id_ = -1;     // column x 2 + half, -1 = none
    mutable int      strip_bank_ = -1;

    VideoState state_{};
};

} // namespace c1942
