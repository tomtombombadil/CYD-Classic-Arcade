// Namco 54XX noise model - see namco54.h.
#include "namco54.h"

#include <cmath>

namespace namco54 {

namespace {

constexpr double kPi = 3.14159265358979323846;

// The board's band-pass filters (OUT0-OUT2): centre, Q, and gain at the
// centre times the mixer's weight (OUT0 through 10K, the others 33K, into
// a 3.3K feedback), worked out from the part values.
struct Band {
    double fc, q, gain;
};
constexpr Band kBands[3] = {
    {167.0, 2.5, 0.51},
    {450.0, 2.1, 0.15},
    {2520.0, 1.7, 0.11},
};

// Per sound: noise clock (Hz), time to fall 40 dB (s), start level (of the
// 4-bit DAC's swing), level per output.
struct Shape {
    double noise_hz;
    double decay_s;
    float  out[3];
};
constexpr Shape kShapes[Chip::kSounds] = {
    {12000.0, 0.55, {0.0f, 1.0f, 1.0f}},   // A: crack
    {3000.0, 1.30, {1.0f, 0.7f, 0.0f}},    // B: rumble
    {6000.0, 0.70, {0.3f, 1.0f, 0.3f}},    // C: mid
};

// Full-scale DAC swing into the filters, in output sample units.
constexpr float kScale = 34000.0f;

} // namespace

void Chip::init(uint32_t out_rate)
{
    out_rate_ = out_rate ? out_rate : 1;
    for (int b = 0; b < 3; ++b) {
        Filter& f = filter_[b];
        double fc = kBands[b].fc;
        if (fc > out_rate_ / 6.0) fc = out_rate_ / 6.0;     // keep the filter stable
        f.f = float(2.0 * std::sin(kPi * fc / out_rate_));
        f.q = float(1.0 / kBands[b].q);
        // The state-variable band output peaks at Q times the input.
        f.gain = float(kBands[b].gain / kBands[b].q);
    }
    reset();
}

void Chip::reset()
{
    for (Voice& v : voice_) v = Voice{};
    for (Filter& f : filter_) f.low = f.band = 0;
    pending_ = 0;
    volume_c_ = 15;
    lfsr_ = 1;
}

bool Chip::playing() const
{
    for (const Voice& v : voice_)
        if (v.level > 0.001f) return true;
    return false;
}

void Chip::start(int sound)
{
    const Shape& s = kShapes[sound];
    Voice& v = voice_[sound];
    v.step = uint32_t(s.noise_hz / out_rate_ * 65536.0);
    v.decay = float(std::exp(std::log(0.01) / (s.decay_s * out_rate_)));
    v.level = sound == kC ? volume_c_ / 15.0f : 1.0f;
    ++starts_[sound];
}

void Chip::write(uint8_t data)
{
    ++commands_;
    if (pending_) {
        const int count = pending_sound_ == kC ? 5 : 4;
        voice_[pending_sound_].params[count - pending_] = data;
        --pending_;
        return;
    }
    switch (data >> 4) {
    case 1: start(kA); break;
    case 2: start(kB); break;
    case 3: pending_ = 4; pending_sound_ = kA; break;
    case 4: pending_ = 4; pending_sound_ = kB; break;
    case 5: start(kC); break;
    case 6: pending_ = 5; pending_sound_ = kC; break;
    case 7: volume_c_ = data & 15; break;
    default: break;
    }
}

void Chip::mix(int16_t* out, int n)
{
    bool quiet = !playing();
    for (const Filter& f : filter_) quiet = quiet && std::fabs(f.band) < 0.5f && std::fabs(f.low) < 0.5f;
    if (quiet) {
        for (Filter& f : filter_) f.low = f.band = 0;
        return;
    }
    for (int i = 0; i < n; ++i) {
        float in[3] = {0, 0, 0};
        for (int s = 0; s < kSounds; ++s) {
            Voice& v = voice_[s];
            if (v.level <= 0.001f) continue;
            v.phase += v.step;
            while (v.phase >= 65536) {
                v.phase -= 65536;
                // 17-bit LFSR (taps 17, 14), one noise stream for all.
                const uint32_t bit = (lfsr_ ^ (lfsr_ >> 3)) & 1;
                lfsr_ = (lfsr_ >> 1) | (bit << 16);
                v.noise = (lfsr_ & 1) ? 1.0f : -1.0f;
            }
            const float x = v.level * v.noise;
            for (int b = 0; b < 3; ++b) in[b] += x * kShapes[s].out[b];
            v.level *= v.decay;
        }
        float sum = 0;
        for (int b = 0; b < 3; ++b) {
            Filter& f = filter_[b];
            f.low += f.f * f.band;
            const float high = in[b] - f.low - f.q * f.band;
            f.band += f.f * high;
            sum += f.band * f.gain;
        }
        int32_t s = out[i] + int32_t(sum * kScale);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = int16_t(s);
    }
}

} // namespace namco54
