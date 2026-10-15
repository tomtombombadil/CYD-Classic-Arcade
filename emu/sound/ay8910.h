// General Instrument AY-3-8910 programmable sound generator, written for
// this project (MIT). Plain C++17, no allocation.
//
// Hardware facts (GI's data sheet, public measurements; MAME's ay8910.cpp
// read for facts only):
// - 16 registers: R0-R5 tone periods of channels A B C (12 bits: fine,
//   coarse low nibble), R6 noise period (5 bits), R7 mixer (bits 0-2 tone
//   OFF for A B C, bits 3-5 noise OFF, bit 6 / 7 = port A / B is an
//   output), R8-R10 amplitudes (4 bits; bit 4 = follow the envelope),
//   R11-R12 envelope period (16 bits), R13 envelope shape (CONTinue,
//   ATTack, ALTernate, HOLD), R14-R15 the I/O ports.
// - Bus: an address write latches the register number; a value with its
//   high nibble set deselects the chip (its mask-programmed address), so
//   data writes are then ignored.
// - Everything counts on the input clock / 8 (a "tick"). A tone counter
//   counts up and flips its square wave when it reaches the period (0 acts
//   as 1): f = clock / (16 x period). The noise is a 17-bit shift register
//   (new bit 16 = bit 0 XOR bit 3, output = bit 0), stepped every 2 x
//   period ticks. The envelope steps every 2 x period ticks (period 0 =
//   every tick), 16 steps a ramp: f = clock / (256 x period). Writing R13
//   restarts the envelope.
// - A channel sounds (tone output OR tone off) AND (noise output OR noise
//   off), so with both off it is a steady level the volume can modulate.
// - The DAC is logarithmic, ~3 dB (1 / sqrt 2) a step, level 0 = silent.
#pragma once

#include <cstdint>

namespace ay8910 {

// Output level of each amplitude 0-15 (15 = kMaxLevel).
constexpr int kMaxLevel = 4096;
extern const uint16_t kLevels[16];

class Chip {
public:
    Chip() { reset(); }
    void reset();

    // The bus as the CPU sees it: an address write (BDIR + BC1), a data
    // write (BDIR), a data read (BC1).
    void    write_address(uint8_t v);
    void    write_data(uint8_t v);
    uint8_t read_data() const;

    // Direct register access (r 0-15); values are masked as the chip
    // stores them.
    void    write(int r, uint8_t v);
    uint8_t reg(int r) const { return regs_[r & 15]; }

    // The I/O ports (0 = A, 1 = B): what the pins show when the port is an
    // output (R7 bit 6 / 7), else 0xFF (pulled up); and the value read
    // when it is an input.
    uint8_t port_out(int port) const;
    void    set_port_in(int port, uint8_t v) { port_in_[port & 1] = v; }

    // Runs `clocks` input clocks and adds each channel's output level x
    // clocks to acc[0..2] (for averaging over an output sample).
    void run(uint32_t clocks, uint32_t* acc);

    // Brings the counters that can't be heard now (and so are carried
    // along lazily) up to date; run() and write() do it as needed. Tests
    // call it before looking at the state below.
    void sync();

    // A channel's output level now (0..kMaxLevel), for tests.
    int  level(int ch) const { return level_[ch]; }
    // Tests: the tone flip-flop, the noise register, the envelope volume.
    int  tone_output(int ch) const { return tone_out_[ch]; }
    uint32_t noise_register() const { return lfsr_; }
    int  envelope_volume() const { return env_step_ ^ env_attack_; }

private:
    void update_levels();
    bool step_tone(int c, uint32_t period, uint32_t ticks);
    bool step_noise(uint32_t period, uint32_t ticks);
    bool step_env(uint32_t period, uint32_t ticks);
    uint32_t noise_period() const { return regs_[6] ? regs_[6] : 1; }
    uint32_t env_period() const { return 2u * (regs_[11] | uint32_t(regs_[12]) << 8); }  // 0 = every tick
    uint32_t tone_period(int ch) const
    {
        const uint32_t p = regs_[2 * ch] | uint32_t(regs_[2 * ch + 1]) << 8;
        return p ? p : 1;
    }

    uint8_t  regs_[16] = {};
    uint8_t  latch_ = 0;
    bool     active_ = true;
    uint8_t  port_in_[2] = {0xff, 0xff};

    uint32_t tone_cnt_[3] = {};
    uint8_t  tone_out_[3] = {};
    uint32_t noise_cnt_ = 0;
    uint8_t  noise_pre_ = 0;        // the noise clock's divide-by-two
    uint32_t lfsr_ = 1;
    uint32_t env_cnt_ = 0;
    int8_t   env_step_ = 15;        // counts down 15..0
    uint8_t  env_attack_ = 0;       // 15 = rising (the step is inverted)
    bool     env_hold_ = false, env_alt_ = false, env_holding_ = false;
    uint32_t sub_ = 0;              // input clocks into the current tick (0-7)
    int      level_[3] = {};
    uint32_t lazy_ = 0;             // ticks the counters that can't be heard still have to move
    uint8_t  tone_live_ = 0;        // bit c: channel c's tone can change its output
    bool     noise_live_ = false, env_live_ = false;
};

} // namespace ay8910
