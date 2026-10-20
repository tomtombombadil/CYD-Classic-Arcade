// Space Invaders sound board models, written for this project (MIT).
// The real board makes every sound with analog circuits (a noise shift
// register, 555 timers, op-amp oscillators and envelopes, an SN76477 for the
// UFO); the program only switches them with bits on ports 3 and 5. These
// are small models of each circuit - an oscillator or noise, a filter, an
// envelope - with pitches and times taken from the board's parts (facts
// read from MAME's discrete description and the schematics), cheap enough
// to run at 24 kHz on the ESP32. They sound close, not identical.
//
// Port 3: bit 0 UFO (repeats while set), 1 shot, 2 player hit, 3 invader
// hit, 4 extra ship, 5 sound on (amplifier). Port 5: bits 0-3 the four
// fleet notes, 4 UFO hit, 5 screen flip (cocktail only).
#pragma once

#include <cstdint>

namespace invaders_sound {

// The board's sounds, numbered like MAME's "invaders" sample set (n.wav).
enum Effect : uint8_t {
    kUfo,           // port 3 bit 0, loops while set
    kShot,          // port 3 bit 1
    kBaseHit,       // port 3 bit 2: the player's base explodes
    kInvaderHit,    // port 3 bit 3
    kFleet1,        // port 5 bit 0 ... the four march notes, high to low
    kFleet2,
    kFleet3,
    kFleet4,        // port 5 bit 3
    kUfoHit,        // port 5 bit 4
    kExtraShip,     // port 3 bit 4
    kEffectCount
};

class Synth {
public:
    void init(uint32_t rate);
    void reset();

    // The latched sound port values (call on every write; edges matter).
    void write_port3(uint8_t v);
    void write_port5(uint8_t v);

    // Effects (bit 1 << Effect) to leave silent here because a sample
    // plays them instead.
    void set_skip(uint16_t effects) { skip_ = effects; }

    // Writes `n` samples at the output rate (overwrites `out`).
    void render(int16_t* out, int n);

private:
    struct Square {         // phase-accumulator square wave
        uint32_t phase = 0, step = 0;
        void set(float hz, float rate) { step = uint32_t(hz / rate * 4294967296.0f); }
        bool tick() { phase += step; return phase < 0x80000000u; }
        bool tick_duty(uint32_t high) { phase += step; return phase < high; }
    };

    float lp(float fc) const;           // one-pole low-pass coefficient for cutoff fc
    float decay(float tau) const;       // per-sample multiplier for time constant tau
    float noise_step();

    float    rate_ = 24000;
    uint8_t  p3_ = 0, p5_ = 0;
    uint16_t skip_ = 0;

    // Noise: 17-bit shift register clocked at 7515 Hz (taps 4 and 16, output bit 12).
    uint32_t lfsr_ = 0x1ffff, noise_phase_ = 0, noise_step_ = 0;

    // UFO: SN76477 VCO swept by its slow triangle oscillator (SLF).
    float ufo_slf_ = 0.33f, ufo_vco_ = 0.33f, ufo_gain_ = 0;
    bool  ufo_slf_up_ = true, ufo_vco_up_ = true;
    float ufo_slf_rise_ = 0, ufo_slf_fall_ = 0, ufo_vco_rate_ = 0, ufo_attack_ = 0;

    // Shot: a narrow pulse in repeated falling sweeps, fading out.
    Square shot_osc_;
    float  shot_amp_ = 0, shot_noise_ = 0, shot_t_ = 1;   // shot_t_: seconds since the bit rose
    float  shot_attack_ = 0, shot_release_ = 0, shot_noise_lp_ = 0;

    // Player hit: filtered noise with a long envelope.
    float bh_amp_ = 0, bh_lp1_ = 0, bh_lp2_ = 0;
    int   bh_hold_ = 0;
    float bh_c1_ = 0, bh_c2_ = 0, bh_attack_ = 0, bh_release_ = 0;

    // Invader hit: a square warbled by a fast triangle, short envelope.
    Square ih_osc_;
    float  ih_amp_ = 0, ih_lfo_ = 0, ih_lfo_dir_ = 1;
    int    ih_hold_ = 0;
    float  ih_attack_ = 0, ih_release_ = 0, ih_lfo_step_ = 0;

    // UFO hit: a square warbled by a slow triangle; envelope follows the bit.
    Square uh_osc_;
    float  uh_amp_ = 0, uh_lfo_ = 0, uh_lfo_dir_ = 1;
    float  uh_attack_ = 0, uh_release_ = 0, uh_lfo_step_ = 0;

    // Fleet: a 555 whose timing resistor the four bits pick, two low-passes.
    uint32_t fl_phase_ = 0, fl_step_ = 0, fl_high_ = 0;   // high_: duty as a phase threshold
    float    fl_lp1_ = 0, fl_lp2_ = 0, fl_c1_ = 0, fl_c2_ = 0;

    // Extra ship: 480 Hz gated by a 7.4 Hz 555 (reset by the bit).
    Square   xs_tone_;
    uint32_t xs_gate_phase_ = 0, xs_gate_step_ = 0, xs_gate_high_ = 0;
    float    xs_amp_ = 0, xs_attack_ = 0;

    // DC blocker on the mix (the board's output coupling capacitor).
    float dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace invaders_sound
