// Space Invaders sound board models - see invaders_sound.h.
//
// Part values below are the board's (schematic M051-00739); where a
// circuit is too involved to model (the op-amp VCOs), the figure is an
// estimate of what it produces, marked "~". Floats: the ESP32 has a
// single-precision FPU, and only the effects that are sounding run.
#include "invaders_sound.h"

#include <algorithm>
#include <cmath>

namespace invaders_sound {

namespace {

constexpr float kTwoPi = 6.2831853f;

// SN76477 (UFO): SLF R 120k / C 1 uF, VCO R 8.2k / C 0.1 uF. The SLF
// capacitor swings 0.33-2.37 V; the VCO capacitor runs from 0.33 V up to
// the SLF voltage + 0.35 V, so the pitch goes from ~780 Hz (SLF high) to
// ~5.3 kHz (SLF low), about 7 times a second.
constexpr float kSlfLow = 0.33f, kSlfHigh = 2.37f, kVcoAbove = 0.35f;
constexpr float kSlfR = 120e3f, kSlfC = 1e-6f, kVcoR = 8.2e3f, kVcoC = 0.1e-6f;
constexpr int   kUfoOversample = 4;

// Fleet: 555 with R2 = 75k, C = 0.1 uF; R1 = the bits' resistors in
// parallel (bit 0: 20k + 20k, bit 1: 68k, bit 2: 82k, bit 3: 100k), so the
// four notes are ~76, 66, 62 and 58 Hz. Output filters: 100 ohm / 4.7 uF
// (339 Hz) and 200 ohm / 10 uF (80 Hz); the second is set to 160 Hz here
// so a small speaker still carries the thump.
constexpr float kFleetR[4] = {40e3f, 68e3f, 82e3f, 100e3f};
constexpr float kFleetR2 = 75e3f, kFleetC = 0.1e-6f;

// Extra ship: 555 with R1 100k, R2 47k, C 1 uF (7.4 Hz, high 76 %) gating
// a 480 Hz square.
constexpr float kXsR1 = 100e3f, kXsR2 = 47e3f, kXsC = 1e-6f, kXsTone = 480.0f;

// Output levels (the mixer's input resistors, weighed by each circuit's swing).
// Shot, measured on a recording (see render): Hz, s, Hz per second
constexpr float kShotHigh = 1840, kShotLow = 580, kShotSweep = 0.153f, kShotSlope = 8800;
constexpr float kShotHoldFull = 0.06f, kShotLength = 0.41f;

constexpr float kUfoLevel = 4200, kShotLevel = 3000, kBaseHitLevel = 26000, kInvHitLevel = 4500;
constexpr float kUfoHitLevel = 4500, kFleetLevel = 9000, kXsLevel = 3000;

} // namespace

float Synth::lp(float fc) const { return 1.0f - std::exp(-kTwoPi * fc / rate_); }
float Synth::decay(float tau) const { return std::exp(-1.0f / (tau * rate_)); }

void Synth::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    noise_step_ = uint32_t(7515.0 / rate_ * 4294967296.0);

    // SN76477 charge / discharge rates as the chip's data gives them (V/s), per sub-sample.
    const float sub = rate_ * kUfoOversample;
    ufo_slf_rise_ = (kSlfHigh - kSlfLow) / (0.5885f * kSlfR * kSlfC + 0.0013f) / sub;
    ufo_slf_fall_ = (kSlfHigh - kSlfLow) / (0.5413f * kSlfR * kSlfC + 0.001343f) / sub;
    ufo_vco_rate_ = 0.64f * 2 * (kSlfHigh + kVcoAbove - kSlfLow) / (kVcoR * kVcoC) / sub;
    ufo_attack_ = lp(1.0f / (kTwoPi * 0.002f));

    // Shot: envelope on the bit 3 ms on / 56 ms off (560k x 0.1 uF); the
    // rest is fitted to a recording of the board (Tom, 2026-10-20: the shot
    // "isn't right" - v0.1's single slow 1.8 kHz -> 300 Hz slide wasn't it).
    shot_attack_ = 1.0f - decay(0.003f);
    shot_release_ = decay(0.056f);
    shot_noise_lp_ = lp(2000);

    // Player hit: noise through 5.6k / 0.1 uF (284 Hz) and 12.4k / 0.1 uF
    // (128 Hz); one-shot ~0.5 s (2.2 uF), release 680k x 1 uF.
    bh_c1_ = lp(284);
    bh_c2_ = lp(128);
    bh_attack_ = 1.0f - decay(0.001f);
    bh_release_ = decay(0.68f);

    // Invader hit: one-shot ~60 ms (0.1 uF), envelope rises with 10k x
    // 0.47 uF, falls ~0.15 s; ~1-2.5 kHz warbled ~70 times a second.
    ih_attack_ = 1.0f - decay(0.0047f);
    ih_release_ = decay(0.15f);
    ih_lfo_step_ = 2.0f * 70.0f / rate_;

    // UFO hit: envelope 100k x 1 uF on, ~0.4 s off; ~600-1600 Hz warbled 8 times a second.
    uh_attack_ = 1.0f - decay(0.1f);
    uh_release_ = decay(0.4f);
    uh_lfo_step_ = 2.0f * 8.0f / rate_;

    fl_c1_ = lp(339);
    fl_c2_ = lp(160);

    xs_tone_.set(kXsTone, rate_);
    const float xs_period = 0.693f * (kXsR1 + 2 * kXsR2) * kXsC;
    xs_gate_step_ = uint32_t(4294967296.0 / (xs_period * rate_));
    xs_gate_high_ = uint32_t((kXsR1 + kXsR2) / (kXsR1 + 2 * kXsR2) * 4294967295.0);
    xs_attack_ = 1.0f - decay(0.001f);

    dc_r_ = 1.0f - kTwoPi * 20.0f / rate_;
    reset();
}

void Synth::reset()
{
    p3_ = p5_ = 0;
    lfsr_ = 0x1ffff;
    noise_phase_ = 0;
    ufo_slf_ = ufo_vco_ = kSlfLow;
    ufo_slf_up_ = ufo_vco_up_ = true;
    ufo_gain_ = 0;
    shot_amp_ = shot_noise_ = 0;
    shot_t_ = kShotLength;
    bh_amp_ = bh_lp1_ = bh_lp2_ = 0;
    bh_hold_ = 0;
    ih_amp_ = ih_lfo_ = 0;
    ih_lfo_dir_ = 1;
    ih_hold_ = 0;
    uh_amp_ = uh_lfo_ = 0;
    uh_lfo_dir_ = 1;
    fl_phase_ = fl_step_ = 0;
    fl_lp1_ = fl_lp2_ = 0;
    xs_amp_ = 0;
    xs_gate_phase_ = 0;
    dc_x_ = dc_y_ = 0;
}

void Synth::write_port3(uint8_t v)
{
    const uint8_t rise = uint8_t(v & ~p3_);
    p3_ = v;
    if (rise & 0x02) shot_t_ = 0;
    if (rise & 0x04) bh_hold_ = int(0.5f * rate_);
    if (rise & 0x08) ih_hold_ = int(0.06f * rate_);
    if (rise & 0x10) xs_gate_phase_ = 0;              // the 555 starts on its high time
}

void Synth::write_port5(uint8_t v)
{
    p5_ = v;
    // Fleet: the selected resistors in parallel set the 555's R1.
    float g = 0;
    for (int b = 0; b < 4; ++b)
        if ((v & (1 << b)) && !(skip_ & (1 << (kFleet1 + b)))) g += 1.0f / kFleetR[b];
    if (g == 0) {
        fl_step_ = 0;
        return;
    }
    const float r1 = 1.0f / g;
    const float period = 0.693f * (r1 + 2 * kFleetR2) * kFleetC;
    fl_step_ = uint32_t(4294967296.0 / (period * rate_));
    fl_high_ = uint32_t((r1 + kFleetR2) / (r1 + 2 * kFleetR2) * 4294967295.0);
}

// One output sample of the noise register (+-1), clocked at 7515 Hz.
inline float Synth::noise_step()
{
    const uint32_t p = noise_phase_ + noise_step_;
    if (p < noise_phase_) {
        const uint32_t fb = ((lfsr_ >> 4) ^ (lfsr_ >> 16)) & 1;
        lfsr_ = ((lfsr_ << 1) | fb) & 0x1ffff;
    }
    noise_phase_ = p;
    return (lfsr_ & (1u << 12)) ? 1.0f : -1.0f;
}

void Synth::render(int16_t* out, int n)
{
    const bool ufo = (p3_ & 0x01) && !(skip_ & (1 << kUfo));
    const bool shot = !(skip_ & (1 << kShot));
    const bool bh = !(skip_ & (1 << kBaseHit));
    const bool ih = !(skip_ & (1 << kInvaderHit));
    const bool uh = !(skip_ & (1 << kUfoHit));
    const bool xs = (p3_ & 0x10) && !(skip_ & (1 << kExtraShip));
    const bool shot_gate = p3_ & 0x02;
    const bool uh_gate = p5_ & 0x10;

    for (int i = 0; i < n; ++i) {
        const float nz = noise_step();
        float mix = 0;

        // UFO: the capacitor voltages, oversampled (the VCO reaches 5 kHz).
        if (ufo || ufo_gain_ > 0.001f) {
            ufo_gain_ += ((ufo ? 1.0f : 0.0f) - ufo_gain_) * ufo_attack_;
            int acc = 0;
            for (int k = 0; k < kUfoOversample; ++k) {
                if (ufo_slf_up_) { ufo_slf_ += ufo_slf_rise_; if (ufo_slf_ >= kSlfHigh) ufo_slf_up_ = false; }
                else             { ufo_slf_ -= ufo_slf_fall_; if (ufo_slf_ <= kSlfLow) ufo_slf_up_ = true; }
                if (ufo_vco_up_) { ufo_vco_ += ufo_vco_rate_; if (ufo_vco_ >= ufo_slf_ + kVcoAbove) ufo_vco_up_ = false; }
                else             { ufo_vco_ -= ufo_vco_rate_; if (ufo_vco_ <= kSlfLow) ufo_vco_up_ = true; }
                acc += ufo_vco_up_ ? 1 : -1;
            }
            mix += float(acc) * (kUfoLevel / kUfoOversample) * ufo_gain_;
        }

        // Shot: a narrow pulse (a third of each cycle high) whose pitch
        // falls in a straight line from 1840 Hz to ~580 Hz in 143 ms and
        // starts again every 153 ms, fading out over 0.41 s - measured on a
        // recording of the board (a 1999 11 kHz WAV: the sweeps' slope,
        // period, pulse shape and the loudness, 8 % pitch jitter from the
        // noise). The game holds the bit ~1 s while the shot flies, so the
        // circuit, not the bit, ends the sound; a shot that hits sooner
        // ends with the bit's 56 ms release.
        if (shot && shot_t_ < kShotLength) {
            shot_amp_ += ((shot_gate ? 1.0f : 0.0f) - shot_amp_) * (shot_gate ? shot_attack_ : 1.0f - shot_release_);
            const float t = shot_t_;
            shot_t_ += 1.0f / rate_;
            const float u = std::fmod(t, kShotSweep);
            shot_noise_ += (nz - shot_noise_) * shot_noise_lp_;
            const float f = std::max(kShotLow, kShotHigh - kShotSlope * u) * (1.0f + 0.08f * shot_noise_);
            shot_osc_.set(f, rate_);
            const float fade = t < kShotHoldFull ? 1.0f
                             : 1.0f - std::pow((t - kShotHoldFull) / (kShotLength - kShotHoldFull), 1.2f);
            // Pulse +2/3, -1/3 of the swing: no DC
            const bool high = shot_osc_.tick_duty(0x55555555u);
            mix += (high ? 2.0f * kShotLevel : -kShotLevel) * shot_amp_ * fade;
        }

        // Player hit.
        if (bh && (bh_hold_ > 0 || bh_amp_ > 0.001f)) {
            if (bh_hold_ > 0) { --bh_hold_; bh_amp_ += (1.0f - bh_amp_) * bh_attack_; }
            else              bh_amp_ *= bh_release_;
            bh_lp1_ += (nz * bh_amp_ - bh_lp1_) * bh_c1_;
            bh_lp2_ += (bh_lp1_ - bh_lp2_) * bh_c2_;
            mix += bh_lp2_ * kBaseHitLevel;
        }

        // Invader hit.
        if (ih && (ih_hold_ > 0 || ih_amp_ > 0.001f)) {
            if (ih_hold_ > 0) { --ih_hold_; ih_amp_ += (1.0f - ih_amp_) * ih_attack_; }
            else              ih_amp_ *= ih_release_;
            ih_lfo_ += ih_lfo_dir_ * ih_lfo_step_;
            if (ih_lfo_ >= 1) { ih_lfo_ = 1; ih_lfo_dir_ = -1; }
            if (ih_lfo_ <= 0) { ih_lfo_ = 0; ih_lfo_dir_ = 1; }
            ih_osc_.set(1000.0f + 1500.0f * ih_lfo_, rate_);
            mix += (ih_osc_.tick() ? kInvHitLevel : -kInvHitLevel) * ih_amp_;
        }

        // UFO hit.
        if (uh && (uh_gate || uh_amp_ > 0.001f)) {
            if (uh_gate) uh_amp_ += (1.0f - uh_amp_) * uh_attack_;
            else         uh_amp_ *= uh_release_;
            uh_lfo_ += uh_lfo_dir_ * uh_lfo_step_;
            if (uh_lfo_ >= 1) { uh_lfo_ = 1; uh_lfo_dir_ = -1; }
            if (uh_lfo_ <= 0) { uh_lfo_ = 0; uh_lfo_dir_ = 1; }
            uh_osc_.set(600.0f + 1000.0f * uh_lfo_, rate_);
            mix += (uh_osc_.tick() ? kUfoHitLevel : -kUfoHitLevel) * uh_amp_;
        }

        // Fleet: the 555 runs only while a note bit is set; the filters ring out.
        if (fl_step_ || fl_lp1_ > 0.0005f || fl_lp2_ > 0.0005f) {
            float x = 0;
            if (fl_step_) {
                x = fl_phase_ < fl_high_ ? 1.0f : 0.0f;
                fl_phase_ += fl_step_;
            }
            fl_lp1_ += (x - fl_lp1_) * fl_c1_;
            fl_lp2_ += (fl_lp1_ - fl_lp2_) * fl_c2_;
            mix += fl_lp2_ * kFleetLevel;
        }

        // Extra ship.
        if (xs || xs_amp_ > 0.001f) {
            xs_amp_ += ((xs ? 1.0f : 0.0f) - xs_amp_) * xs_attack_;
            const bool gate = xs_gate_phase_ < xs_gate_high_;
            xs_gate_phase_ += xs_gate_step_;
            const bool tone = xs_tone_.tick();
            if (gate && tone) mix += kXsLevel * 2 * xs_amp_;
        }

        // Remove the DC (the fleet and extra-ship circuits are one-sided).
        const float y = mix - dc_x_ + dc_r_ * dc_y_;
        dc_x_ = mix;
        dc_y_ = (y > 0.01f || y < -0.01f) ? y : 0.0f;    // no denormals in the silence
        int s = int(y);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = int16_t(s);
    }
}

} // namespace invaders_sound
