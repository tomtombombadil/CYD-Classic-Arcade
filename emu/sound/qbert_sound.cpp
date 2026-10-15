// Q*bert's speech and knocker models - see qbert_sound.h.
#include "qbert_sound.h"

#include <cmath>

namespace qbert_sound {

namespace {

constexpr float kPi = 3.14159265f;

// Phoneme kinds.
enum Kind : uint8_t {
    kSilent,    // pauses, STOP
    kVowel,     // voiced, full level
    kSonorant,  // voiced, softer (nasals, liquids, glides)
    kVoicedFric,// voice and hiss
    kHiss,      // hiss only
    kAspirate,  // H: hiss through the vowel formants
    kStopV,     // closure (a weak voice bar), then a burst
    kStopU,     // closure (silence), then a burst
};

struct Phoneme {
    uint8_t  ms;        // length at the nominal clock (the datasheet's, rounded)
    uint8_t  kind;
    uint16_t f1, f2;    // formants, Hz (voiced kinds)
    uint16_t nf;        // hiss centre, Hz (hiss kinds and bursts)
};

// The SC-01's 64 phonemes in code order. Lengths are close to the
// datasheet's; formants are ordinary English values for the sound named.
constexpr Phoneme kPhonemes[64] = {
    {59, kVowel, 530, 1840, 0},        // 00 EH3
    {71, kVowel, 530, 1840, 0},        // 01 EH2
    {121, kVowel, 530, 1840, 0},       // 02 EH1
    {47, kSilent, 500, 1500, 0},       // 03 PA0
    {47, kStopV, 300, 1700, 3500},     // 04 DT
    {121, kVowel, 480, 2000, 0},       // 05 A1
    {71, kVowel, 480, 2000, 0},        // 06 A2
    {71, kVoicedFric, 300, 1800, 2500},// 07 ZH
    {71, kVowel, 730, 1090, 0},        // 08 AH2
    {59, kVowel, 390, 1990, 0},        // 09 I3
    {71, kVowel, 390, 1990, 0},        // 0A I2
    {121, kVowel, 390, 1990, 0},       // 0B I1
    {103, kSonorant, 280, 1000, 0},    // 0C M
    {80, kSonorant, 280, 1700, 0},     // 0D N
    {71, kStopV, 200, 900, 1200},      // 0E B
    {71, kVoicedFric, 300, 1100, 4000},// 0F V
    {71, kHiss, 0, 0, 2700},           // 10 CH
    {121, kHiss, 0, 0, 2500},          // 11 SH
    {71, kVoicedFric, 300, 1700, 5500},// 12 Z
    {146, kVowel, 570, 840, 0},        // 13 AW1
    {121, kSonorant, 280, 2200, 0},    // 14 NG
    {146, kVowel, 730, 1090, 0},       // 15 AH1
    {103, kVowel, 440, 1020, 0},       // 16 OO1
    {185, kVowel, 440, 1020, 0},       // 17 OO
    {103, kSonorant, 360, 1100, 0},    // 18 L
    {80, kStopU, 0, 0, 2000},          // 19 K
    {47, kVoicedFric, 300, 1800, 2500},// 1A J
    {71, kAspirate, 500, 1500, 1500},  // 1B H
    {71, kStopV, 300, 2000, 2000},     // 1C G
    {103, kHiss, 0, 0, 4500},          // 1D F
    {55, kStopV, 300, 1700, 3500},     // 1E D
    {90, kHiss, 0, 0, 6000},           // 1F S
    {185, kVowel, 480, 2000, 0},       // 20 A
    {65, kVowel, 400, 2200, 0},        // 21 AY
    {80, kSonorant, 280, 2250, 0},     // 22 Y1
    {47, kVowel, 520, 1190, 0},        // 23 UH3
    {250, kVowel, 730, 1090, 0},       // 24 AH
    {103, kStopU, 0, 0, 1000},         // 25 P
    {185, kVowel, 450, 800, 0},        // 26 O
    {185, kVowel, 390, 1990, 0},       // 27 I
    {185, kVowel, 300, 870, 0},        // 28 U
    {103, kSonorant, 280, 2250, 0},    // 29 Y
    {71, kStopU, 0, 0, 4000},          // 2A T
    {90, kSonorant, 420, 1250, 0},     // 2B R
    {185, kVowel, 270, 2290, 0},       // 2C E
    {80, kSonorant, 300, 700, 0},      // 2D W
    {185, kVowel, 660, 1720, 0},       // 2E AE
    {103, kVowel, 660, 1720, 0},       // 2F AE1
    {90, kVowel, 570, 840, 0},         // 30 AW2
    {71, kVowel, 520, 1190, 0},        // 31 UH2
    {103, kVowel, 520, 1190, 0},       // 32 UH1
    {185, kVowel, 520, 1190, 0},       // 33 UH
    {80, kVowel, 450, 800, 0},         // 34 O2
    {121, kVowel, 450, 800, 0},        // 35 O1
    {59, kVowel, 300, 1600, 0},        // 36 IU
    {90, kVowel, 300, 870, 0},         // 37 U1
    {80, kVoicedFric, 300, 1500, 4500},// 38 THV
    {71, kHiss, 0, 0, 4500},           // 39 TH
    {146, kVowel, 490, 1350, 0},       // 3A ER
    {185, kVowel, 530, 1840, 0},       // 3B EH
    {121, kVowel, 270, 2290, 0},       // 3C E1
    {250, kVowel, 570, 840, 0},        // 3D AW
    {185, kSilent, 500, 1500, 0},      // 3E PA1
    {47, kSilent, 500, 1500, 0},       // 3F STOP
};

// Two-pole resonator coefficients (unity gain at DC): y = a x + b y1 + c y2.
void resonator(float f, float bw, float fs, float* k)
{
    const float r = std::exp(-kPi * bw / fs);
    k[1] = 2.0f * r * std::cos(2.0f * kPi * f / fs);
    k[2] = -r * r;
    k[0] = 1.0f - k[1] - k[2];
}

} // namespace

// --- speech --------------------------------------------------------------------

void Speech::init(uint32_t sample_rate, uint32_t cpu_clock)
{
    fs_ = sample_rate;
    cpu_clock_ = cpu_clock;
    reset();
}

void Speech::reset()
{
    phoneme_ = 0x3f;
    inflection_ = 0;
    busy_ = false;
    done_at_ = 0;
    f1_ = 500;
    f2_ = 1500;
    amp_ = noise_amp_ = phase_ = 0;
    y1a_ = y1b_ = y2a_ = y2b_ = yna_ = ynb_ = 0;
    hp_in_ = hp_out_ = 0;
    age_ = 0;
}

// The clock DAC sets the chip's clock: a higher value, a faster, higher
// voice. Our own mapping: 0.6x to 2x the nominal 720 kHz.
float Speech::rate() const { return 0.6f + clock_dac_ * (1.4f / 255.0f); }

void Speech::write(uint8_t phoneme, uint8_t inflection, int64_t cycle)
{
    phoneme_ = phoneme & 0x3f;
    inflection_ = inflection & 3;
    age_ = 0;
    ++count_;
    const double ms = kPhonemes[phoneme_].ms / rate();
    busy_ = true;
    done_at_ = cycle + int64_t(ms * cpu_clock_ / 1000.0);
}

int16_t Speech::sample()
{
    const Phoneme& p = kPhonemes[phoneme_];
    const float fs = float(fs_);
    const float r = rate();
    const int len = int(p.ms / r * fs / 1000.0f);   // the phoneme's length in samples
    ++age_;

    // Source levels for this point of the phoneme.
    float voice = 0, hiss = 0;
    switch (p.kind) {
    case kVowel:      voice = 1.0f; break;
    case kSonorant:   voice = 0.55f; break;
    case kVoicedFric: voice = 0.45f; hiss = 0.35f; break;
    case kHiss:       hiss = 0.6f; break;
    case kAspirate:   hiss = 0.5f; break;
    case kStopV:
    case kStopU: {
        // Closure for the first 60 %, then a burst that fades.
        const int closure = len * 6 / 10;
        if (age_ < closure) voice = p.kind == kStopV ? 0.15f : 0;
        else {
            const float t = float(age_ - closure) / (fs * 0.02f);
            hiss = t < 3 ? 0.8f * std::exp(-t) : 0;
            voice = p.kind == kStopV ? 0.4f : 0;
        }
        break;
    }
    default: break;
    }

    // Nothing to say and nothing ringing: silence, cheaply.
    if (voice == 0 && hiss == 0 && amp_ < 1e-3f && noise_amp_ < 1e-3f) {
        amp_ = noise_amp_ = 0;
        hp_in_ = hp_out_ = 0;
        y1a_ = y1b_ = y2a_ = y2b_ = yna_ = ynb_ = 0;
        return 0;
    }

    // Glide the levels (~4 ms) and the formants (~12 ms) to their targets:
    // the chip's transitions are smooth, and steps would click.
    const float kl = 1.0f - std::exp(-1.0f / (fs * 0.004f));
    const float kf = 1.0f - std::exp(-1.0f / (fs * 0.012f));
    amp_ += (voice - amp_) * kl;
    noise_amp_ += (hiss - noise_amp_) * kl;
    if (p.f1) {
        f1_ += (p.f1 - f1_) * kf;
        f2_ += (p.f2 - f2_) * kf;
    }

    // The voice: a falling sawtooth at the pitch (higher with a faster
    // clock and with the inflection bits).
    const float f0 = 98.0f * r * (1.0f + 0.07f * inflection_);
    phase_ += f0 / fs;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    const float src = amp_ * (1.0f - 2.0f * phase_);

    lfsr_ ^= lfsr_ << 13;
    lfsr_ ^= lfsr_ >> 17;
    lfsr_ ^= lfsr_ << 5;
    const float noise = noise_amp_ * (float(int32_t(lfsr_)) * (1.0f / 2147483648.0f));

    // Formant filters (recomputed when they have glided a little).
    if (std::fabs(f1_ - kf1_) > 3) { resonator(f1_, 90, fs, k1_); kf1_ = f1_; }
    if (std::fabs(f2_ - kf2_) > 6) { resonator(f2_, 120, fs, k2_); kf2_ = f2_; }
    const float nf = p.nf ? (p.nf > fs * 0.45f ? fs * 0.45f : float(p.nf)) : 1500.0f;
    if (nf != knf_) { resonator(nf, 1200, fs, kn_); knf_ = nf; }

    // Voice (and aspiration) through F1 then F2; hiss through its own band.
    const float x = src + (p.kind == kAspirate ? noise : 0);
    const float y1 = k1_[0] * x + k1_[1] * y1a_ + k1_[2] * y1b_;
    y1b_ = y1a_;
    y1a_ = y1;
    const float y2 = k2_[0] * y1 + k2_[1] * y2a_ + k2_[2] * y2b_;
    y2b_ = y2a_;
    y2a_ = y2;
    float out = y2 + 0.5f * (y1 - y2);      // a little of F1 alone: fuller vowels
    if (p.kind != kAspirate) {
        const float yn = kn_[0] * noise + kn_[1] * yna_ + kn_[2] * ynb_;
        ynb_ = yna_;
        yna_ = yn;
        out += 0.25f * yn;
    }

    // DC block, then the level.
    const float hp = out - hp_in_ + 0.995f * hp_out_;
    hp_in_ = out;
    hp_out_ = hp;
    float v = hp * 3200.0f;     // the loudest vowels peak near 12,000
    if (v > 14000.0f) v = 14000.0f;
    if (v < -14000.0f) v = -14000.0f;
    return int16_t(v);
}

// --- knocker -------------------------------------------------------------------

void Knocker::init(uint32_t sample_rate)
{
    fs_ = sample_rate;
    reset();
}

void Knocker::reset()
{
    left_ = 0;
    y1a_ = y1b_ = y2a_ = y2b_ = click_ = 0;
}

// The solenoid's plunger hits the panel: the panel booms (~60 Hz, ~90 ms),
// the cabinet thuds (~150 Hz, ~30 ms), and the hit itself clicks (~2 ms).
void Knocker::strike()
{
    const float fs = float(fs_);
    const float w1 = 2 * kPi * 60.0f / fs, r1 = std::exp(-1.0f / (fs * 0.090f));
    const float w2 = 2 * kPi * 150.0f / fs, r2 = std::exp(-1.0f / (fs * 0.030f));
    b1_ = 2 * r1 * std::cos(w1);
    c1_ = -r1 * r1;
    b2_ = 2 * r2 * std::cos(w2);
    c2_ = -r2 * r2;
    // Each ringing starts as a sine from zero.
    y1a_ = std::sin(w1);
    y1b_ = 0;
    y2a_ = std::sin(w2);
    y2b_ = 0;
    click_ = 1;
    click_decay_ = std::exp(-1.0f / (fs * 0.002f));
    left_ = int(fs * 0.45f);
}

int16_t Knocker::sample()
{
    if (left_ <= 0) return 0;
    --left_;
    const float y1 = b1_ * y1a_ + c1_ * y1b_;
    y1b_ = y1a_;
    y1a_ = y1;
    const float y2 = b2_ * y2a_ + c2_ * y2b_;
    y2b_ = y2a_;
    y2a_ = y2;
    lfsr_ = (lfsr_ >> 1) ^ (-(lfsr_ & 1u) & 0xb400u);
    const float n = (lfsr_ & 1) ? 1.0f : -1.0f;
    const float click = click_ * n;
    click_ *= click_decay_;
    const float v = 15000.0f * y1 + 7000.0f * y2 + 5000.0f * click;
    return int16_t(v > 20000 ? 20000 : v < -20000 ? -20000 : v);
}

} // namespace qbert_sound
