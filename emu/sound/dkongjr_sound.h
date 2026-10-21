// Donkey Kong Jr. sound board output stage and analog effects, written for
// this project (MIT).
//
// The board mixes five things through resistors (R25 47k DAC, R3 100k
// walk, R4 47k jump, R6 20k climb, R5 150k fall): the 8035's 8-bit DAC
// (the music and most effects - from the sound program, so exact, see
// emu/machines/dkong.*) and four discrete circuits the main CPU switches
// through latches 6H and 5H. Our own small models of them, from the
// board's part values (facts read from the schematics as described in
// MAME's discrete sound driver, its breadboard measurements of the
// 74LS624 VCOs included):
//   walk (6H bit 0, bit 7 picks the variant): a 74LS123 one-shot (47k,
//         4.7 uF: 56 ms) fired by a 1 -> 0 write opens a NAND to a mix
//         (XOR) of a 3.1 kHz VCO and a bit of a 14-bit counter, which a
//         second VCO clocks at 14.45 or 59.4 kHz depending on a higher
//         counter bit - a warbling chirp
//   jump (6H bit 1): a one-shot (47k, 22 uF: 0.26 s) raises a VCO's
//         control voltage through a 47k / 10k mixer smoothed by 47 uF
//         (the counter's bit 11 wobbles it), the tone (313-1378 Hz) under
//         an RC envelope (10 uF through 10k + 100k)
//   climb (6H bit 2): a 16-bit noise register clocked at 710 Hz under the
//         same kind of envelope (10 uF, 4.7k + 100k), one-shot 0.26 s,
//         then 1.3 kHz low-pass
//   fall (5H bit 1): a 570 Hz VCO tone while the bit is set
// They sound close, not identical. The DAC path: P2 bit 7 of the 8035
// clear fades the DAC out (10k x 10 uF), set = full level (as on Donkey
// Kong; the other way the intro tune and the play music were silent while
// the program holds the bit set); then a 2-pole
// low-pass (Sallen-Key, 1.9 kHz). The mixer's 10 nF (1.7 kHz) and the
// amplifier's 1k / 4.7 uF coupling (34 Hz) follow.
#pragma once

#include <cstdint>

namespace dkongjr_sound {

class Board {
public:
    void init(uint32_t rate);
    void reset();

    // Latch 6H as the main CPU leaves it (call on every change): bit 0
    // walk, 1 jump, 2 climb, 7 the walk's variant.
    void write_latch(uint8_t bits);
    // Latch 5H bit 1: the falling sound.
    void set_fall(bool on) { fall_ = on; }
    // 8035 P2 bit 7: true = the DAC at full level, false = fading out.
    void set_dac_full(bool full);

    // One output sample, from the DAC's average level over it (0-255).
    int16_t sample(float dac);

private:
    // A square-wave VCO rendered as the share of each sample it is high.
    struct Square {
        float phase = 0;                    // 0..1, high in the first half
        float run(float step);              // step = frequency / rate
    };
    // The RC envelope after each one-shot (MAME's "RCDISC_MODULATED"
    // circuit, our own solution of it): an open-collector inverter drives
    // a series capacitor into a resistor to ground with a clamp diode; a
    // transistor shorts the output while the tone / noise is high.
    struct Envelope {
        float vc = 0;                       // the capacitor's voltage
        float k_on = 0, k_on_clamp = 0;     // per-sample RC factors, one-shot on
        float k_off = 0, k_off_clamp = 0;   //   and off
        float gain_on = 0, gain_off = 0;    // R4 / (Rs + R4)
        float run(bool on);                 // the voltage across R4 (clamped)
    };
    struct OneShot {
        uint32_t left = 0, width = 0;       // samples
        bool run() { if (left) { --left; return true; } return false; }
    };

    float lp(float tau) const;              // 1 - exp(-1 / (tau * rate))
    float counter_share(uint32_t c0, uint32_t c1, int bit) const;

    float   rate_ = 24000;
    uint8_t bits_ = 0;
    bool    fall_ = false;
    bool    dac_full_ = true;

    // DAC: fade, Sallen-Key low-pass (biquad).
    float dac_env_ = 1, dac_fade_k_ = 0;
    float sk_b0_ = 0, sk_b1_ = 0, sk_b2_ = 0, sk_a1_ = 0, sk_a2_ = 0;
    float sk_x1_ = 0, sk_x2_ = 0, sk_y1_ = 0, sk_y2_ = 0;

    // Counter 6L, clocked by the second walk VCO: 16.16 fixed point counts.
    uint32_t count_ = 0;
    uint32_t count_slow_ = 0, count_fast_ = 0;     // steps a sample

    // Walk.
    OneShot walk_shot_;
    Square  walk_vco_;
    float   walk_step_ = 0;

    // Jump.
    OneShot  jump_shot_;
    Envelope jump_env_;
    Square   jump_vco_;
    float    jump_cv_ = 0, jump_cv_k_ = 0;
    float    jump_hp_k_ = 0, jump_hp_x_ = 0, jump_hp_y_ = 0;

    // Climb.
    OneShot  climb_shot_;
    Envelope climb_env_;
    uint32_t lfsr_ = 0, noise_phase_ = 0, noise_step_ = 0;
    bool     noise_ = false;
    float    climb_hp_k_ = 0, climb_hp_x_ = 0, climb_hp_y_ = 0, climb_lp_k_ = 0, climb_lp_ = 0;

    // Fall.
    Square fall_vco_;
    float  fall_step_ = 0;

    // Mixer low-pass, the amplifier's coupling.
    float mix_lp_k_ = 0, mix_lp_ = 0;
    float dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace dkongjr_sound
