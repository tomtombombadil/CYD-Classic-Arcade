// Gyruss (Konami, 1983) arcade machine, written for this project (MIT).
// Four CPUs: the main Z80 at 3.072 MHz (game logic, tiles, NMI at VBLANK),
// the sub CPU - a Konami-1 (a 6809 whose opcode bytes are scrambled by
// address: emu/cpu/m6809's opcode pages, see gyruss.cpp) at 1.536 MHz that
// copies the main CPU's sprite queue into the sprite RAM (IRQ at VBLANK),
// the sound Z80 at 3.58 MHz with five AY-3-8910s (emu/sound/ay8910), and an
// i8039 (emu/cpu/i8035) playing the extra effects through an 8-bit DAC.
// Hardware facts from public documentation and MAME's driver (read for
// facts only).
//
// The board draws 256-pixel scanlines, 256 lines a frame, lines 16-239
// shown; the monitor is turned 90 degrees clockwise, so the player sees
// 224 x 256: display line y = pixel y along the scanline, display x = 239 -
// hardware line.
#pragma once

#include "../cpu/i8035.h"
#include "../cpu/m6809.h"
#include "../cpu/z80.h"
#include "../rom/romset.h"
#include "../sound/ay8910.h"

#include <cstdint>

namespace gyruss {

constexpr int      kScreenW = 224;                      // hardware lines 16-239
constexpr int      kScreenH = 256;                      // pixels along the scanline
constexpr int      kFirstLine = 16;
constexpr int      kLastLine = 239;
constexpr uint32_t kCpuClock = 3072000;                 // 18.432 MHz / 6
constexpr int      kCyclesPerLine = 198;                // 396 pixel clocks at 6.144 MHz
constexpr int      kLinesPerFrame = 256;
constexpr int      kVblankLine = 240;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 50688 -> 60.606 Hz
constexpr uint32_t kSoundXtal = 14318181;               // sound Z80 = / 4, AYs = / 8
constexpr uint32_t kI8039Clock = 8000000;               // a machine cycle = 15 clocks
constexpr int      kI8039PerFrame = 8800;               // machine cycles a frame (exactly)
constexpr int      kMaxSprites = 48;                    // the sprite RAM's slots
constexpr int      kSliceLines = 2;                     // the main and sub CPUs take turns this often
// Sprite RAM contents shown over a frame: each slot's value, with the lines
// it was there for (the sub CPU rewrites slots as the beam goes down).
constexpr int      kMaxEntries = 192;

// Buttons for set_inputs() (pressed = bit set).
enum Button : uint16_t {
    kUp      = 0x001,
    kDown    = 0x002,
    kLeft    = 0x004,
    kRight   = 0x008,
    kFire    = 0x010,
    kCoin    = 0x020,
    kStart1  = 0x040,
    kStart2  = 0x080,
    kCoin2   = 0x100,
    kService = 0x200,   // service credit
};

// DIP switch bank 2 for set_dips() (active high as read: lives 0x03, bonus
// 0x08, difficulty 0x70); the cabinet is fixed upright, the demo sounds
// (bit 7 and bank 3's demo music) go through set_demo_sounds(), coinage
// (bank 1) is fixed at 1 coin 1 credit.
enum Dip : uint8_t {
    kDipLivesMask  = 0x03, kDipLives3 = 0x03, kDipLives4 = 0x02, kDipLives5 = 0x01, kDipLives255 = 0x00,
    kDipBonus30k   = 0x08,  // set = 30k 90k 60k+, clear = 40k 110k 70k+
    kDipDiffMask   = 0x70, kDipDiffAverage = 0x30,
};
constexpr uint8_t kDipDefault = kDipLives3 | kDipBonus30k | kDipDiffAverage;

// The ROM set (MAME "gyruss", Konami); regions in this order.
enum Region { kRegionCpu, kRegionSub, kRegionSound, kRegionI8039, kRegionSprites, kRegionTiles, kRegionProms,
              kRegionCount };
extern const romset::Set kRomSet;

struct Roms {
    const uint8_t* cpu;      // 24 KB main program
    const uint8_t* sub;      // 8 KB sub program (E000-FFFF, opcodes scrambled)
    const uint8_t* sound;    // 16 KB sound program
    const uint8_t* i8039;    // 4 KB
    const uint8_t* sprites;  // 32 KB: 2 x 256 sprites of 8 x 16, 4 bits
    const uint8_t* tiles;    // 8 KB: 512 tiles of 8 x 8, 2 bits
    const uint8_t* proms;    // 32 B palette, 256 B sprite colours, 256 B tile colours
};

// Konami-1: an opcode byte fetched at `addr` as the CPU sees it.
inline uint8_t konami1_decrypt(uint16_t addr, uint8_t v)
{
    static constexpr uint8_t kXor[4] = {0x22, 0x82, 0x28, 0x88};   // by address bits 1 and 3
    return uint8_t(v ^ kXor[((addr >> 1) & 1) | ((addr >> 2) & 2)]);
}

// One sprite as shown over lines [from, to) of the frame.
struct Sprite {
    uint8_t x;          // pixel along the scanline
    uint8_t flags;      // bit 0 flip x, bit 1 flip y
    uint8_t color;      // 0-15
    uint8_t from, to;   // hardware lines (to: 240 = the frame's end)
    int16_t top;        // hardware line of its first row
    uint16_t code;      // 0-511: bank (the low bit) | sprite << 1
};

// What the video hardware shows: built at VBLANK.
struct VideoState {
    uint8_t  vram[0x400];       // tile codes, 32 x 32
    uint8_t  cram[0x400];       // tile attributes: 0-3 colour, 4 in front of sprites, 5 code bit 8, 6 / 7 flips
    uint16_t count;             // sprites, back to front
    Sprite   spr[kMaxEntries];
    // By band of 8 display lines (8 pixels along the scanline): the
    // sprites crossing it, band b's are band_spr[band_at[b] .. band_at[b+1]).
    uint16_t band_at[33];
    uint8_t  band_spr[2 * kMaxEntries];
};

class Machine {
public:
    void init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    void set_dips(uint8_t dips) { dips_ = dips; }
    // DSW2 bit 7 (demo sounds) and DSW3 bit 0 (demo music), on = the
    // factory setting.
    void set_demo_sounds(bool on) { demo_sounds_ = on; }
    // The CPUs' idle loops skipped (exact; on by default - tests compare
    // runs without).
    void set_idle_skip(bool on) { idle_skip_ = on; }

    // Runs one frame. Writes the frame's audio (about audio_rate / 60.6
    // samples) to `audio` (up to `cap`; null = none) and returns the count.
    int run_frame(int16_t* audio, int cap);

    void render_line(const VideoState& vs, int y, uint16_t* out) const;
    void render_line(int y, uint16_t* out) const { render_line(state_, y, out); }
    const VideoState& video() const { return state_; }

    z80::Cpu&    cpu() { return cpu_; }
    m6809::Cpu&  sub_cpu() { return sub_; }
    z80::Cpu&    sound_cpu() { return snd_; }
    i8035::Cpu&  i8039() { return mcu_; }
    const ay8910::Chip& ay(int i) const { return ay_[i]; }
    uint32_t frame() const { return frame_; }
    int      watchdog_resets() const { return 0; }     // the board has no watchdog
    uint8_t  dac() const { return dac_out_; }
    uint64_t skipped_cycles(int cpu) const { return skipped_[cpu & 3]; }
    uint16_t pen(int entry) const { return pens_[entry & 31]; }

    // Tables (tests): the sound board's timer as AY 3's port A reads it.
    static uint8_t timer_bits(uint64_t sound_cycles);
    uint8_t timer_at(int snd_cycle) const;

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t sub_read(void* ctx, uint16_t addr);
    static void    sub_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t sub_opcode(void* ctx, uint16_t addr);
    static uint8_t snd_mem_read(void* ctx, uint16_t addr);
    static void    snd_mem_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t snd_port_read(void* ctx, uint16_t port);
    static void    snd_port_write(void* ctx, uint16_t port, uint8_t v);
    static uint8_t mcu_ext_read(void* ctx, uint8_t addr);
    static uint8_t mcu_port_read(void* ctx, uint8_t port);
    static void    mcu_port_write(void* ctx, uint8_t port, uint8_t v);
    static uint8_t no_in(void*, uint16_t) { return 0xff; }
    static void    no_out(void*, uint16_t, uint8_t) {}

    void vblank();
    void capture_tiles();
    void capture_sprites();
    void sprite_write(int offset, uint8_t v);
    uint8_t in(int port) const;
    int  now() const { return slice_start_ + cpu_.elapsed(); }
    int  sub_now() const { return sub_start_ + sub_.elapsed(); }
    int  sound_now() const { return snd_done_ + snd_.elapsed(); }
    int  mcu_now() const { return mcu_done_ + mcu_.elapsed(); }
    void run_sub(int target);
    void run_sub_to(int target);
    void sub_catch_up();            // the sub CPU's turns put off (see gyruss.cpp)
    void sub_behind(int due);
    // Runs the sound CPU to where the main CPU's frame cycle has reached.
    void sound_to(int main_cycle);
    void flush_cmds();
    // Runs the i8039 to a sound-CPU cycle of this frame, then the audio.
    void sync_audio(int snd_cycle);
    void mcu_to(int snd_cycle);
    // Completes the output samples that end by this sound cycle.
    void audio_to(int snd_cycle);
    bool needs_flush(int snd_cycle) const;      // no room in rows_ for an AY run to snd_cycle
    void ay_write(int chip, uint8_t v, int at);
    void defer_sound(int what, uint8_t v, int at);
    void apply_sound();
    // Sample j's end (sound cycles this frame).
    int  sample_end(int j) const { return j + 1 < audio_n_ ? (j + 1) * snd_frame_ / audio_n_ : snd_frame_; }
    void chip_to(int chip, int snd_cycle);      // one AY, into the samples' rows
    void dac_to(int snd_cycle);                 // the DAC, into the samples' rows
    void set_filter(int chip, uint8_t bits);
    int16_t mix_sample(const uint32_t* acc, uint32_t dac_acc, uint32_t clocks);
    int  main_idle(int room);
    int  sub_idle(int room);
    void skip_timer_wait(int target);
    int  mcu_idle(int room);

    Roms     roms_{};
    z80::Cpu cpu_, snd_;
    m6809::Cpu sub_;
    i8035::Cpu mcu_;
    ay8910::Chip ay_[5];

    uint8_t  vcram_[0x800] = {};    // 8000-83FF colour RAM, 8400-87FF video RAM
    uint8_t  ram_[0x1000] = {};     // 9000-9FFF
    uint8_t  shared_[0x800] = {};   // main A000-A7FF = sub 6000-67FF
    uint8_t  sub_ram_[0x800] = {};  // sub 4000-47FF (sprites at 4040-40FF)
    uint8_t  snd_ram_[0x400] = {};  // sound 6000-63FF
    uint8_t  sub_ops_[0x2000] = {}; // the sub program as its opcode fetches see it
    bool     nmi_mask_ = false, nmi_line_ = false;
    bool     sub_irq_mask_ = false;
    uint8_t  latch_ = 0, latch2_ = 0;
    bool     snd_irq_ = false;      // held until the sound CPU takes it
    // The main CPU's sound writes this frame, in order: the sound board runs
    // to each one's time and takes it at the frame's end (flush_cmds) - it
    // only listens, so nothing on the main side changes. (One component at
    // a time keeps the S3's 16 KB instruction cache from thrashing.)
    struct Cmd { int at; uint8_t command; uint8_t v; };     // command 0 = the IRQ, 1 = C100
    static constexpr int kMaxCmds = 64;
    Cmd      cmds_[kMaxCmds] = {};
    int      cmd_n_ = 0;
    // A sound CPU write put off to the end of its instruction (defer_sound):
    // an AY (0-4) or the i8039's interrupt / command.
    enum : int { kSndMcuIrq = 5, kSndMcuCmd = 6 };
    bool     snd_defer_ = false;
    int      snd_what_ = 0, snd_at_ = 0;
    uint8_t  snd_v_ = 0;
    uint8_t  dips_ = kDipDefault;
    bool     demo_sounds_ = true;
    uint16_t buttons_ = 0;
    int      carry_ = 0;
    int      slice_start_ = 0;      // main cycle of the slice's start
    int      sub_start_ = 0;        // sub cycle of its run's start
    int      sub_done_ = 0;         // sub cycles run this frame
    int      sub_due_ = 0;          // sub cycles it must have run before the main CPU's slice
    bool     sub_lag_ = false;      // behind: the shared RAM unmapped for the main CPU
    bool     sub_vbl_ = false;      // the main CPU is past VBLANK, the sub CPU not yet
    static constexpr int kSubVblank = kVblankLine * kCyclesPerLine / 2;
    // The main CPU's shared RAM writes while the sub CPU is behind, with
    // the sub cycle from which it sees each (only a main CPU read of the
    // shared RAM, VBLANK or the frame's end make the sub CPU catch up).
    struct SubWrite { uint16_t due, addr; uint8_t v; };
    static constexpr int kSubLog = 256;
    SubWrite sub_log_[kSubLog] = {};
    int      sub_log_n_ = 0;
    uint32_t frame_ = 0;

    // Sprites: each slot's value since line slot_from_ (writes while the
    // beam is in lines 0-239 close the old value's lines).
    uint8_t  slot_from_[kMaxSprites] = {};
    uint16_t entries_ = 0;
    Sprite   pending_[kMaxEntries] = {};
    uint8_t  pending_slot_[kMaxEntries] = {};

    // Sound board time: its cycles this frame (59,062.5 on average, the
    // remainder carried), the total before this frame.
    int      snd_frame_ = 0;
    uint32_t snd_rem_ = 0;          // what the frames so far left over (in 1 / (4 x 3.072 MHz) of a sound cycle)
    int      snd_done_ = 0;
    uint64_t snd_base_ = 0;
    uint32_t snd_base10_ = 0;       // snd_base_ % 10240 (the timer's round)
    int      mcu_done_ = 0;         // i8039 cycles this frame
    uint8_t  dac_ = 0x80;           // the DAC as of dac_clock_
    uint8_t  dac_out_ = 0x80;       // the i8039's last P1 write
    // The DAC's changes not yet in the audio (the i8039 writes it often:
    // they don't make the AYs catch up).
    struct DacEvent { int at; uint8_t v; };
    static constexpr int kDacQueue = 64;
    DacEvent dac_q_[kDacQueue] = {};
    int      dac_qn_ = 0;
    int      dac_clock_ = 0;

    // Idle loops (gyruss.cpp), each found by its code at init.
    bool     idle_skip_ = true;
    bool     main_loop_ = false, sub_loop_ = false, snd_loop_ = false, mcu_loop_ = false;
    bool     nmi_now_ = false;      // an NMI raised for this slice: no skipping
    bool     wait_read_ = false;    // the sound CPU just read the timer in its wait
    uint8_t  wait_stay_ = 0;        // the timer bit 2 that keeps it waiting
    int      wait_at_ = 0;          // the read's sound cycle (this frame)
    uint64_t skipped_[4] = {};

    // Audio: n samples over the frame's sound cycles.
    uint32_t audio_rate_ = 24000;
    uint32_t audio_rem_ = 0;        // the same for the samples (in 1 / 3.072 MHz of a sample)
    uint32_t audio_q_ = 0, audio_r_ = 0;    // audio_rate_ x a frame's cycles / 3.072 MHz, and the remainder
    int16_t* audio_ = nullptr;
    int      audio_n_ = 0, audio_done_ = 0;
    int      ay_clock_[5] = {};     // each AY's sound cycle this frame
    int      ay_sample_[5] = {};    // the sample it is in
    int      dac_sample_ = 0;       // the sample dac_clock_ is in
    // The output samples not finished yet: row r = sample audio_done_ + r,
    // the 15 AY channels' level x AY clocks (chip x 3 + channel), [15] the
    // DAC's level x sound cycles.
    static constexpr int kRows = 32;
    uint32_t rows_[kRows][16] = {};
    uint32_t scale_clocks_[2] = {}; // mix_sample(): 1 / clocks for the last two clocks
    float    scale_[2] = {};
    int      scale_next_ = 0;
    float    lp_tab_[4] = {};       // a channel's filter step by its two capacitor bits
    uint8_t  filter_bits_[2] = {};
    float    lp_a_[6] = {};         // AY 1 / 2's channels: 0 = no capacitor, else the low-pass step
    float    lp_y_[6] = {};
    float    out_a_ = 0, out_y_ = 0;
    float    dc_r_ = 0, dc_x_ = 0, dc_y_ = 0;

    uint16_t pens_[32] = {};
    uint16_t tile_pen_[64] = {};
    // Tiles in display orientation: per (code, pixel x along the scanline)
    // the 8 pixels down its lines, 2 bits each (line j in bits 2j+1..2j).
    uint16_t tile_cols_[512 * 8] = {};
    uint16_t sprite_pen_[256] = {};
    VideoState state_{};
};

} // namespace gyruss
