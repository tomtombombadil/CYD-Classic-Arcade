// Battlezone sound board models - see bzone_sound.h.
//
// Part values are the board's (R5-R35, C9-C29 as in the parts list); the
// engine's frequency pot R11 (250k) is an operator adjustment - set here
// to 36k, where MAME's default setting puts it (an assumption). Levels: on
// the board the explosion is the loudest by far and the engine sits below
// 80 Hz, where a small speaker gives little - so each sound's level here
// is set for the CYD's speaker (as for Asteroids): explosion peaks
// ~17,000, shell ~14,000, engine ~7,000, POKEY tunes ~8,000; together
// they stay under ~22,000 (a soft knee above 16,000).
#include "bzone_sound.h"

#include <cmath>

namespace bzone_sound {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kNoiseHz = 6000.0f;         // 12 kHz halved by a 74LS109
constexpr float kTtl = 3.4f;                // a TTL output's high level

// The shell / explosion circuits: a cap charged through a CD4066 (270
// ohm) while its bit is on, discharged through the other switch and 22k
// + 1k while the noise bit is high; the op-amp's + input is the 1k's end.
constexpr float kRon = 270.0f;
constexpr float kRDis = 22e3f + 1e3f;       // R14 + R15 (shell), R17 + R16 (explosion)
constexpr float kTap = 1e3f / (kRon + kRDis);
constexpr float kCShell = 4.7e-6f;          // C9
constexpr float kCExplode = 10e-6f;         // C14
// Op-amp gain 1 + R32 / (R13 + R12), or with R12 shorted by a 4066 (loud):
// 1 + 330k / 43k, 1 + 330k / (10k + 270 || 33k). The explosion's (R33,
// R18, R19) are the same values.
constexpr float kGainSoft = 1.0f + 330e3f / 43e3f;
constexpr float kGainLoud = 1.0f + 330e3f / (10e3f + kRon * 33e3f / (kRon + 33e3f));
constexpr float kRail = 20.5f;              // the op-amp's output on 22 V
constexpr float kAmpTau = 330e3f * 4.7e-9f; // R32 / C21: 103 Hz

// Engine: the rev switch picks the control voltage's target (R6 4.7k
// against R7 1k, or with R5 1k + 270 ohm across R6), which the 10 uF C13
// follows through R8 100k - or R8 || R9 (22k) when rising past the diode.
constexpr float kCvIdle = 5.0f * 4.7e3f / (1e3f + 4.7e3f);
constexpr float kRevPar = (1e3f + kRon) * 4.7e3f / (1e3f + kRon + 4.7e3f);
constexpr float kCvRev = 5.0f * kRevPar / (1e3f + kRevPar);
// The 555 (R10 100k, R11 36k, C11 15 nF) charges from CV / 2 to CV
// through R10 + R11 and discharges through R11.
constexpr float kR10 = 100e3f, kR11 = 36e3f, kC11 = 15e-9f;
// The four counter outputs through 33k each (R20, R21, R34, R35) onto
// C29 0.47 uF.
constexpr float kEngTau = 33e3f / 4 * 0.47e-6f;

// The final mixer's resistors: shell R25 100k, explosion R28 100k, engine
// R26 33k + R20 / 4, POKEY R27 330k - as conductances, then one gain to
// int16 per sound (see the levels above).
constexpr float kShellLevel = 1.0f / 100e3f * 5.9e8f;
constexpr float kExplodeLevel = 1.0f / 100e3f * 5.6e8f;
constexpr float kEngineLevel = 1.0f / 41.25e3f * 5.0e8f;
constexpr float kPokeyLevel = 1.0f / 330e3f * 1.5e9f;
constexpr float kKnee = 16000.0f, kKneeSpan = 8000.0f;

// A filter state that has died away becomes exactly 0, so idle filters
// never reach slow denormal floats.
inline float settle(float v) { return (v > -1e-6f && v < 1e-6f) ? 0.0f : v; }

} // namespace

void Board::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    noise_step_ = uint32_t(kNoiseHz / rate_ * 4294967296.0f);
    sh_decay_ = std::exp(-1.0f / (kRDis + kRon) / kCShell / rate_);
    ex_decay_ = std::exp(-1.0f / (kRDis + kRon) / kCExplode / rate_);
    sh_charge_ = 1.0f - std::exp(-1.0f / (kRon * kCShell * rate_));
    ex_charge_ = 1.0f - std::exp(-1.0f / (kRon * kCExplode * rate_));
    amp_lp_ = 1.0f - std::exp(-1.0f / (kAmpTau * rate_));
    eng_fall_ = 1.0f - std::exp(-1.0f / (100e3f * 10e-6f * rate_));
    eng_rise_ = 1.0f - std::exp(-1.0f / (100e3f * 22e3f / 122e3f * 10e-6f * rate_));
    eng_lp_ = 1.0f - std::exp(-1.0f / (kEngTau * rate_));
    pk_lp_ = 1.0f - std::exp(-1.0f / (10e3f * 0.015e-6f * rate_));       // 1061 Hz
    dc_r_ = std::exp(-1.0f / (22e3f * 0.1e-6f * rate_));                  // 72 Hz
    reset();
}

void Board::reset()
{
    latch_ = 0;
    lfsr_ = 0;
    noise_phase_ = 0;
    shell_ff_ = explode_ff_ = 0;
    last_out_ = last_nand_ = 0;
    sh_cap_ = ex_cap_ = 0;
    sh_out_ = ex_out_ = 0;
    eng_cv_ = 0;
    eng_phase_ = 0;
    cnt12_ = cnt10_ = 0;
    eng_node_ = 0;
    pk_out_ = 0;
    dc_x_ = dc_y_ = 0;
}

float Board::engine_hz() const
{
    // Thresholds CV / 2 and CV (kept below the 5 V supply).
    const float cv = eng_cv_ < 0.5f ? 0.5f : eng_cv_ > 4.9f ? 4.9f : eng_cv_;
    const float high = (kR10 + kR11) * kC11 * std::log((5.0f - cv / 2) / (5.0f - cv));
    const float low = kR11 * kC11 * 0.6931472f;
    return 1.0f / (high + low);
}

void Board::render(int16_t* io, int n)
{
    const uint8_t v = latch_;
    const float sh_gain = (v & kShellLoud) ? kGainLoud : kGainSoft;
    const float ex_gain = (v & kExplodeLoud) ? kGainLoud : kGainSoft;
    const float cv_target = (v & kEngineRev) ? kCvRev : kCvIdle;
    const bool engine_on = v & kEngineOn;
    if (!engine_on) cnt12_ = cnt10_ = 0;            // the counters held clear
    float eng_step = engine_hz() / rate_;
    for (int i = 0; i < n; ++i) {
        // Noise register.
        const uint32_t np = noise_phase_;
        noise_phase_ += noise_step_;
        if (noise_phase_ < np) {
            const int fb = !(((lfsr_ >> 3) ^ (lfsr_ >> 14)) & 1);
            lfsr_ = uint16_t((lfsr_ << 1) | fb);
            const uint8_t out = uint8_t(lfsr_ >> 15);
            const uint8_t nand = (lfsr_ & 0x7800) != 0x7800;
            if (out && !last_out_) shell_ff_ ^= 1;
            if (nand && !last_nand_) explode_ff_ ^= 1;
            last_out_ = out;
            last_nand_ = nand;
        }

        // Shell and explosion: the caps, chopped by their noise bits.
        if (v & kShell) sh_cap_ += (5.0f - sh_cap_) * sh_charge_;
        else if (shell_ff_) sh_cap_ = settle(sh_cap_ * sh_decay_);
        if (v & kExplode) ex_cap_ += (5.0f - ex_cap_) * ex_charge_;
        else if (explode_ff_) ex_cap_ = settle(ex_cap_ * ex_decay_);
        float sh_in = shell_ff_ ? sh_cap_ * kTap * sh_gain : 0.0f;
        float ex_in = explode_ff_ ? ex_cap_ * kTap * ex_gain : 0.0f;
        if (sh_in > kRail) sh_in = kRail;
        if (ex_in > kRail) ex_in = kRail;
        sh_out_ = settle(sh_out_ + (sh_in - sh_out_) * amp_lp_);
        ex_out_ = settle(ex_out_ + (ex_in - ex_out_) * amp_lp_);

        // Engine: the control voltage, the 555 every 16 samples (it moves
        // slowly), the counters on its rising edges.
        const float d = cv_target - eng_cv_;
        eng_cv_ += d * (d > 0.5f ? eng_rise_ : eng_fall_);
        if ((i & 15) == 15) eng_step = engine_hz() / rate_;
        eng_phase_ += eng_step;
        if (eng_phase_ >= 1.0f) {
            eng_phase_ -= 1.0f;
            if (engine_on) {
                cnt12_ = cnt12_ == 15 ? 4 : uint8_t(cnt12_ + 1);
                cnt10_ = cnt10_ == 15 ? 6 : uint8_t(cnt10_ + 1);
            }
        }
        const int outs = (cnt12_ > 7) + (cnt12_ == 15) + (cnt10_ > 7) + (cnt10_ == 15);
        eng_node_ = settle(eng_node_ + (kTtl * float(outs) * 0.25f - eng_node_) * eng_lp_);

        // POKEY: 0-5 V at all four channels' full volume.
        pk_out_ = settle(pk_out_ + (float(io[i]) * (5.0f / 15360.0f) - pk_out_) * pk_lp_);

        // The mix, switched by the sound-on bit, then the coupling cap.
        const float x = (v & kSoundOn) ? sh_out_ * kShellLevel + ex_out_ * kExplodeLevel +
                                             eng_node_ * kEngineLevel + pk_out_ * kPokeyLevel
                                       : 0.0f;
        dc_y_ = settle((x - dc_x_ + dc_y_) * dc_r_);
        dc_x_ = x;
        // A soft knee above 16,000 (the sounds rarely meet at their peaks;
        // when they do, the sum stays under ~22,000 instead of clipping).
        float y = dc_y_;
        const float a = y < 0 ? -y : y;
        if (a > kKnee) {
            const float e = a - kKnee;
            const float k = kKnee + e * kKneeSpan / (e + kKneeSpan);
            y = y < 0 ? -k : k;
        }
        io[i] = int16_t(y);
    }
}

} // namespace bzone_sound
