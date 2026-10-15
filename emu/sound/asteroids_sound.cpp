// Asteroids sound board models - see asteroids_sound.h.
//
// Part values are the board's; where a circuit is too involved to model
// exactly, the figure is what it produces, marked "~". Levels: the board's
// mixer makes the explosion and thrust ~20 times louder than the fire
// sounds (weights 1000 / 600 against ~50), but they sit below ~100 Hz,
// where a small speaker gives little - so each sound's level here is set
// for the CYD's speaker: explosion peaks ~18,000, thrust ~11,000, thump and
// fire ~9,000, saucer ~7,000, extra life ~5,000.
#include "asteroids_sound.h"

#include <cmath>

namespace asteroids_sound {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kNoiseHz = 12000.0f;        // 12.096 MHz / 1008 (~)

// Thump VCO control: the 4-bit resistor DAC (220k, 100k, 47k, 22k from
// 3.5 V CMOS outputs) with 6.8k to 4.3 V and 47k to ground sets the base of
// a PNP current source (22k emitter resistor from 5 V, Vbe 0.8 V) that
// charges 0.22 uF between 1/3 and 2/3 of 5 V.
float thump_frequency(int data)
{
    const float g[4] = {1 / 220e3f, 1 / 100e3f, 1 / 47e3f, 1 / 22e3f};
    float num = 4.3f / 6.8e3f, den = 1 / 6.8e3f + 1 / 47e3f;
    for (int b = 0; b < 4; ++b) {
        den += g[b];
        if (data >> b & 1) num += 3.5f * g[b];
    }
    const float v = num / den;
    const float i = (5.0f - v - 0.8f) / 22e3f;
    const float t = 0.22e-6f * (5.0f / 3.0f) / (i > 1e-6f ? i : 1e-6f);
    return 1.0f / t;
}

// Levels (int16 per unit of each circuit's swing).
constexpr float kThumpLevel = 7000, kSaucerLevel = 7000, kLifeLevel = 5000;
constexpr float kExplodeLevel = 30000, kThrustLevel = 16000, kFireLevel = 300;
// The fire sounds' amplitudes: 53 -> 7 (ship), 49.5 -> 7 (saucer).
constexpr float kShipFireTop = 46.0f, kSaucerFireTop = 42.5f, kFireFloor = 7.0f;

// A filter state that has died away becomes exactly 0, so idle filters
// can be skipped (and never reach slow denormal floats).
inline float settle(float v) { return (v > -1e-6f && v < 1e-6f) ? 0.0f : v; }

} // namespace

float Board::lp(float fc) const { return 1.0f - std::exp(-kTwoPi * fc / rate_); }
float Board::decay(float tau) const { return std::exp(-1.0f / (tau * rate_)); }

void Board::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    noise_step_ = uint32_t(kNoiseHz / rate_ * 4294967296.0f);
    life_step_ = uint32_t(3000.0f / rate_ * 4294967296.0f);
    ex_c_ = lp(1.0f / (kTwoPi * 3042.0f * 1e-6f));          // 3042 ohm / 1 uF: 52 Hz
    th_c_ = lp(1.0f / (kTwoPi * 3.3e3f * 0.1e-6f));         // 3.3k / 0.1 uF: 482 Hz
    ship_decay_ = decay(2700.0f * 3 * 1e-5f);               // 8.1k / 10 uF
    saucer_decay_ = decay(10000.0f * 3 * 1e-5f);            // 30k / 10 uF
    tr_c1_ = lp(1.0f / (kTwoPi * 2200.0f * 1e-6f));         // 2.2k / 1 uF: 72 Hz
    tr_f_ = 2.0f * std::sin(3.14159265f * 89.5f / rate_);   // band-pass 89.5 Hz, Q 7.6
    tr_q_ = 1.0f / 7.6f;
    tr_c2_ = lp(160.0f);
    dc_r_ = 1.0f - kTwoPi * 15.0f / rate_;
    reset();
}

void Board::reset()
{
    latch_ = 0;
    lfsr_ = 0;
    noise_phase_ = 0;
    noise_bit_ = 0;
    ex_vol_ = 0;
    ex_phase_ = 0;
    ex_step_ = uint32_t(kNoiseHz / 12 / rate_ * 4294967296.0f);
    ex_held_ = ex_lp_ = 0;
    th_on_ = false;
    thump_hz_ = thump_frequency(0);
    th_step_ = uint32_t(thump_hz_ / rate_ * 4294967296.0f);
    th_phase_ = 0;
    th_lp_ = th_lp2_ = 0;
    sc_lfo_ = 0;
    sc_lfo_dir_ = 1;
    sc_phase_ = 0;
    ship_ = Fire{};
    saucer_fire_ = Fire{};
    tr_lp_ = tr_bp_ = tr_bp_v_ = tr_out_ = 0;
    life_phase_ = 0;
    dc_x_ = dc_y_ = 0;
}

void Board::explode(uint8_t v)
{
    ex_vol_ = (v >> 2) & 15;
    static const uint8_t kDiv[4] = {12, 6, 3, 5};
    ex_step_ = uint32_t(kNoiseHz / kDiv[v >> 6] / rate_ * 4294967296.0f);
}

void Board::thump(uint8_t v)
{
    th_on_ = (v & 0x10) != 0;
    thump_hz_ = thump_frequency(v & 15);
    th_step_ = uint32_t(thump_hz_ / rate_ * 4294967296.0f);
}

void Board::latch(int bit, bool on)
{
    const uint8_t m = uint8_t(1u << bit);
    const bool was = (latch_ & m) != 0;
    latch_ = uint8_t(on ? latch_ | m : latch_ & ~m);
    if (on && !was) {
        // The fire sounds' ramps and envelopes start over with the bit.
        if (bit == kShipFire) ship_ = Fire{0, 0, kShipFireTop};
        if (bit == kSaucerFire) saucer_fire_ = Fire{0, 0, kSaucerFireTop};
    }
}

void Board::noise_reset()
{
    lfsr_ = 0;
}

void Board::render(int16_t* out, int n)
{
    const float dt = 1.0f / rate_;
    for (int i = 0; i < n; ++i) {
        // Noise source.
        const uint32_t np = noise_phase_;
        noise_phase_ += noise_step_;
        if (noise_phase_ < np) {
            const int fb = !(((lfsr_ >> 6) ^ (lfsr_ >> 14)) & 1);
            lfsr_ = uint16_t((lfsr_ << 1) | fb);
            noise_bit_ = fb;
        }
        float mix = 0;

        // Explosion: the noise bit latched on the pitch clock's edge.
        const uint32_t ep = ex_phase_;
        ex_phase_ += ex_step_;
        if (ex_phase_ < ep) ex_held_ = float(noise_bit_);
        if (ex_vol_ || ex_lp_ != 0) {
            ex_lp_ = settle(ex_lp_ + ex_c_ * (ex_held_ * float(ex_vol_) * (1.0f / 15.0f) - ex_lp_));
            mix += ex_lp_ * kExplodeLevel;
        }

        // Thump.
        th_phase_ += th_step_;
        if (th_on_ || th_lp_ != 0 || th_lp2_ != 0) {
            const float th_in = th_on_ ? (th_phase_ < 0x80000000u ? 1.0f : -1.0f) : 0.0f;
            th_lp_ = settle(th_lp_ + th_c_ * (th_in - th_lp_));
            th_lp2_ = settle(th_lp2_ + th_c_ * (th_lp_ - th_lp2_));
            mix += th_lp2_ * kThumpLevel;
        }

        // Saucer.
        if (latch_ & (1u << kSaucer)) {
            const bool big = latch_ & (1u << kSaucerBig);
            sc_lfo_ += sc_lfo_dir_ * 2.0f * (big ? 5.75f : 8.25f) * dt;
            if (sc_lfo_ > 1) { sc_lfo_ = 2 - sc_lfo_; sc_lfo_dir_ = -1; }
            if (sc_lfo_ < 0) { sc_lfo_ = -sc_lfo_; sc_lfo_dir_ = 1; }
            const float hz = 750.0f + 920.0f * sc_lfo_ - (big ? 250.0f : 0.0f);
            sc_phase_ += hz * dt;
            if (sc_phase_ >= 1) sc_phase_ -= 1;
            const float tri = sc_phase_ < 0.5f ? 4 * sc_phase_ - 1 : 3 - 4 * sc_phase_;
            mix += tri * kSaucerLevel;
        }

        // Ship fire: 820 -> 110 Hz over 0.28 s; saucer fire 830 -> 630 Hz.
        // Duty 67 % + 4500 / f (capped).
        Fire* fires[2] = {&ship_, &saucer_fire_};
        for (int k = 0; k < 2; ++k) {
            if (!(latch_ & (1u << (k ? kSaucerFire : kShipFire)))) continue;
            Fire& f = *fires[k];
            const float top = k ? 830.0f : 820.0f, bottom = k ? 630.0f : 110.0f;
            float hz = top - (top - bottom) / 0.28f * f.t;
            if (hz < bottom) hz = bottom;
            f.t += dt;
            f.phase += hz * dt;
            if (f.phase >= 1) f.phase -= 1;
            float duty = 0.67f + 45.0f / hz;
            if (duty > 0.98f) duty = 0.98f;
            const float amp = f.amp + kFireFloor;
            f.amp *= k ? saucer_decay_ : ship_decay_;
            mix += (f.phase < duty ? 0.5f : -0.5f) * amp * kFireLevel;
        }

        // Thrust: gated filtered noise.
        tr_lp_ += tr_c1_ * ((noise_bit_ ? 1.0f : -1.0f) - tr_lp_);
        const bool thrust = latch_ & (1u << kThrust);
        if (thrust || tr_bp_ != 0 || tr_bp_v_ != 0 || tr_out_ != 0) {
            const float tin = thrust ? tr_lp_ : 0.0f;
            // Band-pass (state variable).
            const float hp = tin - tr_bp_v_ - tr_q_ * tr_bp_;
            tr_bp_ = settle(tr_bp_ + tr_f_ * hp);
            tr_bp_v_ = settle(tr_bp_v_ + tr_f_ * tr_bp_);
            tr_out_ = settle(tr_out_ + tr_c2_ * (tr_bp_ - tr_out_));
            mix += tr_out_ * kThrustLevel;
        }

        // Extra life: 3 kHz.
        if (latch_ & (1u << kLife)) {
            life_phase_ += life_step_;
            mix += (life_phase_ < 0x80000000u ? 1.0f : -1.0f) * kLifeLevel;
        }

        // Coupling capacitor, level.
        const float x = mix;
        dc_y_ = settle(x - dc_x_ + dc_r_ * dc_y_);
        dc_x_ = x;
        float y = dc_y_;
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        out[i] = int16_t(y);
    }
}

} // namespace asteroids_sound
