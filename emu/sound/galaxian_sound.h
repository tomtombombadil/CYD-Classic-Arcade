// Galaxian's sound board, a model of its circuits written for this project
// (MIT). No samples: every part is computed from the schematic's values
// (as listed in MAME's discrete model - read for facts only).
//
// - Tone (the tunes, the "dive" whistles): an 8-bit counter preloaded with
//   the pitch latch (7800) divides the 1.536 MHz sound clock by
//   256 - pitch and clocks a 4-bit counter; its outputs QA (33k), QC (22k,
//   and 10k while VOL1 is on) and QD (15k while VOL2 is on) are mixed
//   with the background (5.1k) into one node. Pitch FF = 1.536 MHz,
//   inaudible: the game's "off".
// - Background (the swarm): three 555s (FS1-3: 100k + 470k / 330k / 220k,
//   0.01 uF), each held in reset while its bit is off, share a control
//   voltage from a fourth, slow 555: a 1 uF capacitor charged by a current
//   set through 100k by a 4-bit resistor DAC (6004-6007: 1M / 470k / 220k
//   / 100k, 15k to 4.4 V, 330k to ground) and emptied at 2/3 Vcc - a
//   sawtooth, scaled by the op-amp stage (x 1.43, -1.06 V, clipped to 0-5
//   V: 1.3-3.7 V). Mixed through 10k each.
// - HIT (explosions): the noise (a 17-bit shift register at 122.88 kHz,
//   sampled by a flip-flop at 7.92 kHz) through an RC that holds it 0.38 s
//   after HIT goes off (172k, 2.2 uF), then a band-pass (150k / 22k /
//   470k, 0.01 uF x 2: 168 Hz, Q 2.5, gain 1.6).
// - FIRE (the player's shot): a 555 (10k + 22k, 0.01 uF) whose control
//   voltage is the noise (10k) plus an RC (2.2k, 47 uF) that falls to 0 V
//   while FIRE is on - the pitch rises from ~1.3 kHz towards 6.5 kHz - gated
//   by a 0.1 s RC (100k, 1 uF) that lets it ring out after FIRE ends.
// - Mixer: tone + background node 5.1k, HIT 1.32k, FIRE 2.2k into 10k of
//   feedback, AC-coupled out.
//
// Unsure: the slow 555's current source (taken as (DAC - 0.7 V) / 100k,
// giving a 16-21 Hz sawtooth), the DAC's high level (4 V), the noise
// register's taps (taken as the star generator's), the output coupling
// (a 20 Hz high-pass here) and the overall level (set for a small speaker).
#pragma once

#include <cstdint>

namespace galaxian_sound {

// The control bits written at 6800-6807 (bit 0 of the data).
enum Control : int {
    kFs1 = 0, kFs2 = 1, kFs3 = 2, kHit = 3, kFire = 5, kVol1 = 6, kVol2 = 7,
};

// A 555 astable (charging through Ra + Rb, discharging through Rb) whose
// thresholds follow a control voltage: cv up, cv / 2 down.
struct Astable {
    float tau_high = 0, tau_low = 0;        // seconds
    float k_high = 0, k_low = 0;            // exp(-dt / tau): one output sample
    float v = 0;                            // the capacitor
    bool  high = true;
    void  init(float ra, float rb, float c, float dt);
    void  reset() { v = 0; high = true; }
    // Runs dt seconds at control voltage cv; returns the share of it the
    // output was high (0-1).
    float run(float cv, float dt);
};

class Sound {
public:
    void init(uint32_t audio_rate);
    void reset();

    void pitch(uint8_t v) { pitch_ = v; }
    // 6004-6007: the background DAC's bits 0-3.
    void lfo(int bit, bool on);
    // 6800-6807 (Control).
    void control(int reg, bool on);

    // Writes n samples.
    void render(int16_t* out, int n);

    // Tests: the slow 555's frequency (Hz) for the DAC code, and the
    // control voltage at the bottom / top of its sawtooth.
    static float lfo_hz(int code);
    static float fs_cv(float cap);

private:
    float noise_mean(float dt);

    uint32_t rate_ = 24000;
    float    dt_ = 0;
    uint8_t  pitch_ = 0xff;
    uint8_t  lfo_ = 0;
    uint8_t  ctrl_ = 0;

    uint32_t tone_t_ = 0;           // the 4-bit counter, in counts (0-16, 16.16 fixed point)
    float    lfo_v_ = 5.0f / 3;     // the slow 555's capacitor
    float    lfo_rate_ = 0;         // its volts a second
    Astable  fs_[3];
    Astable  fire_osc_;
    float    fire_rc_ = 4.0f;       // the 2.2k / 47 uF RC (4 V at rest)
    float    k_fire_rc_ = 0;
    float    fire_env_ = 0, k_fire_env_ = 0;
    float    hit_env_ = 0, k_hit_env_ = 0;

    uint32_t lfsr_ = 0;
    float    noise_clock_ = 0;      // flip-flop ticks still to come in this sample (fraction)
    float    lfsr_due_ = 0;         // shift register steps owed to the next tick
    uint8_t  noise_ = 0;            // the flip-flop

    // Band-pass (RBJ, peak gain 1) and the output high-pass.
    float    bp_b0_ = 0, bp_a1_ = 0, bp_a2_ = 0;
    float    bp_x1_ = 0, bp_x2_ = 0, bp_y1_ = 0, bp_y2_ = 0;
    float    dc_r_ = 0, dc_x_ = 0, dc_y_ = 0;
};

} // namespace galaxian_sound
