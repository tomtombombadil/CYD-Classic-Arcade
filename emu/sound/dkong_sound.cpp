// Donkey Kong sound board models - see dkong_sound.h.
//
// Part numbers are the board's (TKG-4 sound board); voltages are on the
// board's 5 V scale and turned into samples at the end. Floats: the ESP32
// has a single-precision FPU. A circuit at rest (its latch bit clear and
// its envelope back up) costs almost nothing.
#include "dkong_sound.h"

#include <cmath>

namespace dkong_sound {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kVcc = 5.0f;
constexpr float kCvMax = 4.6f;          // the 555 stops near its supply

// 555 timing parts: Ra 47k, Rb 27k; C 47 nF (jump), 33 nF (walk).
constexpr float kRa = 47e3f, kRb = 27e3f, kJumpC = 47e-9f, kWalkC = 33e-9f;
constexpr float k555High = kVcc - 0.5f;

// The 4049 oscillators: f ~ 1 / (2.2 R C). Jump 18k / 3.3 uF (~7.7 Hz),
// walk 4.3k / 10 uF (~10.6 Hz).
constexpr float kJumpWobbleHz = 1.0f / (2.2f * 18e3f * 3.3e-6f);
constexpr float kWalkWobbleHz = 1.0f / (2.2f * 4.3e3f * 10e-6f);

// Control voltage of each 555: its own 5k / 10k divider (3.33 V through
// 3.33k) against the trigger and wobble network through 1.2k, by
// Thevenin. With the latch bit clear the open-collector inverter grounds
// one end; set, that end is pulled up to 5 V. cv = base + wobble x (0 / 5 V).
//   jump: 10k + 10k from the inverter, 10k from the wobble, 10 uF
//   walk: 1k + 10k from the inverter, 12k from the wobble, 3.3 uF
constexpr float kJumpCvOff = 2.168f, kJumpCvOffWob = 0.1748f;
constexpr float kJumpCvOn = 2.838f, kJumpCvOnWob = 0.198f;
constexpr float kJumpCvTau = 10e-6f * 2.17e3f;          // 10 uF x (3.33k || 6.2k)
constexpr float kWalkCvOff = 2.221f, kWalkCvOffWob = 0.1518f;
constexpr float kWalkCvOn = 3.099f, kWalkCvOnWob = 0.155f;
constexpr float kWalkCvTau = 3.3e-6f * 2.22e3f;         // 3.3 uF x (3.33k || 6.65k)

// Trigger pulses (jump, stomp): 1 uF charging through 10k + 10k; the
// transistor conducts while the 10k half is above 0.6 V - for ~28 ms.
constexpr float kPulseTau = 1e-6f * 20e3f, kPulseThreshold = kVcc - 2 * 0.6f;

// Envelopes: the capacitor is pulled down through 10k while the pulse
// lasts and charges back through 110k. Jump 4.7 uF, stomp 3.3 uF.
constexpr float kJumpFast = 10e3f * 4.7e-6f, kJumpSlow = 110e3f * 4.7e-6f;
constexpr float kStompFast = 10e3f * 3.3e-6f, kStompSlow = 110e3f * 3.3e-6f;
// The integrators after the diode mixers: a transistor follows the
// mixer up, charging 1 uF through 150 ohm (jump) / 750 ohm (stomp); when
// the mixer falls it lets go and the capacitor drains toward ground
// through that resistor + ~2.8k, until the transistor catches it again.
constexpr float kJumpUp = 150.0f * 1e-6f, kStompUp = 750.0f * 1e-6f;
constexpr float kJumpDown = (150.0f + 2.83e3f) * 1e-6f, kStompDown = (750.0f + 2.83e3f) * 1e-6f;
constexpr float kDivider = 5.1f / 7.1f;                  // 5.1k / (2k + 5.1k) to the mixer

// Walk: the trigger's 3.3 uF couples a pulse through 1k + 4.7k into 10k
// (x 0.637 at the start), cut off while the 555's output is high (it
// then charges faster, through 1k || 10k). Clearing the bit gives a small
// negative blip (a diode holds it to -0.6 V) as it drains through 4.7k.
constexpr float kWalkGain = 10.0f / 15.7f;
constexpr float kWalkTauLow = 3.3e-6f * 15.7e3f, kWalkTauHigh = 3.3e-6f * 6.61e3f;
constexpr float kWalkTauOff = 3.3e-6f * 4.7e3f;
constexpr float kWalkOut = 0.5f;                         // 5.6k / 11.2k divider

// Noise: three LS164s (24 bits) clocked by 2VF = 4 kHz, fed back from
// bits 10 and 23; an LS161 counts the output's rising edges.
constexpr float kNoiseHz = 4000.0f;

// DAC: 0-255 -> 0-5 V, VR2 at 0.9; the fade is 10k x 10 uF; Sallen-Key
// low-pass 5.6k / 5.6k, 22 nF / 10 nF: 1916 Hz, Q 0.74.
constexpr float kDacVolts = kVcc / 256.0f * 0.9f;
constexpr float kDacFadeTau = 10e3f * 10e-6f;
constexpr float kSkHz = 1916.0f, kSkQ = 0.74f;

// Output scale (sample units per volt at the mixer's inputs) and the
// amplifier's coupling (~20 Hz).
constexpr float kScale = 6000.0f;

// The transistor integrator: up with the input, else draining to ground,
// never below the input.
inline float follow(float y, float in, float up, float down)
{
    if (in > y) return y + (in - y) * up;
    y -= y * down;
    return y > in ? y : in;
}

} // namespace

float Board::lp(float tau) const { return 1.0f - std::exp(-1.0f / (tau * rate_)); }


void Board::Vco555::init(float ra, float rb, float c, float rate)
{
    const float t_low = rb * c * 0.693147f;
    for (int i = 0; i < 64; ++i) {
        float cv = kCvMax * float(i) / 63.0f;
        if (cv < 0.5f) cv = 0.5f;
        const float t_high = (ra + rb) * c * std::log((kVcc - cv / 2) / (kVcc - cv));
        step[i] = 1.0f / ((t_high + t_low) * rate);
        duty[i] = t_high / (t_high + t_low);
    }
}

// The share of this sample the output spends high: the integral of the
// square wave from phase p to p + step, over step.
float Board::Vco555::run(float cv)
{
    float x = cv * (63.0f / kCvMax);
    if (x < 0) x = 0;
    if (x > 62.999f) x = 62.999f;
    const int i = int(x);
    const float f = x - float(i);
    const float s = step[i] + (step[i + 1] - step[i]) * f;
    const float d = duty[i] + (duty[i + 1] - duty[i]) * f;
    const float p1 = phase + s;             // under 2: the 555 is far below the sample rate
    const float h0 = phase < d ? phase : d;
    const int whole = int(p1);
    const float frac = p1 - float(whole);
    const float h1 = float(whole) * d + (frac < d ? frac : d);
    phase = frac;
    return (h1 - h0) / s;
}

bool Board::Pulse::run(bool bit)
{
    if (bit) {
        v += (kVcc - v) * charge;
        return v < threshold;
    }
    v -= v * release;
    return false;
}

void Board::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    const double phase_unit = 4294967296.0 / rate_;
    noise_step_ = uint32_t(kNoiseHz * phase_unit);
    jump_wob_.step = uint32_t(kJumpWobbleHz * phase_unit);
    walk_wob_.step = uint32_t(kWalkWobbleHz * phase_unit);
    jump_vco_.init(kRa, kRb, kJumpC, rate_);
    walk_vco_.init(kRa, kRb, kWalkC, rate_);

    Pulse* const pulses[2] = {&jump_pulse_, &stomp_pulse_};
    for (Pulse* p : pulses) {
        p->charge = lp(kPulseTau);
        p->release = lp(0.001f);            // the clamp diode empties it at once
        p->threshold = kPulseThreshold;
    }
    jump_cv_k_ = lp(kJumpCvTau);
    walk_cv_k_ = lp(kWalkCvTau);
    jump_fast_ = lp(kJumpFast);
    jump_slow_ = lp(kJumpSlow);
    stomp_fast_ = lp(kStompFast);
    stomp_slow_ = lp(kStompSlow);
    jump_up_ = lp(kJumpUp);
    stomp_up_ = lp(kStompUp);
    jump_down_ = lp(kJumpDown);
    stomp_down_ = lp(kStompDown);
    walk_on_ = lp(kWalkTauLow);
    walk_on_gated_ = lp(kWalkTauHigh);
    walk_off_ = lp(kWalkTauOff);
    dac_fade_ = 1.0f - lp(kDacFadeTau);

    // Sallen-Key low-pass as a biquad (bilinear transform).
    const float w = kTwoPi * kSkHz / rate_;
    const float alpha = std::sin(w) / (2 * kSkQ), cw = std::cos(w);
    const float a0 = 1 + alpha;
    sk_b0_ = (1 - cw) / 2 / a0;
    sk_b1_ = (1 - cw) / a0;
    sk_b2_ = sk_b0_;
    sk_a1_ = -2 * cw / a0;
    sk_a2_ = (1 - alpha) / a0;

    dc_r_ = 1.0f - kTwoPi * 20.0f / rate_;
    reset();
}

void Board::reset()
{
    bits_ = 0;
    dac_full_ = true;
    dac_env_ = 1;
    sk_x1_ = sk_x2_ = sk_y1_ = sk_y2_ = 0;
    lfsr_ = 0;
    noise_phase_ = 0;
    counter_ = 0;
    noise_out_ = false;
    stomp_pulse_.v = jump_pulse_.v = 0;
    stomp_env_ = jump_env_ = kVcc;
    stomp_y_ = jump_y_ = kVcc;
    jump_cv_ = kJumpCvOff;
    walk_cv_ = kWalkCvOff;
    jump_vco_.phase = walk_vco_.phase = 0;
    jump_wob_.phase = walk_wob_.phase = 0;
    walk_cap_ = 0;
    // At rest the jump and stomp outputs sit at 5 V (x the divider): start
    // the coupling there, so there is no thump at power-on.
    dc_x_ = 2 * kVcc * kDivider;
    dc_y_ = 0;
}

void Board::write_latch(uint8_t bits) { bits_ = bits; }

void Board::set_dac_full(bool full)
{
    if (full == dac_full_) return;
    dac_full_ = full;
    dac_env_ = 1;                           // the fade starts from full level
}

int16_t Board::sample(float dac)
{
    // DAC: fade, then the Sallen-Key filter.
    if (!dac_full_) dac_env_ *= dac_fade_;
    const float x = dac * kDacVolts * dac_env_;
    const float y = sk_b0_ * x + sk_b1_ * sk_x1_ + sk_b2_ * sk_x2_ - sk_a1_ * sk_y1_ - sk_a2_ * sk_y2_;
    sk_x2_ = sk_x1_;
    sk_x1_ = x;
    sk_y2_ = sk_y1_;
    sk_y1_ = (y > 1e-9f || y < -1e-9f) ? y : 0.0f;
    float mix = y;

    // Stomp.
    const bool stomp_on = stomp_pulse_.run(bits_ & 0x04);
    if (stomp_on || stomp_env_ < kVcc - 0.001f || stomp_y_ < kVcc - 0.001f) {
        stomp_env_ += ((stomp_on ? 0.0f : kVcc) - stomp_env_) * (stomp_on ? stomp_fast_ : stomp_slow_);
        const uint32_t p = noise_phase_ + noise_step_;
        if (p < noise_phase_) {
            const bool out = ((lfsr_ >> 10) ^ (lfsr_ >> 23)) & 1;
            lfsr_ = ((lfsr_ << 1) | (out ? 0u : 1u)) & 0xffffff;
            if (out && !noise_out_) counter_ = uint8_t((counter_ + 1) & 7);
            noise_out_ = out;
        }
        noise_phase_ = p;
        const float sq = (counter_ & 4) ? kVcc : 0.0f;
        const float m = sq > stomp_env_ ? sq : stomp_env_;
        stomp_y_ = follow(stomp_y_, m, stomp_up_, stomp_down_);
    }
    mix += stomp_y_ * kDivider;

    // Jump.
    const bool jump_bit = bits_ & 0x02;
    const bool jump_on = jump_pulse_.run(jump_bit);
    if (jump_bit || jump_on || jump_env_ < kVcc - 0.001f || jump_y_ < kVcc - 0.001f) {
        const float wob = jump_wob_.tick() ? kVcc : 0.0f;
        const float target = jump_bit ? kJumpCvOn + kJumpCvOnWob * wob : kJumpCvOff + kJumpCvOffWob * wob;
        jump_cv_ += (target - jump_cv_) * jump_cv_k_;
        const float tone = jump_vco_.run(jump_cv_) * k555High;
        jump_env_ += ((jump_on ? 0.0f : kVcc) - jump_env_) * (jump_on ? jump_fast_ : jump_slow_);
        const float m = tone > jump_env_ ? tone : jump_env_;
        jump_y_ = follow(jump_y_, m, jump_up_, jump_down_);
    }
    mix += jump_y_ * kDivider;

    // Walk.
    const bool walk_bit = bits_ & 0x01;
    if (walk_bit || walk_cap_ > 0.001f) {
        const float wob = walk_wob_.tick() ? kVcc : 0.0f;
        const float target = walk_bit ? kWalkCvOn + kWalkCvOnWob * wob : kWalkCvOff + kWalkCvOffWob * wob;
        walk_cv_ += (target - walk_cv_) * walk_cv_k_;
        const float high = walk_vco_.run(walk_cv_);
        float out;
        if (walk_bit) {
            out = (1.0f - high) * (kVcc - walk_cap_) * kWalkGain;
            walk_cap_ += (kVcc - walk_cap_) * (high * walk_on_gated_ + (1.0f - high) * walk_on_);
        } else {
            const float neg = walk_cap_ * 0.68f;
            out = -(1.0f - high) * (neg < 0.6f ? neg : 0.6f);
            walk_cap_ -= walk_cap_ * walk_off_;
        }
        mix += out * kWalkOut;
    }

    // The amplifier's coupling capacitors remove the DC.
    const float o = mix - dc_x_ + dc_r_ * dc_y_;
    dc_x_ = mix;
    dc_y_ = (o > 1e-6f || o < -1e-6f) ? o : 0.0f;
    int s = int(o * kScale);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return int16_t(s);
}

} // namespace dkong_sound
