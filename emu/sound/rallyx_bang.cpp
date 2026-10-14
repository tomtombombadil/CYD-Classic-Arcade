// Rally-X crash sound model - see rallyx_bang.h.
#include "rallyx_bang.h"

#include <cmath>

namespace rallyx_bang {

namespace {

constexpr float kTwoPi = 6.2831853f;

// The 4V clock: 6.144 MHz pixel clock / 384 per line / 8 lines.
constexpr float kNoiseClock = 2000.0f;
// Charging through the analog switch's on-resistance (~500 ohm x 1 uF).
constexpr float kChargeTau = 0.0005f;
// Discharge: 150k + 22k x 1 uF, only while the noise switch is closed.
constexpr float kDischargeTau = 172e3f * 1e-6f;
// Filters (see the header): source ~19k into 10 nF; 470k x 10 nF; ~760 ohm
// x 47 nF at the mixing node.
constexpr float kSourceLowPass = 840.0f;
constexpr float kHighPass = 34.0f;
constexpr float kNodeLowPass = 4400.0f;
// Output level for the capacitor fully charged: loud next to the music, as
// a crash is on the cabinet, with room left for the wavetable voices.
constexpr float kLevel = 26000.0f;
// Below this everything is quiet: stop computing.
constexpr float kQuiet = 1e-4f;

} // namespace

// 18 stages, feedback from stages 18 and 11 (x^18 + x^11 + 1, a maximal
// length sequence: the register never gets stuck, as the board's reset
// diode makes sure of).
uint32_t Bang::lfsr_next(uint32_t r)
{
    const uint32_t fb = ((r >> 17) ^ (r >> 10)) & 1;
    r = ((r << 1) | fb) & 0x3ffff;
    return r ? r : 1;
}

void Bang::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    clk_step_ = uint32_t(double(kNoiseClock) / rate_ * 4294967296.0);
    charge_ = 1.0f - std::exp(-1.0f / (kChargeTau * rate_));
    discharge_ = std::exp(-1.0f / (kDischargeTau * rate_));
    lp_c_ = 1.0f - std::exp(-kTwoPi * kSourceLowPass / rate_);
    hp_r_ = std::exp(-kTwoPi * kHighPass / rate_);
    out_c_ = 1.0f - std::exp(-kTwoPi * kNodeLowPass / rate_);
    reset();
}

void Bang::reset()
{
    trigger_ = false;
    lfsr_ = 1;
    clk_phase_ = 0;
    cap_ = lp_ = hp_x_ = hp_y_ = out_lp_ = 0;
}

bool Bang::active() const
{
    return trigger_ || cap_ > kQuiet || std::fabs(lp_) > kQuiet || std::fabs(hp_y_) > kQuiet ||
           std::fabs(out_lp_) > kQuiet;
}

void Bang::mix(int16_t* out, int n)
{
    if (!active()) return;
    for (int i = 0; i < n; ++i) {
        const uint32_t p = clk_phase_ + clk_step_;
        if (p < clk_phase_) lfsr_ = lfsr_next(lfsr_);
        clk_phase_ = p;
        const bool closed = (lfsr_ >> 17) & 1;

        if (trigger_)    cap_ += (1.0f - cap_) * charge_;
        else if (closed) cap_ *= discharge_;
        const float x = closed ? cap_ : 0.0f;

        lp_ += (x - lp_) * lp_c_;
        const float y = hp_r_ * (hp_y_ + lp_ - hp_x_);
        hp_x_ = lp_;
        hp_y_ = y;
        out_lp_ += (y - out_lp_) * out_c_;

        int s = out[i] + int(out_lp_ * kLevel);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = int16_t(s);
    }
    // No denormals in the silence.
    if (!trigger_ && cap_ < kQuiet && std::fabs(hp_y_) < kQuiet && std::fabs(out_lp_) < kQuiet)
        cap_ = lp_ = hp_x_ = hp_y_ = out_lp_ = 0;
}

void Bang::render(int16_t* out, int n)
{
    for (int i = 0; i < n; ++i) out[i] = 0;
    mix(out, n);
}

} // namespace rallyx_bang
