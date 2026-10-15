// Asteroids sound board models, written for this project (MIT). Every
// Asteroids sound is an analog circuit the program switches on and off;
// these are small models of each, with pitches, times and levels from the
// board's parts (facts read from MAME's discrete description and the
// schematics). Close, not identical - and no samples.
//
//   3600  explosion: bits 2-5 volume, bits 6-7 pitch (noise sampled at
//         12 kHz / 12, 6, 3 or 5), low-passed
//   3A00  thump: bit 4 on, bits 0-3 the 555 VCO's control (a resistor DAC
//         into a current source: higher = lower pitch)
//   3C00-3C07 (bit 7 of the data): 0 saucer, 1 saucer fire, 2 saucer size
//         (1 = big: lower pitch, slower warble), 3 thrust, 4 ship fire,
//         5 extra life (3 kHz tone)
//   3E00  noise reset
#pragma once

#include <cstdint>

namespace asteroids_sound {

enum Latch : uint8_t {
    kSaucer = 0, kSaucerFire = 1, kSaucerBig = 2, kThrust = 3, kShipFire = 4, kLife = 5,
};

class Board {
public:
    void init(uint32_t rate);
    void reset();

    void explode(uint8_t v);            // 3600
    void thump(uint8_t v);              // 3A00
    void latch(int bit, bool on);       // 3C00-3C07
    void noise_reset();                 // 3E00

    // Writes `n` samples at the output rate (overwrites `out`).
    void render(int16_t* out, int n);

    // State (tests).
    bool on(int bit) const { return (latch_ >> bit) & 1; }
    float thump_hz() const { return thump_hz_; }
    uint16_t noise() const { return lfsr_; }

private:
    float lp(float fc) const;           // one-pole low-pass coefficient
    float decay(float tau) const;       // per-sample factor for time constant tau

    float    rate_ = 24000;
    uint8_t  latch_ = 0;

    // Noise: 16-bit shift register at 12 kHz, XNOR of bits 6 and 14 in.
    uint16_t lfsr_ = 0;
    uint32_t noise_phase_ = 0, noise_step_ = 0;
    int      noise_bit_ = 0;

    // Explosion: the noise held at the pitch clock, times the volume, RC.
    uint8_t  ex_vol_ = 0;
    uint32_t ex_phase_ = 0, ex_step_ = 0;
    float    ex_held_ = 0, ex_lp_ = 0, ex_c_ = 0;

    // Thump: 555 VCO, 50 % square, gated, through 3.3k / 0.1 uF.
    bool     th_on_ = false;
    float    thump_hz_ = 0;
    uint32_t th_phase_ = 0, th_step_ = 0;
    float    th_lp_ = 0, th_lp2_ = 0, th_c_ = 0;

    // Saucer: triangle VCO (750-1670 Hz small, 500-1420 big) swept by a
    // 8.25 / 5.75 Hz triangle.
    float    sc_lfo_ = 0, sc_lfo_dir_ = 1, sc_phase_ = 0;

    // Fire sounds: square VCOs whose pitch ramps down for 0.28 s while the
    // bit is held, amplitude decaying with an RC.
    struct Fire {
        float t = 0;                    // seconds since the bit came on
        float phase = 0;
        float amp = 0;
    };
    Fire     ship_, saucer_fire_;
    float    ship_decay_ = 0, saucer_decay_ = 0;

    // Thrust: noise through RC (2.2k / 1 uF), a band-pass at 89.5 Hz and
    // a 160 Hz low-pass.
    float    tr_lp_ = 0, tr_bp_ = 0, tr_bp_v_ = 0, tr_out_ = 0;
    float    tr_c1_ = 0, tr_f_ = 0, tr_q_ = 0, tr_c2_ = 0;

    // Life: 3 kHz square.
    uint32_t life_phase_ = 0, life_step_ = 0;

    // The output coupling capacitor (DC blocker).
    float    dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace asteroids_sound
