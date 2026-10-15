// Texas Instruments SN76489 sound chip, written for this project (MIT).
// Three square-wave tone channels and one noise channel, each with a 4-bit
// attenuator (2 dB steps, 15 = off). The clock input is divided by 16: one
// "tick" steps every counter. A tone channel flips its output every N
// ticks (N = its 10-bit period; 0 counts as 1024), so it sounds at
// clock / (32 N). The noise channel shifts a 15-bit register every 32, 64
// or 128 ticks, or at half tone 3's rate; white noise feeds back bit 0 XOR
// bit 1 (period 32767), "periodic" noise bit 0 alone (period 15). Writing
// the noise control resets the register to 0x4000. Output = register bit 0.
// Facts from TI's data sheet and MAME's notes on the chip (read for facts
// only).
//
// Register writes: 1 cc t dddd latches channel cc (t = 1: attenuation) and
// sets the low four bits; 0 x dddddd then sets a latched tone's six high
// period bits (or, for attenuation / noise, the low four bits again).
//
// Each output sample is the average of the chip's output over its time
// (the outputs are counted tick by tick), so tones far above the output
// rate fade instead of folding back as noise.
#pragma once

#include <cstdint>

namespace sn76489 {

class Chip {
public:
    // `clock`: the chip's clock input (Hz); `rate`: output samples a second;
    // `level`: one channel's output at full volume (attenuation 0).
    void init(uint32_t clock, uint32_t rate, int level);
    // Every channel silent (attenuation 15); periods 1024; noise register
    // 0x4000. (A real chip powers up with anything: MAME's notes say loud.)
    void reset();
    void write(uint8_t v);

    // Adds `n` samples to `acc`: each channel adds its level while its
    // output is 1 (0 otherwise), averaged over the sample.
    void mix(int32_t* acc, int n);

    // For tests.
    uint16_t period(int ch) const { return period_[ch]; }
    uint8_t  attenuation(int ch) const { return atten_[ch]; }
    uint16_t noise_register() const { return lfsr_; }
    uint8_t  noise_control() const { return noise_ctl_; }
    int      ticks_per_second() const { return int(clock_ / 16); }

private:
    void set_noise_period();
    void shift_noise();

    uint32_t clock_ = 4000000;
    uint32_t step_ = 0;         // ticks a sample, 16.16
    uint32_t frac_ = 0;
    int      level_[16] = {};   // per attenuation
    uint8_t  latched_ = 0;      // register 0-7 (channel * 2 + attenuation)
    uint16_t period_[4] = {};   // channel 3: ticks between noise shifts
    int      count_[4] = {};    // ticks to the next flip / shift
    uint8_t  out_[4] = {};
    uint8_t  atten_[4] = {};
    uint8_t  noise_ctl_ = 0;    // bit 2 white, bits 0-1 rate
    uint16_t lfsr_ = 0x4000;
};

} // namespace sn76489
