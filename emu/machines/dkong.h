// Donkey Kong (Nintendo, TKG-4 two-board set) arcade machine, written for
// this project (MIT). Z80 at 3.072 MHz (61.44 MHz / 20), 60.606 Hz frames
// (264 lines x 192 CPU cycles, 384 pixels a line at 6.144 MHz), NMI at the
// start of VBLANK (line 240), the 8257 DMA copying the sprite list each
// frame, a 32 x 32 tile layer coloured per column (PROM 5E) and 128
// sprites of 16 x 16 (16 a line at most). The sound board: an 8035 at
// 6 MHz playing the music and some effects through an 8-bit DAC from its
// own ROMs (emulated, so exact), and discrete circuits for walking,
// jumping and the stomp that the main CPU switches on and off (our own
// small models of them, emu/sound/dkong_sound.*). Hardware facts from
// public documentation and MAME's driver (read for facts only).
//
// The monitor is turned 90 degrees counter-clockwise, so the 256 x 224
// picture is shown 224 wide x 256 tall. render_line() makes one display
// line from a snapshot taken at the start of the visible frame - no frame
// buffer.
#pragma once

#include "../cpu/i8035.h"
#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/dkong_sound.h"

#include <cstdint>

namespace dkong {

constexpr int      kScreenW = 224;
constexpr int      kScreenH = 256;
constexpr uint32_t kCpuClock = 3072000;                 // 61.44 MHz / 20
constexpr int      kCyclesPerLine = 192;
constexpr int      kLinesPerFrame = 264;
constexpr int      kFirstLine = 16;                     // visible lines 16-239
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
// The 8035 runs at 6 MHz / 15 = 400,000 machine cycles a second: exactly
// 25 for every 192 Z80 cycles (one line).
constexpr uint32_t kSoundClock = 400000;
constexpr int      kSoundCyclesPerLine = 25;
constexpr int      kSoundCyclesPerFrame = kSoundCyclesPerLine * kLinesPerFrame;   // 6600

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kRight  = 0x001,
    kLeft   = 0x002,
    kUp     = 0x004,
    kDown   = 0x008,
    kJump   = 0x010,
    kCoin   = 0x020,
    kStart1 = 0x040,
    kStart2 = 0x080,
};

// DIP switches (DSW0, read at 7D80).
enum Dip : uint8_t {
    kDipLivesMask = 0x03, kDipLives3 = 0x00, kDipLives4 = 0x01, kDipLives5 = 0x02, kDipLives6 = 0x03,
    kDipBonusMask = 0x0c, kDipBonus7000 = 0x00, kDipBonus10000 = 0x04, kDipBonus15000 = 0x08,
                          kDipBonus20000 = 0x0c,
    kDipCoinMask  = 0x70, kDip1Coin1Credit = 0x00, kDip2Coins1Credit = 0x10, kDip1Coin2Credits = 0x20,
    kDipUpright   = 0x80,     // clear = cocktail
};
// 3 lives, bonus at 7000, 1 coin 1 credit, upright.
constexpr uint8_t kDipDefault = kDipLives3 | kDipBonus7000 | kDip1Coin1Credit | kDipUpright;

// The ROM set: MAME "dkong" (US set 1) or its clone "dkongo" (US set 2),
// which has other program ROMs (romset variants 1 and 2).
enum Region { kRegionCpu, kRegionSound, kRegionTiles, kRegionSprites, kRegionProms, kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;     // 16 KB program
    const uint8_t* sound;   // 8035: 2 KB program (3I), 2 KB data (3J)
    const uint8_t* tiles;   // 2 x 2 KB bit planes
    const uint8_t* sprites; // 4 x 2 KB: plane 0 left / right halves, plane 1 left / right
    const uint8_t* proms;   // 2 x 256 palette (inverted), 256 tile colours per column
};

// What the video hardware shows: copied as the visible frame starts.
struct VideoState {
    uint8_t vram[0x400];
    // The sprites that show, in drawing order: hardware x of their left
    // edge, first line, code, colour + flips, and which of their 16 lines
    // pass the 16-a-line limit.
    struct Sprite {
        uint8_t  x, y, code, attr;  // attr: colour (bits 0-3), flip x (7), flip y (6)
        uint16_t lines;
    };
    Sprite  sprites[128];
    uint8_t sprite_count;
    uint8_t palette_bank;           // 0-3: colour + 16 x bank
    bool    flip;
};

class Machine {
public:
    // `audio_rate`: output sample rate of run_frame()'s audio.
    // `swap_rgb565_bytes`: store pixels byte-swapped (SPI LCDs take them
    // big-endian).
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dsw0) { dsw0_ = dsw0; }

    // Runs one frame (from the first visible line to the next frame's).
    // Writes the frame's audio (about audio_rate / 60.6 samples) to
    // `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    // Renders display line y (0-255) of a snapshot into 224 RGB565 pixels.
    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(video_, y, out); }
    const VideoState& video() const { return video_; }

    z80::Cpu&   cpu() { return cpu_; }
    i8035::Cpu& sound_cpu() { return snd_; }
    uint32_t    frame() const { return frame_; }
    int         watchdog_resets() const { return watchdog_resets_; }
    // Diagnostics for tests and the PC tool.
    uint8_t     tune() const { return tune_; }
    uint8_t     sound_latch() const { return sound_bits_; }
    uint32_t    dma_count() const { return dma_count_; }
    uint32_t    dac_writes() const { return dac_writes_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t port_read(void* ctx, uint16_t port);
    static void    port_write(void* ctx, uint16_t port, uint8_t v);
    static uint8_t snd_ext_read(void* ctx, uint8_t addr);
    static uint8_t snd_port_in(void* ctx, uint8_t port);
    static void    snd_port_out(void* ctx, uint8_t port, uint8_t v);
    static uint8_t snd_test(void* ctx, uint8_t pin);

    void decode_gfx();
    void snapshot();
    void dma();
    uint8_t in2();
    int  now() const { return fc_ + cpu_.elapsed(); }     // main CPU cycle in this frame
    void sound_to(int main_cycle);      // runs the 8035 up to this point of the frame
    void audio_to(int snd_cycle);       // renders output samples up to this 8035 cycle
    void run_main_to(int cycle);

    Roms       roms_{};
    z80::Cpu   cpu_;
    i8035::Cpu snd_;
    dkong_sound::Board board_;

    uint8_t  ram_[0xc00] = {};      // 6000-6BFF
    uint8_t  sprite_ram_[0x400] = {};   // 7000-73FF (two banks of 0x200)
    uint8_t  vram_[0x400] = {};     // 7400-77FF
    uint8_t  dsw0_ = kDipDefault;
    uint16_t buttons_ = 0;

    // Latches on the CPU board.
    uint8_t  tune_ = 0;             // 7C00: music / effect number for the 8035
    uint8_t  sound_bits_ = 0;       // 7D00-7D07 (6H): bit n = the last write to 7D0n
    bool     nmi_mask_ = false;     // 7D84
    bool     flip_ = false;         // 7D82
    uint8_t  sprite_bank_ = 0;      // 7D83
    uint8_t  palette_bank_ = 0;     // 7D86 / 7D87
    bool     drq_ = false;          // 7D85

    // The 8257 DMA controller: four channels' address and count registers
    // (written low byte then high byte), the mode register.
    uint16_t dma_addr_[4] = {}, dma_count_reg_[4] = {};
    uint8_t  dma_mode_ = 0;
    bool     dma_flip_ = false;     // the byte flip-flop
    uint32_t dma_count_ = 0;        // transfers done (diagnostics)
    int      stall_ = 0;            // Z80 cycles the DMA held the bus

    int      fc_ = 0;               // main CPU cycles into the frame
    int      snd_done_ = 0;         // 8035 cycles into the frame
    int      watchdog_ = 0;
    int      watchdog_resets_ = 0;
    uint32_t frame_ = 0;
    uint32_t audio_rate_ = 24000;
    uint64_t audio_rem_ = 0;

    // Audio of the frame being run: sample k covers 8035 cycles
    // [k, k + 1) x 6600 / n; positions are in 8035 cycles x n.
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int64_t  audio_pos_ = 0;
    int64_t  dac_acc_ = 0;          // DAC level x time in the current sample
    uint8_t  dac_ = 0;
    uint32_t dac_writes_ = 0;

    VideoState video_{};
    // Graphics decoded once into display orientation, 2 bits a pixel:
    // tiles[code * 8 + column] = 8 rows, sprites[code * 16 + column] = 16.
    uint16_t tile_cols_[256 * 8] = {};
    uint32_t sprite_cols_[128 * 16] = {};
    uint16_t pen_[256] = {};        // RGB565 per (colour 0-63, pixel value)
};

} // namespace dkong
