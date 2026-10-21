// Donkey Kong Jr. sound board models - see dkongjr_sound.h.
//
// Part numbers are the board's; voltages are on its 5 V scale and turned
// into samples at the end. Floats (the ESP32 has a single-precision FPU).
// A circuit at rest costs almost nothing.
#include "dkongjr_sound.h"

#include <cmath>

namespace dkongjr_sound {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kVcc = 5.0f;

// 74LS123 one-shots with a diode: t = 0.25 R C (1 + 700 / R).
constexpr float one_shot(float r, float c) { return 0.25f * r * c * (1.0f + 700.0f / r); }
constexpr float kWalkShot = one_shot(47e3f, 4.7e-6f);       // R8, C14: 56 ms
constexpr float kJumpShot = one_shot(47e3f, 22e-6f);        // R9, C15: 0.26 s
constexpr float kClimbShot = one_shot(47e3f, 22e-6f);       // R17, C27: 0.26 s

// The 74LS624 VCOs at the levels their inputs take (breadboard
// measurements): walk 5K pin 7 at 0.151 V (its one-shot on); 5K pin 10 at
// the 74LS04's low / high (0.135 / 4.15 V); fall 7P at 0.134 V.
constexpr float kWalkVcoHz = 3139.0f;
constexpr float kCountSlowHz = 14450.0f, kCountFastHz = 59400.0f;
constexpr float kFallVcoHz = 570.0f;
// Jump VCO 8L: control voltage -> Hz.
constexpr float kJumpV[9] = {0.151f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 3.0f, 4.0f, 4.14f};
constexpr float kJumpHz[9] = {313, 288, 275, 324, 370, 635, 965, 1325, 1378};

// Logic levels: 74LS04 / 7N outputs, the NANDs with 1k pull-ups.
constexpr float kInvLow = 0.151f, kInvHigh = 4.14f;
constexpr float kNandLow = 0.2f, kNandHigh = 4.9f;

// Jump control voltage: the one-shot's inverter through R13 47k and the
// counter's inverter through R12 10k, onto C24 47 uF.
constexpr float kR13 = 47e3f, kR12 = 10e3f;
constexpr float kJumpCvTau = 47e-6f * (kR13 * kR12 / (kR13 + kR12));

// Envelopes: R1 120 ohm pull-up, R2 in series with C, R4 100k to ground.
// Jump R27 10k, C28 10 uF; climb R24 4.7k, C29 10 uF.
constexpr float kEnvR1 = 120.0f, kEnvR4 = 100e3f;
constexpr float kJumpR2 = 10e3f, kJumpC = 10e-6f;
constexpr float kClimbR2 = 4.7e3f, kClimbC = 10e-6f;
constexpr float kDiode = 0.6f;

// Couplings into the mixer: jump R4 47k / C23 0.47 uF, climb R6 20k / C30
// 0.47 uF then R2 120 ohm / C25 1 uF.
constexpr float kJumpHpTau = 47e3f * 0.47e-6f, kClimbHpTau = 20e3f * 0.47e-6f;
constexpr float kClimbLpTau = 120.0f * 1e-6f;

// The noise register (3J, 4J) clocked by 7P at 710 Hz.
constexpr float kNoiseHz = 710.0f;

// DAC: 0-255 -> 0-5 V; the fade is R20 10k x C32 10 uF; Sallen-Key
// low-pass 5.6k / 5.6k, 22 nF / 10 nF: 1916 Hz, Q 0.74.
constexpr float kDacVolts = kVcc / 256.0f;
constexpr float kDacFadeTau = 10e3f * 10e-6f;
constexpr float kSkHz = 1916.0f, kSkQ = 0.74f;

// The mixer: R25 47k DAC, R3 100k walk, R4 47k jump, R6 20k climb, R5 150k
// fall, C155 10 nF on its node; the amplifier's 1k / 4.7 uF coupling.
constexpr float kGDac = 1 / 47e3f, kGWalk = 1 / 100e3f, kGJump = 1 / 47e3f, kGClimb = 1 / 20e3f,
                kGFall = 1 / 150e3f;
constexpr float kGSum = kGDac + kGWalk + kGJump + kGClimb + kGFall;
constexpr float kMixLpTau = 10e-9f / kGSum;
constexpr float kAmpHpHz = 1.0f / (kTwoPi * 1e3f * 4.7e-6f);

// Levels for a small speaker (sample units per volt at each mixer input,
// after its weight). By the resistors the climb noise would be ~2.3x the
// music and the walk / fall ~1/2 / 1/3 of it. Set so the effects peak
// ~8,000-14,000 (walk ~10,000, jump / climb ~14,000, fall ~8,000),
// under the intro tune (~16,500) and over the quiet play music (~6,000).
constexpr float kScaleDac = 26000.0f;
constexpr float kScaleWalk = 26000.0f;
constexpr float kScaleJump = 17000.0f;
constexpr float kScaleClimb = 7500.0f;
constexpr float kScaleFall = 30000.0f;
// Soft knee above this; the output never passes kKneeMax.
constexpr float kKnee = 16000.0f, kKneeMax = 22000.0f;

inline uint32_t samples(float seconds, float rate) { return uint32_t(seconds * rate + 0.5f); }

} // namespace

float Board::lp(float tau) const { return 1.0f - std::exp(-1.0f / (tau * rate_)); }

float Board::Square::run(float step)
{
    // The share of [phase, phase + step) spent in the first half of a period.
    const float p1 = phase + step;
    const float h0 = phase < 0.5f ? phase : 0.5f;
    const int whole = int(p1);
    const float frac = p1 - float(whole);
    const float h1 = float(whole) * 0.5f + (frac < 0.5f ? frac : 0.5f);
    phase = frac;
    return (h1 - h0) / step;
}

float Board::Envelope::run(bool on)
{
    const float u = on ? kVcc : 0.0f;
    const float vx = (u - vc) * (on ? gain_on : gain_off);
    if (vx < -kDiode) {                     // the diode holds the node
        vc += (u + kDiode - vc) * (on ? k_on_clamp : k_off_clamp);
        return -kDiode;
    }
    vc += (u - vc) * (on ? k_on : k_off);
    return vx;
}

// The share of counts in [c0, c1) (16.16 fixed point) with bit `bit` set.
float Board::counter_share(uint32_t c0, uint32_t c1, int bit) const
{
    const int b = bit + 16;
    const uint32_t half = 1u << b, mask = (half << 1) - 1;
    auto high = [&](uint32_t x) {
        const uint32_t r = x & mask;
        return float((x >> (b + 1)) << b) + float(r > half ? r - half : 0);
    };
    return (high(c1) - high(c0)) / float(c1 - c0);
}

void Board::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    walk_shot_.width = samples(kWalkShot, rate_);
    jump_shot_.width = samples(kJumpShot, rate_);
    climb_shot_.width = samples(kClimbShot, rate_);
    walk_step_ = kWalkVcoHz / rate_;
    fall_step_ = kFallVcoHz / rate_;
    count_slow_ = uint32_t(kCountSlowHz / rate_ * 65536.0f);
    count_fast_ = uint32_t(kCountFastHz / rate_ * 65536.0f);
    noise_step_ = uint32_t(double(kNoiseHz) * 4294967296.0 / rate_);
    jump_cv_k_ = lp(kJumpCvTau);

    struct EnvParts { Envelope* e; float r2, c; };
    const EnvParts envs[2] = {{&jump_env_, kJumpR2, kJumpC}, {&climb_env_, kClimbR2, kClimbC}};
    for (const EnvParts& p : envs) {
        const float rs_on = kEnvR1 + p.r2, rs_off = p.r2;
        p.e->gain_on = kEnvR4 / (rs_on + kEnvR4);
        p.e->gain_off = kEnvR4 / (rs_off + kEnvR4);
        p.e->k_on = lp((rs_on + kEnvR4) * p.c);
        p.e->k_off = lp((rs_off + kEnvR4) * p.c);
        p.e->k_on_clamp = lp(rs_on * p.c);
        p.e->k_off_clamp = lp(rs_off * p.c);
    }
    jump_hp_k_ = 1.0f - lp(kJumpHpTau);
    climb_hp_k_ = 1.0f - lp(kClimbHpTau);
    climb_lp_k_ = lp(kClimbLpTau);
    mix_lp_k_ = lp(kMixLpTau);
    dac_fade_k_ = 1.0f - lp(kDacFadeTau);

    // Sallen-Key low-pass as a biquad (bilinear transform).
    const float w = kTwoPi * kSkHz / rate_;
    const float alpha = std::sin(w) / (2 * kSkQ), cw = std::cos(w);
    const float a0 = 1 + alpha;
    sk_b0_ = (1 - cw) / 2 / a0;
    sk_b1_ = (1 - cw) / a0;
    sk_b2_ = sk_b0_;
    sk_a1_ = -2 * cw / a0;
    sk_a2_ = (1 - alpha) / a0;

    dc_r_ = 1.0f - kTwoPi * kAmpHpHz / rate_;
    reset();
}

void Board::reset()
{
    bits_ = 0;
    fall_ = false;
    dac_full_ = true;
    dac_env_ = 1;
    sk_x1_ = sk_x2_ = sk_y1_ = sk_y2_ = 0;
    count_ = 0;
    walk_shot_.left = jump_shot_.left = climb_shot_.left = 0;
    walk_vco_.phase = jump_vco_.phase = fall_vco_.phase = 0;
    jump_env_.vc = climb_env_.vc = 0;
    jump_cv_ = kInvLow;
    jump_hp_x_ = jump_hp_y_ = 0;
    climb_hp_x_ = climb_hp_y_ = climb_lp_ = 0;
    lfsr_ = 0;
    noise_phase_ = 0;
    noise_ = false;
    // At rest the walk and fall NANDs sit high: start the mixer and the
    // coupling there, so there is no thump at power-on.
    const float rest = kNandHigh * (kGWalk / kGSum) * kScaleWalk + kNandHigh * (kGFall / kGSum) * kScaleFall;
    mix_lp_ = rest;
    dc_x_ = rest;
    dc_y_ = 0;
}

// The one-shots fire on the bit's 1 -> 0 write (the board's inputs are
// inverted; the 74LS123s trigger on a rising edge) and retrigger.
void Board::write_latch(uint8_t bits)
{
    const uint8_t fell = uint8_t(bits_ & ~bits);
    if (fell & 0x01) walk_shot_.left = walk_shot_.width;
    if (fell & 0x02) jump_shot_.left = jump_shot_.width;
    if (fell & 0x04) climb_shot_.left = climb_shot_.width;
    bits_ = bits;
}

void Board::set_dac_full(bool full)
{
    if (full == dac_full_) return;
    dac_full_ = full;
    dac_env_ = 1;                           // the fade starts from full level
}

int16_t Board::sample(float dac)
{
    // DAC: fade, then the Sallen-Key filter.
    if (!dac_full_) dac_env_ *= dac_fade_k_;
    const float x = dac * kDacVolts * dac_env_;
    const float y = sk_b0_ * x + sk_b1_ * sk_x1_ + sk_b2_ * sk_x2_ - sk_a1_ * sk_y1_ - sk_a2_ * sk_y2_;
    sk_x2_ = sk_x1_;
    sk_x1_ = x;
    sk_y2_ = sk_y1_;
    sk_y1_ = (y > 1e-9f || y < -1e-9f) ? y : 0.0f;
    float mix = y * (kGDac / kGSum) * kScaleDac;

    // Counter 6L: its clock is slow while the bit 6K picks (12 with latch
    // bit 7 set, else 11) is high.
    const bool alt = bits_ & 0x80;
    const int sel = alt ? 12 : 11;
    const uint32_t c0 = count_;
    const uint32_t c1 = c0 + (((c0 >> (sel + 16)) & 1) ? count_slow_ : count_fast_);
    count_ = c1 & ((1u << 30) - 1);

    // Walk: NAND(one-shot, VCO xor counter bit 3 - or, with bit 7 set, the
    // VCO held low and counter bit 6).
    float walk = kNandHigh;
    if (walk_shot_.run()) {
        const float a = alt ? 0.0f : walk_vco_.run(walk_step_);
        const float b = counter_share(c0, c1, alt ? 6 : 3);
        const float xr = a + b - 2 * a * b;
        walk = kNandHigh - (kNandHigh - kNandLow) * xr;
    }
    mix += walk * (kGWalk / kGSum) * kScaleWalk;

    // Jump.
    const bool jump_on = jump_shot_.run();
    {
        const float v11 = jump_on ? kInvHigh : kInvLow;
        const float v12 = kInvHigh - (kInvHigh - kInvLow) * counter_share(c0, c1, 11);
        const float target = (v11 / kR13 + v12 / kR12) / (1 / kR13 + 1 / kR12);
        jump_cv_ += (target - jump_cv_) * jump_cv_k_;
    }
    if (jump_on || jump_env_.vc > 1e-4f || jump_hp_y_ > 1e-4f || jump_hp_y_ < -1e-4f) {
        float cv = jump_cv_;
        if (cv < kJumpV[0]) cv = kJumpV[0];
        if (cv > kJumpV[8]) cv = kJumpV[8];
        int i = 0;
        while (i < 7 && cv > kJumpV[i + 1]) ++i;
        const float hz = kJumpHz[i] + (kJumpHz[i + 1] - kJumpHz[i]) * (cv - kJumpV[i]) / (kJumpV[i + 1] - kJumpV[i]);
        const float high = jump_vco_.run(hz / rate_);
        const float env = jump_env_.run(jump_on) * (1.0f - high);
        const float o = env - jump_hp_x_ + jump_hp_k_ * jump_hp_y_;
        jump_hp_x_ = env;
        jump_hp_y_ = o;
        mix += o * (kGJump / kGSum) * kScaleJump;
    }

    // Climb.
    const bool climb_on = climb_shot_.run();
    if (climb_on || climb_env_.vc > 1e-4f || climb_lp_ > 1e-4f || climb_lp_ < -1e-4f) {
        const uint32_t p = noise_phase_ + noise_step_;
        if (p < noise_phase_) {
            const bool f0 = ((lfsr_ >> 2) ^ (lfsr_ >> 15)) & 1;
            lfsr_ = ((lfsr_ << 1) | (f0 ? 0u : 1u)) & 0xffff;
            noise_ = !f0;
        }
        noise_phase_ = p;
        const float env = climb_env_.run(climb_on) * (noise_ ? 0.0f : 1.0f);
        const float o = env - climb_hp_x_ + climb_hp_k_ * climb_hp_y_;
        climb_hp_x_ = env;
        climb_hp_y_ = o;
        climb_lp_ += (o - climb_lp_) * climb_lp_k_;
        mix += climb_lp_ * (kGClimb / kGSum) * kScaleClimb;
    }

    // Fall: NAND(bit, 570 Hz VCO).
    const float fall = fall_ ? kNandHigh - (kNandHigh - kNandLow) * fall_vco_.run(fall_step_) : kNandHigh;
    mix += fall * (kGFall / kGSum) * kScaleFall;

    // The mixer node's capacitor, then the amplifier's coupling.
    mix_lp_ += (mix - mix_lp_) * mix_lp_k_;
    const float o = mix_lp_ - dc_x_ + dc_r_ * dc_y_;
    dc_x_ = mix_lp_;
    dc_y_ = (o > 1e-3f || o < -1e-3f) ? o : 0.0f;

    float s = o;
    const float a = s < 0 ? -s : s;
    if (a > kKnee) {
        const float over = (a - kKnee) / (kKneeMax - kKnee);
        const float soft = kKnee + (kKneeMax - kKnee) * over / (1.0f + over);
        s = s < 0 ? -soft : soft;
    }
    return int16_t(s);
}

} // namespace dkongjr_sound
