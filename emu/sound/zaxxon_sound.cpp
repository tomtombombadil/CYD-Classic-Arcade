// Zaxxon sound board models - see zaxxon_sound.h.
//
// Facts (MAME's driver and service notes): all sounds are discrete
// circuits, each started by an active-low 8255 output; the engine, homing
// missile, force field and battleship sound while their output is low;
// the base missile, both explosions, the cannon, the enemy shot and the
// alarms are one shots started by a falling edge (the big explosion and
// the low fuel alarm only when not already sounding); bits 0-1 of port A
// set the engine's level (MAME: 0.5 + 0.157 x the value); the board has
// two 555 noise generators that feed the explosions.
//
// Assumed (our own design, after how the game sounds - the circuits' part
// values weren't to be had): every pitch, time constant and filter below,
// and each sound's level, set for a small speaker so the loudest (the big
// explosion) peaks ~20,000 and a busy moment of play stays under ~21,000.
#include "zaxxon_sound.h"

#include <cmath>

namespace zaxxon_sound {

namespace {

constexpr float kTwoPi = 6.2831853f;

// Levels (int16 per unit of each model's swing).
constexpr float kEngineLevel = 2600, kHomingLevel = 3200, kBaseMissileLevel = 5000;
constexpr float kLaserLevel = 2800, kBattleshipLevel = 4200, kSmallExpLevel = 13000;
constexpr float kBigExpLevel = 22000, kCannonLevel = 5200, kShotLevel = 4200, kAlarmLevel = 3200;

// One shots' lengths (s).
constexpr float kBaseMissileTime = 1.4f, kCannonTime = 0.16f, kShotTime = 0.22f;
constexpr float kAlarm2Time = 0.42f, kAlarm3Time = 1.2f, kBigExpTime = 2.6f;

// A filter state that has died away becomes exactly 0, so idle filters
// can be skipped (and never reach slow denormal floats).
inline float settle(float v) { return (v > -1e-6f && v < 1e-6f) ? 0.0f : v; }

// A square wave of phase p (0-1): +1 / -1.
inline float square(float p) { return p < 0.5f ? 1.0f : -1.0f; }

// Steps a 17-bit shift register (taps 17 and 14) and returns its output as
// +1 / -1.
inline float step_lfsr(uint32_t& r)
{
    const uint32_t fb = ((r >> 16) ^ (r >> 13)) & 1;
    r = ((r << 1) | fb) & 0x1ffff;
    return (r & 1) ? 1.0f : -1.0f;
}

} // namespace

float Board::lp(float fc) const { return 1.0f - std::exp(-kTwoPi * fc / rate_); }
float Board::decay(float tau) const { return std::exp(-1.0f / (tau * rate_)); }

void Board::init(uint32_t rate)
{
    rate_ = float(rate ? rate : 24000);
    n1_step_ = uint32_t(15000.0f / rate_ * 4294967296.0f);
    n2_step_ = uint32_t(9000.0f / rate_ * 4294967296.0f);
    ea_c_ = lp(280.0f);
    eb_f_ = 2.0f * std::sin(3.14159265f * 900.0f / rate_);     // band-pass 900 Hz, Q 1.5
    eb_q_ = 1.0f / 1.5f;
    eng_attack_ = 1.0f - decay(0.05f);
    sx_c_ = lp(1500.0f);
    sx_decay_ = decay(0.22f);
    bx_c_ = lp(600.0f);
    bx_decay_ = decay(0.75f);
    dc_r_ = 1.0f - kTwoPi * 20.0f / rate_;
    reset();
}

void Board::reset()
{
    a_ = b_ = c_ = 0xff;
    for (uint32_t& t : triggers_) t = 0;
    lfsr1_ = lfsr2_ = 1;
    n1_phase_ = n2_phase_ = 0;
    noise1_ = noise2_ = 0;
    ea_lp1_ = ea_lp2_ = eb_bp_ = eb_bpv_ = 0;
    eng_level_ = eng_a_amp_ = eng_b_amp_ = 0;
    hm_phase_ = hm_lfo_ = hm_amp_ = 0;
    bm_t_ = 9;
    bm_phase_ = bm_amp_ = 0;
    lz_phase_ = lz_mod_ = lz_amp_ = 0;
    bs_phase_ = bs_lfo_ = bs_amp_ = 0;
    sx_env_ = sx_lp_ = 0;
    bx_env_ = bx_lp_ = bx_lp2_ = 0;
    cn_t_ = sh_t_ = a2_t_ = a3_t_ = 9;
    cn_phase_ = cn_env_ = sh_phase_ = sh_env_ = a2_phase_ = a3_phase_ = 0;
    dc_x_ = dc_y_ = 0;
}

void Board::trigger(Sound s)
{
    ++triggers_[s];
    switch (s) {
    case kBaseMissile: bm_t_ = 0; bm_amp_ = 1; break;
    case kSmallExp:    sx_env_ = 1; break;
    case kBigExp:      bx_env_ = 1; break;
    case kCannon:      cn_t_ = 0; cn_env_ = 1; break;
    case kShot:        sh_t_ = 0; sh_env_ = 1; break;
    case kAlarm2:      a2_t_ = 0; break;
    case kAlarm3:      a3_t_ = 0; break;
    default: break;
    }
}

bool Board::active(Sound s) const
{
    switch (s) {
    case kEngineA:     return !(a_ & 0x04);
    case kEngineB:     return !(a_ & 0x08);
    case kHoming:      return !(a_ & 0x10);
    case kBaseMissile: return bm_t_ < kBaseMissileTime;
    case kLaser:       return !(a_ & 0x40);
    case kBattleship:  return !(a_ & 0x80);
    case kSmallExp:    return sx_env_ > 0.01f;
    case kBigExp:      return bx_env_ > 0.01f;
    case kCannon:      return cn_t_ < kCannonTime;
    case kShot:        return sh_t_ < kShotTime;
    case kAlarm2:      return a2_t_ < kAlarm2Time;
    case kAlarm3:      return a3_t_ < kAlarm3Time;
    default:           return false;
    }
}

// A falling edge (1 -> 0) on an output starts its one shot.
void Board::write_a(uint8_t v)
{
    const uint8_t fell = uint8_t(a_ & ~v);
    a_ = v;
    if (fell & 0x20) trigger(kBaseMissile);
}

void Board::write_b(uint8_t v)
{
    const uint8_t fell = uint8_t(b_ & ~v);
    b_ = v;
    if (fell & 0x10) trigger(kSmallExp);
    if ((fell & 0x20) && !active(kBigExp)) trigger(kBigExp);
    if (fell & 0x80) trigger(kCannon);
}

void Board::write_c(uint8_t v)
{
    const uint8_t fell = uint8_t(c_ & ~v);
    c_ = v;
    if (fell & 0x01) trigger(kShot);
    if (fell & 0x04) trigger(kAlarm2);
    if ((fell & 0x08) && !active(kAlarm3)) trigger(kAlarm3);
}

void Board::render(int16_t* out, int n)
{
    const float dt = 1.0f / rate_;
    // Held sounds fade in and out over ~50 ms (the analog switches feed RC
    // filters), so nothing clicks.
    const float eng_target = 0.5f + 0.157f * float(a_ & 3);
    const float ea_on = (a_ & 0x04) ? 0.0f : 1.0f, eb_on = (a_ & 0x08) ? 0.0f : 1.0f;
    const float hm_on = (a_ & 0x10) ? 0.0f : 1.0f, lz_on = (a_ & 0x40) ? 0.0f : 1.0f;
    const float bs_on = (a_ & 0x80) ? 0.0f : 1.0f;
    for (int i = 0; i < n; ++i) {
        // Noise sources.
        uint32_t p = n1_phase_;
        n1_phase_ += n1_step_;
        if (n1_phase_ < p) noise1_ = step_lfsr(lfsr1_);
        p = n2_phase_;
        n2_phase_ += n2_step_;
        if (n2_phase_ < p) noise2_ = step_lfsr(lfsr2_);

        float mix = 0;

        // Engines.
        eng_level_ += (eng_target - eng_level_) * eng_attack_;
        eng_a_amp_ = settle(eng_a_amp_ + (ea_on - eng_a_amp_) * eng_attack_);
        eng_b_amp_ = settle(eng_b_amp_ + (eb_on - eng_b_amp_) * eng_attack_);
        if (eng_a_amp_ != 0 || ea_lp2_ != 0) {
            ea_lp1_ = settle(ea_lp1_ + (noise1_ * eng_a_amp_ - ea_lp1_) * ea_c_);
            ea_lp2_ = settle(ea_lp2_ + (ea_lp1_ - ea_lp2_) * ea_c_);
            mix += ea_lp2_ * 2.2f * eng_level_ * kEngineLevel;
        }
        if (eng_b_amp_ != 0 || eb_bp_ != 0 || eb_bpv_ != 0) {
            // State-variable band-pass.
            const float hp = noise2_ * eng_b_amp_ - eb_bp_ * eb_q_ - eb_bpv_;
            eb_bp_ = settle(eb_bp_ + eb_f_ * hp);
            eb_bpv_ = settle(eb_bpv_ + eb_f_ * eb_bp_);
            mix += eb_bp_ * 0.8f * eng_level_ * kEngineLevel;
        }

        // Homing missile: 700-1300 Hz, swept up and down 7 times a second.
        hm_amp_ = settle(hm_amp_ + (hm_on - hm_amp_) * eng_attack_);
        if (hm_amp_ != 0) {
            hm_lfo_ += 7.0f * dt;
            if (hm_lfo_ >= 1) hm_lfo_ -= 1;
            const float tri = hm_lfo_ < 0.5f ? hm_lfo_ * 2 : 2 - hm_lfo_ * 2;
            hm_phase_ += (700.0f + 600.0f * tri) * dt;
            if (hm_phase_ >= 1) hm_phase_ -= 1;
            mix += square(hm_phase_) * hm_amp_ * kHomingLevel;
        }

        // Base missile: a whoosh climbing away - 300 Hz up to 1500 Hz over
        // the first second, with noise, fading out.
        if (bm_t_ < kBaseMissileTime) {
            const float f = 300.0f + 1200.0f * (1.0f - std::exp(-bm_t_ * 2.5f));
            bm_phase_ += f * dt;
            if (bm_phase_ >= 1) bm_phase_ -= 1;
            const float env = std::exp(-bm_t_ * 1.6f) * (bm_t_ < 0.02f ? bm_t_ * 50.0f : 1.0f);
            mix += (0.7f * square(bm_phase_) + 0.5f * noise1_) * env * kBaseMissileLevel;
            bm_t_ += dt;
        }

        // Force field: a 160 Hz buzz switched to 210 Hz 35 times a second.
        lz_amp_ = settle(lz_amp_ + (lz_on - lz_amp_) * eng_attack_);
        if (lz_amp_ != 0) {
            lz_mod_ += 35.0f * dt;
            if (lz_mod_ >= 1) lz_mod_ -= 1;
            lz_phase_ += (lz_mod_ < 0.5f ? 160.0f : 210.0f) * dt;
            if (lz_phase_ >= 1) lz_phase_ -= 1;
            // A sawtooth's edge, for the rasp.
            mix += (2.0f * lz_phase_ - 1.0f) * lz_amp_ * kLaserLevel;
        }

        // Battleship: a 95 Hz square throbbing 3 times a second.
        bs_amp_ = settle(bs_amp_ + (bs_on - bs_amp_) * eng_attack_);
        if (bs_amp_ != 0) {
            bs_lfo_ += 3.0f * dt;
            if (bs_lfo_ >= 1) bs_lfo_ -= 1;
            const float tri = bs_lfo_ < 0.5f ? bs_lfo_ * 2 : 2 - bs_lfo_ * 2;
            bs_phase_ += (95.0f + 15.0f * tri) * dt;
            if (bs_phase_ >= 1) bs_phase_ -= 1;
            mix += square(bs_phase_) * (0.35f + 0.65f * tri) * bs_amp_ * kBattleshipLevel;
        }

        // Small explosion: noise 1 through 1.5 kHz, 0.22 s decay.
        if (sx_env_ != 0 || sx_lp_ != 0) {
            sx_lp_ = settle(sx_lp_ + (noise1_ * sx_env_ - sx_lp_) * sx_c_);
            sx_env_ = settle(sx_env_ * sx_decay_);
            mix += sx_lp_ * kSmallExpLevel;
        }

        // Big explosion: noise 2 through two 600 Hz poles, 0.75 s decay,
        // cut after 2.6 s.
        if (bx_env_ != 0 || bx_lp2_ != 0) {
            bx_lp_ = settle(bx_lp_ + (noise2_ * bx_env_ - bx_lp_) * bx_c_);
            bx_lp2_ = settle(bx_lp2_ + (bx_lp_ - bx_lp2_) * bx_c_);
            bx_env_ = bx_env_ < 0.03f ? settle(bx_env_ * 0.999f) : bx_env_ * bx_decay_;
            mix += bx_lp2_ * 1.6f * kBigExpLevel;
        }

        // Cannon: 2400 Hz falling to ~500 Hz in 0.16 s, a little noise.
        if (cn_t_ < kCannonTime) {
            cn_phase_ += (500.0f + 1900.0f * std::exp(-cn_t_ * 22.0f)) * dt;
            if (cn_phase_ >= 1) cn_phase_ -= 1;
            const float env = 1.0f - cn_t_ / kCannonTime;
            mix += (square(cn_phase_) + 0.3f * noise1_) * env * kCannonLevel;
            cn_t_ += dt;
        }

        // Enemy shot: 1100 Hz falling to ~250 Hz in 0.22 s.
        if (sh_t_ < kShotTime) {
            sh_phase_ += (250.0f + 850.0f * std::exp(-sh_t_ * 14.0f)) * dt;
            if (sh_phase_ >= 1) sh_phase_ -= 1;
            const float env = 1.0f - sh_t_ / kShotTime;
            mix += square(sh_phase_) * env * kShotLevel;
            sh_t_ += dt;
        }

        // Alarm 2 (target lock): three 1300 Hz beeps, 70 ms on / 70 off.
        if (a2_t_ < kAlarm2Time) {
            a2_phase_ += 1300.0f * dt;
            if (a2_phase_ >= 1) a2_phase_ -= 1;
            if (std::fmod(a2_t_, 0.14f) < 0.07f) mix += square(a2_phase_) * kAlarmLevel;
            a2_t_ += dt;
        }

        // Alarm 3 (low fuel): 750 / 560 Hz, switched every 0.12 s.
        if (a3_t_ < kAlarm3Time) {
            a3_phase_ += (std::fmod(a3_t_, 0.24f) < 0.12f ? 750.0f : 560.0f) * dt;
            if (a3_phase_ >= 1) a3_phase_ -= 1;
            mix += square(a3_phase_) * kAlarmLevel;
            a3_t_ += dt;
        }

        // Output coupling capacitor, then a soft knee above 16,000 so a
        // pile-up of sounds never clips.
        const float y = mix - dc_x_ + dc_r_ * dc_y_;
        dc_x_ = mix;
        dc_y_ = settle(y);
        float s = y;
        if (s > 16000.0f) s = 16000.0f + (s - 16000.0f) / (1.0f + (s - 16000.0f) / 5500.0f);
        if (s < -16000.0f) s = -16000.0f + (s + 16000.0f) / (1.0f - (s + 16000.0f) / 5500.0f);
        out[i] = int16_t(s);
    }
}

} // namespace zaxxon_sound
