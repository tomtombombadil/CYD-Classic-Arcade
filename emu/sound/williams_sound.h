// The Williams video-game sound board (Defender, Stargate, Robotron, Joust,
// ...), written for this project (MIT): a 6808 (3.579545 MHz crystal, E =
// 894.9 kHz) running the sound ROM, 256 bytes of RAM (the 6808 has none;
// 0000-007F + a 6810 at 0080-00FF), a 6821 PIA at 0400 (and 8400) and an
// MC1408 8-bit DAC on the PIA's port A. The main board's sound command
// comes in on port B (its top two bits pulled high) and CB1 (high unless
// the command is FF); the PIA's interrupts drive the 6808's IRQ. All of
// the sound is this program writing the DAC, so it is exact. The ROM is
// mapped at the top of E000-FFFF and seen again below it. Facts from MAME's
// driver (read for facts only).
//
// The owner runs it in step with the main CPU: begin_frame() with the
// frame's 6808 cycle count, run_to() before each command, end_frame().
#pragma once

#include "../cpu/m6800.h"
#include "../machines/pia6821.h"

#include <cstdint>

namespace williams_sound {

constexpr uint32_t kCrystal = 3579545;          // the 6808 divides it by 4

class Board {
public:
    // `rom`: the sound ROM (a power of two, 2 or 4 KB).
    void init(const uint8_t* rom, uint32_t rom_size, uint32_t audio_rate);
    void reset();

    // A frame of `cycles` 6808 cycles and `samples` output samples into
    // `audio` (null = no output).
    void begin_frame(int16_t* audio, int samples, int cycles);
    void run_to(int cycle);             // runs the 6808 to this cycle of the frame
    void command(uint8_t cmd);          // the main board's 6 bits (call run_to first)
    void end_frame();                   // runs to the frame's end, fills every sample

    m6800::Cpu& cpu() { return cpu_; }
    Pia6821&    pia() { return pia_; }
    uint8_t     dac() const { return dac_; }
    uint32_t    dac_writes() const { return dac_writes_; }
    int         done() const { return done_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static void    dac_write(void* ctx, uint8_t v);
    static void    irq_changed(void* ctx);
    void dac_set(uint8_t v);
    void audio_to(int cycle);

    m6800::Cpu cpu_;
    Pia6821    pia_;
    uint8_t    ram_[0x400] = {};        // one CPU page; 0000-00FF are the chips
    const uint8_t* rom_ = nullptr;
    uint32_t   rom_mask_ = 0;

    uint8_t  dac_ = 0x80;
    uint32_t dac_writes_ = 0;

    // The frame being run: sample k covers cycles [k, k + 1) x cycles_ /
    // samples_; positions are in cycles x samples_.
    int16_t* audio_ = nullptr;
    int      samples_ = 0, cycles_ = 1, done_ = 0, out_ = 0;
    float    inv_cycles_ = 1.0f;
    int64_t  pos_ = 0, acc_ = 0;
    // DC-blocking high-pass (the board's output coupling).
    float    hp_x_ = 0, hp_y_ = 0, hp_a_ = 0.997f;
};

} // namespace williams_sound
