// Donkey Kong sound board output stage and analog effects, written for
// this project (MIT).
//
// The board mixes four things through equal 47k resistors: the 8035's
// 8-bit DAC (music, the death tune, the roar, coin / spring / falling /
// prize effects - all from the sound program, see emu/machines/dkong.*)
// and three discrete circuits that the main CPU switches through latch 6H:
//   bit 0 walk:  a 555 tone (its control voltage wobbled by a CMOS
//                inverter oscillator) let through by a short RC pulse
//   bit 1 jump:  the same kind of 555 + wobble, its control voltage raised
//                while the bit is set, under a fast-dip / slow-recover
//                envelope - the "boing"
//   bit 2 stomp: a 4 kHz noise shift register divided by a counter, under
//                the same kind of envelope - Donkey Kong's thump
// These are small models of those circuits (an oscillator, an envelope,
// a filter), with times and pitches from the board's parts (facts read
// from the schematics as described in MAME's discrete sound driver). They
// sound close, not identical. The DAC path: P2 bit 7 of the 8035 lets the
// DAC through at full level or fades it out (10k x 10 uF), then a 2-pole
// low-pass (Sallen-Key, 1.9 kHz).
#pragma once

#include <cstdint>

namespace dkong_sound {

class Board {
public:
    void init(uint32_t rate);
    void reset();

    // Latch 6H's bits as the main CPU leaves them (call on every change):
    // bit 0 walk, 1 jump, 2 stomp.
    void write_latch(uint8_t bits);
    // 8035 P2 bit 7: true = the DAC at full level, false = fading out.
    void set_dac_full(bool full);

    // One output sample, from the DAC's average level over it (0-255).
    int16_t sample(float dac);

private:
    // A 555 astable whose control voltage sets the pitch; rendered as the
    // share of each output sample it spends high (no aliasing buzz).
    struct Vco555 {
        float phase = 0;                    // in periods
        float step[64] = {};                // per sample, by control voltage (0-4.6 V)
        float duty[64] = {};                // share of the period spent high
        void  init(float ra, float rb, float c, float rate);
        float run(float cv);                // 0..1, the high share of this sample
    };
    // The 4049 RC oscillator wobbling a 555's control voltage: a square wave.
    struct Wobble {
        uint32_t phase = 0, step = 0;
        bool tick() { phase += step; return phase < 0x80000000u; }
    };
    // A trigger pulse: the coupling capacitor charges while the latch bit
    // is set; the transistor after it conducts until it has (~28 ms).
    struct Pulse {
        float v = 0, charge = 0, release = 0, threshold = 0;
        bool  run(bool bit);
    };

    float lp(float tau) const;              // 1 - exp(-1 / (tau * rate))

    float   rate_ = 24000;
    uint8_t bits_ = 0;
    bool    dac_full_ = true;

    // DAC: fade envelope, Sallen-Key low-pass (biquad).
    float dac_env_ = 1, dac_fade_ = 0;
    float sk_b0_ = 0, sk_b1_ = 0, sk_b2_ = 0, sk_a1_ = 0, sk_a2_ = 0;
    float sk_x1_ = 0, sk_x2_ = 0, sk_y1_ = 0, sk_y2_ = 0;

    // Stomp: noise -> counter -> square; envelope; integrator.
    uint32_t lfsr_ = 0, noise_phase_ = 0, noise_step_ = 0;
    uint8_t  counter_ = 0;
    bool     noise_out_ = false;
    Pulse    stomp_pulse_;
    float    stomp_env_ = 5, stomp_fast_ = 0, stomp_slow_ = 0;
    float    stomp_y_ = 0, stomp_up_ = 0, stomp_down_ = 0;

    // Jump: wobble -> control voltage -> 555; envelope; integrator.
    Wobble jump_wob_;
    Vco555 jump_vco_;
    Pulse  jump_pulse_;
    float  jump_cv_ = 0, jump_cv_k_ = 0;
    float  jump_env_ = 5, jump_fast_ = 0, jump_slow_ = 0;
    float  jump_y_ = 0, jump_up_ = 0, jump_down_ = 0;

    // Walk: wobble -> control voltage -> 555, gated by the trigger's pulse.
    Wobble walk_wob_;
    Vco555 walk_vco_;
    float  walk_cv_ = 0, walk_cv_k_ = 0;
    float  walk_cap_ = 0, walk_on_ = 0, walk_on_gated_ = 0, walk_off_ = 0;

    // The amplifier's coupling capacitors: DC removed.
    float dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace dkong_sound
