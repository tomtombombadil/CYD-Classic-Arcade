// Zaxxon sound board models, written for this project (MIT). Every Zaxxon
// sound is a discrete circuit (555 timers, two noise generators, op-amp
// filters and analog switches) that an output of the main board's 8255
// switches; the outputs are active low (0 = on / trigger). These are small
// models of each sound - no samples.
//
//   port A  bits 0-1 the engine's volume, 2 engine (fortress), 3 engine
//           (space), 4 homing missile, 5 base missile (one shot), 6 force
//           field laser, 7 battleship (Zaxxon robot)
//   port B  bit 4 small explosion, 5 big explosion (the player's ship),
//           7 cannon (the player's shot); the others aren't connected
//   port C  bit 0 enemy shot, 2 alarm 2 (target lock), 3 alarm 3 (low
//           fuel); the others aren't connected
//
// Which output starts which sound, which hold (looped while low) and which
// are one shots are facts (MAME's driver); the circuits' part values
// weren't to be had, so each sound's pitch, time and filter here is our own
// design after how the game sounds - see zaxxon_sound.cpp.
#pragma once

#include <cstdint>

namespace zaxxon_sound {

enum Sound : uint8_t {
    kEngineA, kEngineB, kHoming, kBaseMissile, kLaser, kBattleship,
    kSmallExp, kBigExp, kCannon, kShot, kAlarm2, kAlarm3, kSoundCount,
};

class Board {
public:
    void init(uint32_t rate);
    void reset();

    // The 8255's port outputs (as written; active low).
    void write_a(uint8_t v);
    void write_b(uint8_t v);
    void write_c(uint8_t v);

    // Writes `n` samples at the output rate (overwrites `out`).
    void render(int16_t* out, int n);

    // State (tests): a held sound's input is on, or a one shot is sounding.
    bool active(Sound s) const;
    uint8_t engine_volume() const { return uint8_t(a_ & 3); }
    uint32_t triggers(Sound s) const { return triggers_[s]; }

private:
    float lp(float fc) const;           // one-pole low-pass coefficient
    float decay(float tau) const;       // per-sample factor for time constant tau
    void  trigger(Sound s);

    float    rate_ = 24000;
    uint8_t  a_ = 0xff, b_ = 0xff, c_ = 0xff;
    uint32_t triggers_[kSoundCount] = {};

    // Noise: two 17-bit shift registers (the board's two noise generators),
    // clocked at ~15 and ~9 kHz.
    uint32_t lfsr1_ = 1, lfsr2_ = 1;
    uint32_t n1_phase_ = 0, n1_step_ = 0, n2_phase_ = 0, n2_step_ = 0;
    float    noise1_ = 0, noise2_ = 0;

    // Engines: noise 1 through a two-pole low-pass (fortress rumble) and
    // noise 2 through a band-pass (the space hiss), level by bits 0-1.
    float    ea_lp1_ = 0, ea_lp2_ = 0, ea_c_ = 0;
    float    eb_bp_ = 0, eb_bpv_ = 0, eb_f_ = 0, eb_q_ = 0;
    float    eng_level_ = 0, eng_a_amp_ = 0, eng_b_amp_ = 0, eng_attack_ = 0;

    // Homing missile: a square VCO swept by a slow triangle.
    float    hm_phase_ = 0, hm_lfo_ = 0, hm_amp_ = 0;
    // Base missile: a falling square VCO with noise, an RC decay.
    float    bm_t_ = 9, bm_phase_ = 0, bm_amp_ = 0;
    // Force field: a buzzing square, its pitch switched by a fast square.
    float    lz_phase_ = 0, lz_mod_ = 0, lz_amp_ = 0;
    // Battleship: a low square, its level throbbing.
    float    bs_phase_ = 0, bs_lfo_ = 0, bs_amp_ = 0;
    // Explosions: noise through a low-pass, RC envelopes.
    float    sx_env_ = 0, sx_lp_ = 0, sx_c_ = 0, sx_decay_ = 0;
    float    bx_env_ = 0, bx_lp_ = 0, bx_lp2_ = 0, bx_c_ = 0, bx_decay_ = 0;
    // Cannon and enemy shot: falling square VCOs, RC envelopes.
    float    cn_t_ = 9, cn_phase_ = 0, cn_env_ = 0;
    float    sh_t_ = 9, sh_phase_ = 0, sh_env_ = 0;
    // Alarms: gated tones.
    float    a2_t_ = 9, a2_phase_ = 0;
    float    a3_t_ = 9, a3_phase_ = 0;

    // The amplifier's coupling capacitor (DC blocker).
    float    dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace zaxxon_sound
