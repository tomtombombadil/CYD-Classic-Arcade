// Taito Qix (1981), written for this project (MIT). Two 6809s at 1.25 MHz
// (20 MHz / 16): the data CPU (game logic, inputs, sound commands) and the
// video CPU (all drawing), sharing 1 KB of RAM at 8000 and interrupting
// each other with FIRQs (a write or read of 8C00 raises the other CPU's
// FIRQ, 8C01 clears one's own). The video CPU owns the 256 x 256 bitmap of
// 8-bit pixels (64 KB, read back by the game), the 1 KB palette RAM (4
// banks of 256 colours RRGGBBII), the CMOS RAM (8400) and a 6845 CRTC that
// sets the timing: 41 x 8 pixel clocks a line, 273 lines (256 shown), so
// 82 CPU cycles a line, 22,386 a frame = 55.84 Hz; VSYNC lines 256-271.
// The data CPU has three 6821 PIAs for the controls / coins and a fourth
// to the sound board (emu/sound/qix_sound.*): port A = the command, CA2 =
// its interrupt, port B = the volume, CB1 = VSYNC (the data CPU's frame
// interrupt). Hardware facts from MAME's driver (read for facts only).
//
// Video RAM: the video CPU sees one 32 KB half at 0000-7FFF, picked by
// bit 7 of the address latch's high byte (9402); 9400 reads / writes the
// byte at the latched address (9402 high, 9403 low). Pixel (x, y) of the
// CRTC's picture is byte y * 256 + x. The two halves are separate 32 KB
// blocks on the heap (see Machine()), so no single 64 KB block is needed.
//
// The monitor is turned (MAME's ROT270): the player sees the CRTC's lines
// as columns, line 0 on the left. Shown: 240 x 256, lines 8-247 - the
// playfield, scores and text lie in lines 16-240, so a 240-wide panel
// shows the game 1:1 (scaling 256 down would drop columns, and with them
// Qix's one-pixel lines). Flip screen (cocktail) isn't drawn. The CMOS
// RAM starts empty, as a new board's does: the first boot writes the
// factory settings and then shows the operator's setup pages (language,
// settings, high scores, audits, coins), which wait for the Advance
// switch; the machine then restarts the board with those settings kept
// (as a power cycle with the CMOS battery would) and the game comes up.
#pragma once

#include "../cpu/m6809.h"
#include "../rom/romset.h"
#include "../sound/qix_sound.h"
#include "pia6821.h"

#include <cstdint>

namespace qix {

constexpr int      kScreenW = 240;              // CRTC lines 8-247 (see render_line)
constexpr int      kScreenH = 256;
constexpr int      kFirstLine = 8;
constexpr uint32_t kCpuClock = 1250000;
constexpr int      kCyclesPerLine = 82;                 // 328 pixel clocks at 5 MHz
constexpr int      kLinesPerFrame = 273;
constexpr int      kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;   // 22386 -> 55.84 Hz

enum Button : uint16_t {
    kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008,
    kFast = 0x0010, kSlow = 0x0020, kStart1 = 0x0040, kStart2 = 0x0080,
    kCoin = 0x0100, kCoin2 = 0x0200, kAdvance = 0x0400, kTilt = 0x0800,
};

// The data CPU's C000-FFFF, the video CPU's C800-FFFF, the sound CPU's
// F800-FFFF.
enum Region { kRegionData, kRegionVideo, kRegionSound, kRegionCount };
extern const romset::Set kRomSet;

class Machine {
public:
    // Allocates the video RAM (two 32 KB blocks, nothrow): check ok().
    Machine();
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    bool ok() const { return vram_[0] && vram_[1]; }

    // `regions` in Region order.
    void init(const uint8_t* const* regions, uint32_t audio_rate, bool swap_rgb565_bytes = false);
    void reset();

    void set_inputs(uint16_t buttons) { buttons_ = buttons; }
    // Idle-loop skipping on (default) or off (to check it changes nothing).
    void set_idle_skipping(bool on) { idle_skipping_ = on; }

    // Runs one frame (from the start of VSYNC to the next); writes its
    // audio (signed 16-bit, up to `cap`) and returns the sample count.
    int  run_frame(int16_t* audio, int cap);
    // Line y (0-255) of the picture as the player sees it: 240 RGB565 pixels.
    void render_line(int y, uint16_t* out) const;

    m6809::Cpu&      data_cpu() { return side_[kData].cpu; }
    m6809::Cpu&      video_cpu() { return side_[kVideo].cpu; }
    qix_sound::Board& sound() { return sound_; }
    Pia6821&         sound_pia() { return snd_pia_; }
    uint8_t*         shared_ram() { return shared_; }
    uint8_t*         data_ram() { return data_ram_; }
    uint8_t*         cmos() { return cmos_; }
    uint8_t*         palette_ram() { return palette_; }
    uint8_t          video_byte(uint16_t addr) const { return vram_[addr >> 15][addr & 0x7fff]; }
    uint8_t          palette_bank() const { return palette_bank_; }
    uint8_t          crtc_reg(int r) const { return crtc_[r & 31]; }
    uint32_t         frame() const { return frame_; }
    uint32_t         sound_commands() const { return sound_commands_; }
    uint32_t         idle_skips() const { return idle_skips_; }
    // The frame the board was restarted after the first boot's settings
    // (0 = not yet).
    uint32_t         restarted_at() const { return cmos_restart_ ? cmos_valid_at_ : 0; }
    bool             cmos_valid() const;
    // CPU cycles run (not skipped) so far, for profiling.
    uint64_t         cycles_run(int cpu) const { return side_[cpu].ran; }
    // The line the beam is on at frame cycle t (0-272; 0-255 shown).
    static int       beam_line(int t);
    // What 9800 reads at frame cycle t: the shown line, 255 in the blank.
    static uint8_t   scanline_at(int t);

    enum { kData = 0, kVideo = 1 };

private:
    // One 6809 and its place in the frame.
    struct Side {
        m6809::Cpu cpu;
        int      t = 0;                     // cycles into the frame (where its run started)
        bool     asleep = false;            // in an idle loop: time stands still until woken
        int      round = 0;                 // the idle loop's cycles a turn
        int      wake_at = 0;               // ... and when it must look again (scanline loops)
        uint8_t* counter = nullptr;         // ... the byte a counting wait adds 1 to a turn
        int      watch = -1, watch_count = -1;  // ... the bytes it reads (-1: any)
        int      firq_at = -1;              // a FIRQ raised by the other CPU, due at this cycle
        uint8_t  probe_wait = 0, probe_backoff = 0;
        uint32_t io = 0, scan_reads = 0;    // I/O accesses (idle-loop check)
        uint64_t ran = 0;
    };

    static uint8_t data_read(void* ctx, uint16_t addr);
    static void    data_write(void* ctx, uint16_t addr, uint8_t v);
    static uint8_t video_read(void* ctx, uint16_t addr);
    static void    video_write(void* ctx, uint16_t addr, uint8_t v);
    static void    snd_pia_port_a(void* ctx, uint8_t v);
    static void    snd_pia_port_b(void* ctx, uint8_t v);
    static void    snd_pia_irq(void* ctx);
    static void    sound_port_a(void* ctx, uint8_t pins);
    static void    sound_ca2(void* ctx, bool level);

    // Frame cycle of CPU s: mid-instruction while it runs.
    int  now(int s) const { return s == running_ ? side_[s].t + side_[s].cpu.elapsed() : side_[s].t; }
    int  sound_cycle(int t) const { return int(int64_t(t) * snd_cycles_ / kCyclesPerFrame); }
    void sync_sound() { sound_.run_to(sound_cycle(now(kData))); }
    void raise_firq(int from);              // `from` hits the other CPU's FIRQ
    void wake(int s, int at);
    void shared_write(int s, uint16_t addr, uint8_t v);
    void map_shared_writes();
    void map_video_window(uint8_t latch_hi);
    void snd_pia_ca2_check();
    void set_input_ports();
    void run_cpus_to(int target);
    bool probe_idle(int s, int limit);
    int  due(int s, int target) const;

    Side           side_[2];
    int            running_ = -1;           // the CPU inside run(), or -1
    Pia6821        pia_[3];                 // U11 (P1, coins), U20 (spare), U30 (P2, coin control)
    Pia6821        snd_pia_;                // the data CPU's sound PIA
    qix_sound::Board sound_;
    const uint8_t* rom_data_ = nullptr;
    const uint8_t* rom_video_ = nullptr;

    uint8_t* vram_[2] = {nullptr, nullptr}; // the two 32 KB halves (heap)
    uint8_t  shared_[0x400] = {};           // 8000-83FF, both CPUs
    uint8_t  data_ram_[0x400] = {};         // data CPU 8400-87FF
    uint8_t  cmos_[0x400] = {};             // video CPU 8400-87FF
    uint8_t  palette_[0x400] = {};          // video CPU 9000-93FF: 4 banks
    uint8_t  latch_[2] = {};                // 9402 / 9403
    uint8_t  window_ = 0;                   // the half mapped at 0000-7FFF
    uint8_t  palette_bank_ = 0;
    uint8_t  crtc_[32] = {};
    uint8_t  crtc_sel_ = 0;
    bool     snd_ca2_ = true;
    bool     idle_skipping_ = true;
    uint16_t buttons_ = 0;

    int      snd_cycles_ = 0;               // 6802 cycles in this frame
    uint64_t snd_rem_ = 0, audio_rem_ = 0;
    uint32_t audio_rate_ = 24000;
    uint32_t frame_ = 0;
    uint32_t sound_commands_ = 0;
    uint32_t idle_skips_ = 0;
    // The first boot: the frame the CMOS was first seen valid, and whether
    // the board has been restarted with it (see run_frame()).
    uint32_t cmos_valid_at_ = 0;
    bool     cmos_restart_ = false;

    uint16_t pen_[256] = {};                // the palette bank as the frame ended, RGB565
    uint16_t rgb565_[256] = {};             // RRGGBBII -> RGB565
    bool     swap_ = false;
};

} // namespace qix
