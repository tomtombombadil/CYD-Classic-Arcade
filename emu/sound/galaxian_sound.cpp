// Galaxian sound model - see galaxian_sound.h.
#include "galaxian_sound.h"

#include <cmath>

namespace galaxian_sound {

namespace {

constexpr float kVcc = 5.0f;
constexpr float kSoundClock = 1536000.0f;   // 18.432 MHz / 12
constexpr float kNoiseClock = 122880.0f;    // the shift register: 12.288 MHz / 100
constexpr float kNoiseTick = 7920.0f;       // the flip-flop: 60 x 264 / 2 (line counter)

// Pre-mix (conductances in 1 / kilohm): QA 33k, QC 22k, + QC 10k (VOL1),
// QD 15k (VOL2), the background 5.1k.
constexpr float kGQa = 1.0f / 33, kGQc = 1.0f / 22, kGVol1 = 1.0f / 10, kGVol2 = 1.0f / 15, kGBg = 1.0f / 5.1f;
// Final mixer: 10k feedback over 5.1k (pre-mix), 1.32k (HIT), 2.2k (FIRE).
constexpr float kGainPre = 10.0f / 5.1f, kGainHit = 10.0f / 1.32f, kGainFire = 10.0f / 2.2f;

// Background DAC: bits 0-3 through 1M / 470k / 220k / 100k (high = 4 V),
// 15k to 4.4 V, 330k to ground; the current source drops 0.7 V into 100k,
// charging 1 uF from 1/3 to 2/3 Vcc.
constexpr float kDacR[4] = {1000, 470, 220, 100};
constexpr float kDacHigh = 4.0f, kDacBiasV = 4.4f, kDacBiasR = 15, kDacGndR = 330;
constexpr float kLfoR = 100e3f, kLfoC = 1e-6f, kVbe = 0.7f;
// Its sawtooth into the FS control voltage: x R33 / (R31 || R32 || R33)
// - 5 V x R33 / R31 (10k, 47k, 47k): 1.3-3.7 V.
constexpr float kCvGain = 10.0f / (1.0f / (1.0f / 10 + 2.0f / 47)), kCvOffset = -kVcc * 10.0f / 47;

// HIT: hold RC (150k + 22k, 2.2 uF); band-pass 150k in, 22k shunt, 470k
// feedback, 0.01 uF x 2.
constexpr float kHitTau = 172e3f * 2.2e-6f;
constexpr float kBpF0 = 167.6f, kBpQ = 2.47f, kBpGain = 470.0f / (2 * 150);
// FIRE: RC 2.2k / 47 uF; control voltage = (4 V x noise / 10k + RC / 2.2k)
// x (10k || 2.2k); ring-out 100k / 1 uF.
constexpr float kFireRcTau = 2.2e3f * 47e-6f, kFireEnvTau = 100e3f * 1e-6f;
constexpr float kFireParallel = 1.0f / (1.0f / 10 + 1.0f / 2.2f);
constexpr float kFireNoiseShare = kFireParallel / 10, kFireRcShare = kFireParallel / 2.2f;

// Each path's level against the mixer's own weights, set for a small
// speaker: by the resistors alone the tunes would peak at a sixth of the
// shot and the explosions (which would drive the op-amp to its rails).
// Peaks in play then (PC runs): tunes ~12,000, swarm ~6,000, shot
// ~11,000, explosions ~15,000, all together up to the knee's ceiling.
constexpr float kToneLevel = 4.2f, kBgLevel = 0.85f, kHitLevel = 0.9f, kFireLevel = 0.45f;
// The output: a 20 Hz coupling high-pass, then volts -> samples, with a
// soft knee above 15,000 that approaches 22,000 (an explosion's first
// swing and a tune together go no further).
constexpr float kDcHz = 20.0f;
constexpr float kOutGain = 1050.0f;
constexpr float kKnee = 15000.0f, kCeiling = 22000.0f;

// The time bit b of a counter that counts once a unit spends high in
// [0, t): bit b is high for 2^b of every 2^(b+1) counts. t in 16.16 fixed
// point (no floor or division a sample).
uint32_t bit_time(int b, uint32_t t)
{
    const uint32_t rest = t & ((2u << (b + 16)) - 1), half = 1u << (b + 16);
    return ((t >> (b + 17)) << (b + 16)) + (rest > half ? rest - half : 0);
}

} // namespace

void Astable::init(float ra, float rb, float c, float dt)
{
    tau_high = (ra + rb) * c;
    tau_low = rb * c;
    k_high = std::exp(-dt / tau_high);
    k_low = std::exp(-dt / tau_low);
    reset();
}

float Astable::run(float cv, float dt)
{
    float left = dt, high_time = 0;
    for (int guard = 0; guard < 16 && left > 0; ++guard) {
        if (high) {
            // Charging towards Vcc; the output falls at cv (never, at 5 V).
            const float k = left == dt ? k_high : std::exp(-left / tau_high);
            const float end = kVcc - (kVcc - v) * k;
            if (cv >= kVcc - 1e-3f || end < cv) {
                v = end;
                high_time += left;
                break;
            }
            const float t = v >= cv ? 0 : tau_high * std::log((kVcc - v) / (kVcc - cv));
            high_time += t;
            left -= t;
            v = cv;
            high = false;
        } else {
            // Discharging towards 0 V; the output rises at cv / 2.
            const float k = left == dt ? k_low : std::exp(-left / tau_low);
            const float lo = cv * 0.5f;
            const float end = v * k;
            if (lo <= 1e-3f || end > lo) {
                v = end;
                break;
            }
            const float t = v <= lo ? 0 : tau_low * std::log(v / lo);
            left -= t;
            v = lo;
            high = true;
        }
    }
    return high_time / dt;
}

float Sound::lfo_hz(int code)
{
    float num = kDacBiasV / kDacBiasR, den = 1.0f / kDacBiasR + 1.0f / kDacGndR;
    for (int b = 0; b < 4; ++b) {
        den += 1.0f / kDacR[b];
        if (code & (1 << b)) num += kDacHigh / kDacR[b];
    }
    const float current = (num / den - kVbe) / kLfoR;
    return current > 0 ? current / (kLfoC * kVcc / 3) : 0;
}

float Sound::fs_cv(float cap)
{
    const float cv = cap * kCvGain + kCvOffset;
    return cv < 0 ? 0 : cv > kVcc ? kVcc : cv;
}

void Sound::init(uint32_t audio_rate)
{
    rate_ = audio_rate;
    dt_ = 1.0f / float(audio_rate);
    fs_[0].init(100e3f, 470e3f, 0.01e-6f, dt_);
    fs_[1].init(100e3f, 330e3f, 0.01e-6f, dt_);
    fs_[2].init(100e3f, 220e3f, 0.01e-6f, dt_);
    fire_osc_.init(10e3f, 22e3f, 0.01e-6f, dt_);
    k_fire_rc_ = std::exp(-dt_ / kFireRcTau);
    k_fire_env_ = std::exp(-dt_ / kFireEnvTau);
    k_hit_env_ = std::exp(-dt_ / kHitTau);

    const float w0 = 2.0f * 3.14159265f * kBpF0 / float(audio_rate);
    const float alpha = std::sin(w0) / (2.0f * kBpQ);
    const float a0 = 1.0f + alpha;
    bp_b0_ = alpha / a0;
    bp_a1_ = -2.0f * std::cos(w0) / a0;
    bp_a2_ = (1.0f - alpha) / a0;
    dc_r_ = std::exp(-2.0f * 3.14159265f * kDcHz / float(audio_rate));
    reset();
}

void Sound::reset()
{
    pitch_ = 0xff;
    lfo_ = 0;
    ctrl_ = 0;
    tone_t_ = 0;
    lfo_v_ = kVcc / 3;
    lfo_rate_ = lfo_hz(0) * kVcc / 3;
    for (Astable& o : fs_) o.reset();
    fire_osc_.reset();
    fire_rc_ = 4.0f;
    fire_env_ = hit_env_ = 0;
    lfsr_ = 0;
    noise_clock_ = 1;
    lfsr_due_ = 0;
    noise_ = 0;
    bp_x1_ = bp_x2_ = bp_y1_ = bp_y2_ = 0;
    dc_x_ = dc_y_ = 0;
}

void Sound::lfo(int bit, bool on)
{
    const uint8_t m = uint8_t(1 << (bit & 3));
    lfo_ = on ? uint8_t(lfo_ | m) : uint8_t(lfo_ & ~m);
    lfo_rate_ = lfo_hz(lfo_) * kVcc / 3;
}

void Sound::control(int reg, bool on)
{
    const uint8_t m = uint8_t(1 << (reg & 7));
    const uint8_t was = ctrl_;
    ctrl_ = on ? uint8_t(ctrl_ | m) : uint8_t(ctrl_ & ~m);
    for (int i = 0; i < 3; ++i)
        if ((was ^ ctrl_) & ~ctrl_ & (1 << i)) fs_[i].reset();      // held in reset: capacitor emptied
}

// The flip-flop's output averaged over dt, stepping the shift register.
float Sound::noise_mean(float dt)
{
    float left = dt * kNoiseTick, sum = 0;
    const float span = left;
    while (noise_clock_ <= left) {
        sum += noise_ * noise_clock_;
        left -= noise_clock_;
        for (lfsr_due_ += kNoiseClock / kNoiseTick; lfsr_due_ >= 1; lfsr_due_ -= 1)
            lfsr_ = (lfsr_ >> 1) | ((((lfsr_ >> 12) ^ ~lfsr_) & 1u) << 16);
        noise_ = uint8_t(lfsr_ & 1);
        noise_clock_ = 1;
    }
    sum += noise_ * left;
    noise_clock_ -= left;
    return sum / span;
}

void Sound::render(int16_t* out, int n)
{
    // 4-bit counter steps a sample, 16.16 fixed point.
    const uint32_t count_rate = uint32_t(kSoundClock / float(256 - pitch_) * dt_ * 65536.0f);
    const float per_count = count_rate ? 1.0f / float(count_rate) : 0;
    const bool vol1 = ctrl_ & (1 << kVol1), vol2 = ctrl_ & (1 << kVol2);
    const float g_qc = kGQc + (vol1 ? kGVol1 : 0), g_qd = vol2 ? kGVol2 : 0;
    const float g_sum = kGQa + g_qc + g_qd + kGBg;
    const bool hit = ctrl_ & (1 << kHit), fire = ctrl_ & (1 << kFire);

    for (int i = 0; i < n; ++i) {
        // Tone: QA / QC / QD averaged over the sample (a fast count is its mean).
        float qa, qc, qd;
        if (count_rate >= 16u << 16) {
            qa = qc = qd = 0.5f;
        } else {
            const uint32_t t0 = tone_t_, t1 = tone_t_ + count_rate;
            qa = float(bit_time(0, t1) - bit_time(0, t0)) * per_count;
            qc = float(bit_time(2, t1) - bit_time(2, t0)) * per_count;
            qd = float(bit_time(3, t1) - bit_time(3, t0)) * per_count;
            tone_t_ = t1 & ((16u << 16) - 1);
        }

        // Background: the sawtooth sets the three 555s' control voltage.
        lfo_v_ += lfo_rate_ * dt_;
        if (lfo_v_ >= kVcc * 2 / 3) lfo_v_ = kVcc / 3;
        const float cv = fs_cv(lfo_v_);
        float bg = 0;
        for (int k = 0; k < 3; ++k)
            if (ctrl_ & (1 << k)) bg += fs_[k].run(cv, dt_);
        bg *= kVcc / 3;

        const float tone = (kGQa * qa + g_qc * qc + g_qd * qd) * kVcc / g_sum;
        const float swarm = kGBg * bg / g_sum;

        // Noise, HIT and FIRE (each skipped while it can't be heard: the
        // noise and the shot's 555 only run while HIT or FIRE needs them).
        hit_env_ = hit ? 1.0f : hit_env_ > 1e-4f ? hit_env_ * k_hit_env_ : 0;
        fire_env_ = fire ? 1.0f : fire_env_ > 1e-4f ? fire_env_ * k_fire_env_ : 0;
        fire_rc_ = (fire ? 0.0f : 4.0f) + (fire_rc_ - (fire ? 0.0f : 4.0f)) * k_fire_rc_;
        const float noise = hit_env_ > 0 || fire_env_ > 0 ? noise_mean(dt_) : 0;
        float hit_out = 0;
        if (hit_env_ > 0 || bp_y1_ != 0 || bp_y2_ != 0 || bp_x1_ != 0) {
            const float x = kVcc * noise * hit_env_;
            float y = bp_b0_ * (x - bp_x2_) - bp_a1_ * bp_y1_ - bp_a2_ * bp_y2_;
            if (y > -1e-6f && y < 1e-6f && x == 0 && bp_x1_ == 0) y = 0;     // rung out
            bp_x2_ = bp_x1_;
            bp_x1_ = x;
            bp_y2_ = bp_y1_;
            bp_y1_ = y;
            hit_out = y * kBpGain;
        }
        float fire_out = 0;
        if (fire_env_ > 0) {
            const float fire_cv = 4.0f * noise * kFireNoiseShare + fire_rc_ * kFireRcShare;
            fire_out = kVcc * fire_osc_.run(fire_cv, dt_) * fire_env_;
        }

        // Mixer and output coupling.
        const float in = kGainPre * (kToneLevel * tone + kBgLevel * swarm) + kHitLevel * kGainHit * hit_out +
                         kFireLevel * kGainFire * fire_out;
        const float y = dc_r_ * (dc_y_ + in - dc_x_);
        dc_x_ = in;
        dc_y_ = y;
        float s = y * kOutGain;
        const float mag = s < 0 ? -s : s;
        if (mag > kKnee) {
            // Soft knee: the rest of the way to the ceiling is approached, not hit.
            const float over = (mag - kKnee) / (kCeiling - kKnee);
            const float m = kKnee + (kCeiling - kKnee) * over / (1.0f + over);
            s = s < 0 ? -m : m;
        }
        out[i] = int16_t(s);
    }
}

} // namespace galaxian_sound
