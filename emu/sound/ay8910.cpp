// AY-3-8910 - see ay8910.h.
//
// run() doesn't step tick by tick: it finds the ticks until the next noise
// or envelope step, adds the levels over that stretch at once and jumps
// there. A tone's square wave in between is counted in closed form (its
// time high from its counter and period), so tones cost nothing per flip.
// And a run too short for any output to change (the usual case: a run per
// output sample) only adds the levels; the ticks it ran are put on the
// counters later, at once (settle()).
#include "ay8910.h"

namespace ay8910 {

// 4096 x 2^((n - 15) / 2): the data sheet's 1 / sqrt 2 per step.
const uint16_t kLevels[16] = {
    0, 32, 45, 64, 91, 128, 181, 256, 362, 512, 724, 1024, 1448, 2048, 2896, 4096,
};

namespace {

// Bits a register keeps.
constexpr uint8_t kMask[16] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff,
};

// Tone periods up to this count as their average (see update_levels).
constexpr uint32_t kFastPeriod = 4;
// The counters that can't be heard are brought up to date at least this
// often (in ticks), so catching up never takes long.
constexpr uint32_t kSyncTicks = 4096;

} // namespace

void Chip::reset()
{
    for (uint8_t& r : regs_) r = 0;
    latch_ = 0;
    active_ = true;
    for (int c = 0; c < 3; ++c) {
        tone_cnt_[c] = 0;
        tone_out_[c] = 0;
    }
    noise_cnt_ = 0;
    noise_pre_ = 0;
    lfsr_ = 1;
    env_cnt_ = 0;
    env_step_ = 15;
    env_attack_ = 0;
    env_hold_ = env_alt_ = env_holding_ = false;
    sub_ = 0;
    lazy_ = 0;
    budget_ = pend_ = 0;
    update_levels();
}

void Chip::write_address(uint8_t v)
{
    active_ = (v >> 4) == 0;
    if (active_) latch_ = v & 0x0f;
}

void Chip::write_data(uint8_t v)
{
    if (active_) write(latch_, v);
}

uint8_t Chip::read_data() const
{
    if (!active_) return 0xff;
    if (latch_ >= 14) {
        const int port = latch_ - 14;
        return (regs_[7] & (0x40 << port)) ? regs_[latch_] : port_in_[port];
    }
    return regs_[latch_];
}

uint8_t Chip::port_out(int port) const
{
    port &= 1;
    return (regs_[7] & (0x40 << port)) ? regs_[14 + port] : 0xff;
}

void Chip::write(int r, uint8_t v)
{
    sync();                                 // carried counters move under the old registers
    r &= 15;
    regs_[r] = uint8_t(v & kMask[r]);
    if (r == 13) {                          // a new shape restarts the envelope
        const uint8_t s = regs_[13];
        env_attack_ = (s & 0x04) ? 15 : 0;
        if (!(s & 0x08)) {                  // one ramp, then 0: as HOLD with ALT = ATT
            env_hold_ = true;
            env_alt_ = env_attack_ != 0;
        } else {
            env_hold_ = s & 0x01;
            env_alt_ = s & 0x02;
        }
        env_step_ = 15;
        env_cnt_ = 0;
        env_holding_ = false;
    }
    update_levels();
}

void Chip::update_levels()
{
    const uint8_t mix = regs_[7];
    const int noise = lfsr_ & 1;
    const int env = env_step_ ^ env_attack_;
    budget_ = 0;
    tone_live_ = 0;
    noise_live_ = env_live_ = false;
    for (int c = 0; c < 3; ++c) {
        // A tone above ~23 kHz (period 1-4) counts as its average, half
        // on: the output's sample-long averages hardly see it otherwise.
        const bool fast = !(mix & (1 << c)) && tone_period(c) <= kFastPeriod;
        const bool on = (((fast ? 1 : tone_out_[c]) | (mix >> c)) & (noise | (mix >> (3 + c))) & 1) != 0;
        const uint8_t a = regs_[8 + c];
        level_[c] = on ? kLevels[(a & 0x10) ? env : (a & 0x0f)] >> (fast ? 1 : 0) : 0;
        tone_hi_[c] = ((noise | (mix >> (3 + c))) & 1) ? kLevels[(a & 0x10) ? env : (a & 0x0f)] : 0;
        // What can change this channel's output: its tone, the noise, the
        // envelope - only those need exact timing in run().
        if (!(a & 0x1f)) continue;
        if (!(mix & (1 << c)) && !fast) tone_live_ |= uint8_t(1 << c);
        if (!(mix & (8 << c))) noise_live_ = true;
        if (a & 0x10) env_live_ = true;
    }
}

// Advances a counter that fires on reaching `period` (and restarts at 0)
// by `ticks`; returns how often it fired.
static inline uint32_t advance(uint32_t& cnt, uint32_t period, uint32_t ticks)
{
    const uint32_t first = cnt < period ? period - cnt : 1;
    if (ticks < first) {
        cnt += ticks;
        return 0;
    }
    const uint32_t t = ticks - first;
    if (t < period) {                       // the usual case: once
        cnt = t;
        return 1;
    }
    cnt = t % period;
    return 1 + t / period;
}

void Chip::advance_tone(int c, uint32_t period, uint32_t ticks) { advance(tone_cnt_[c], period, ticks); }

bool Chip::step_tone(int c, uint32_t period, uint32_t ticks)
{
    if (!(advance(tone_cnt_[c], period, ticks) & 1)) return false;
    tone_out_[c] ^= 1;
    return true;
}

bool Chip::step_noise(uint32_t period, uint32_t ticks)
{
    const uint32_t fired = advance(noise_cnt_, period, ticks);
    if (!fired) return false;
    // The divide-by-two: a register step on each firing that brings it back to 0.
    const uint32_t steps = noise_pre_ ? (fired + 1) / 2 : fired / 2;
    noise_pre_ ^= uint8_t(fired & 1);
    // Up to 14 steps at once: the new bits (bit 0 XOR bit 3, into bit 16)
    // only reach the taps after 14 steps, so they all come from the old
    // register.
    for (uint32_t left = steps; left;) {
        const uint32_t k = left < 12 ? left : 12;
        lfsr_ = (lfsr_ >> k) | (((lfsr_ ^ (lfsr_ >> 3)) & ((1u << k) - 1)) << (17 - k));
        left -= k;
    }
    return steps != 0;
}

bool Chip::step_env(uint32_t period, uint32_t ticks)
{
    if (env_holding_) return false;
    uint32_t steps = period ? advance(env_cnt_, period, ticks) : ticks;
    if (!steps) return false;
    if (uint32_t(env_step_) >= steps) {
        env_step_ = int8_t(env_step_ - steps);
    } else if (env_hold_) {                 // the ramp ended: hold
        if (env_alt_) env_attack_ ^= 15;
        env_holding_ = true;
        env_step_ = 0;
    } else {                                // wrapped: a new ramp (inverted if ALT)
        steps -= uint32_t(env_step_) + 1;
        if (env_alt_ && ((steps >> 4) & 1) == 0) env_attack_ ^= 15;
        env_step_ = int8_t(15 - (steps & 15));
    }
    return true;
}

// The ticks run in the quick path, on the live counters (none fires in
// them) - as run()'s loop would have moved them.
void Chip::settle()
{
    if (!pend_) return;
    const uint32_t t = pend_;
    pend_ = 0;
    for (int c = 0; c < 3; ++c)
        if (tone_live_ & (1 << c)) advance_tone(c, tone_period(c), t);
    if (noise_live_) step_noise(noise_period(), t);
    if (env_live_) step_env(env_period(), t);
    lazy_ += t;
}

// Input clocks until the next tone flip, noise step or envelope step that
// can change an output.
uint32_t Chip::next_change() const
{
    uint32_t d = 0xffffffu;                 // in ticks
    for (int c = 0; c < 3; ++c) {
        if (!(tone_live_ & (1 << c))) continue;
        const uint32_t p = tone_period(c);
        const uint32_t left = tone_cnt_[c] < p ? p - tone_cnt_[c] : 1;
        if (left < d) d = left;
    }
    if (noise_live_) {
        const uint32_t np = noise_period();
        const uint32_t left = (noise_cnt_ < np ? np - noise_cnt_ : 1) + (noise_pre_ ? 0 : np);
        if (left < d) d = left;
    }
    if (env_live_ && !env_holding_) {
        const uint32_t ep = env_period();
        const uint32_t left = env_cnt_ < ep ? ep - env_cnt_ : 1;
        if (left < d) d = left;
    }
    return (d - 1) * 8 + (8 - sub_);
}

void Chip::sync()
{
    settle();
    if (!lazy_) return;
    for (int c = 0; c < 3; ++c)
        if (!(tone_live_ & (1 << c))) step_tone(c, tone_period(c), lazy_);
    if (!noise_live_) step_noise(noise_period(), lazy_);
    if (!env_live_) step_env(env_period(), lazy_);
    lazy_ = 0;
}

// run() when an output can change in the run (else it only adds the levels).
void Chip::run_events(uint32_t clocks, uint32_t* acc)
{
    settle();
    // Registers don't change during a run.
    const uint32_t tp[3] = {tone_period(0), tone_period(1), tone_period(2)};
    const uint32_t np = noise_period();
    const uint32_t ep = env_period();
    while (clocks) {
        // Ticks until the next noise or envelope step (a period lowered
        // below its count fires on the next tick). Counters that can't
        // change the outputs (a channel's tone switched off, a silent
        // channel, no channel on the noise or the envelope) are carried
        // along later, by sync().
        uint32_t d = 0xffffffu;
        if (noise_live_) {
            // The register steps on every second firing of the counter.
            const uint32_t left = (noise_cnt_ < np ? np - noise_cnt_ : 1) + (noise_pre_ ? 0 : np);
            if (left < d) d = left;
        }
        if (env_live_ && !env_holding_) {
            const uint32_t left = env_cnt_ < ep ? ep - env_cnt_ : 1;
            if (left < d) d = left;
        }

        const uint32_t to_event = (d - 1) * 8 + (8 - sub_);
        const uint32_t take = clocks < to_event ? clocks : to_event;
        const uint32_t ticks = (sub_ + take) >> 3;
        for (int c = 0; c < 3; ++c) {
            if (!(tone_live_ & (1 << c))) {
                acc[c] += uint32_t(level_[c]) * take;
                continue;
            }
            // The square wave over `take` clocks: it flips at clock f0 (the
            // counter's next firing) and every 8 x period clocks after; m
            // flips in all. Its level is tone_hi_ while high, 0 while low.
            const uint32_t p = tp[c];
            const uint32_t first = tone_cnt_[c] < p ? p - tone_cnt_[c] : 1;
            const uint32_t f0 = (first - 1) * 8 + (8 - sub_);
            const uint32_t o = tone_out_[c];
            uint32_t high;
            if (take < f0) {
                high = o ? take : 0;
            } else {
                const uint32_t span = 8 * p;
                const uint32_t m = 1 + (take - f0) / span;
                const uint32_t last = take - f0 - (m - 1) * span;     // after the last flip
                high = (o ? f0 : 0) + (o ? (m - 1) / 2 : m / 2) * span + (((o ^ m) & 1) ? last : 0);
                tone_out_[c] = uint8_t(o ^ (m & 1));
                level_[c] = tone_out_[c] ? tone_hi_[c] : 0;
            }
            acc[c] += uint32_t(tone_hi_[c]) * high;
            if (ticks) advance_tone(c, p, ticks);
        }
        clocks -= take;
        sub_ = (sub_ + take) & 7;
        if (!ticks) break;                  // only when the clocks ran out

        bool changed = false;
        if (noise_live_) changed |= step_noise(np, ticks);
        if (env_live_) changed |= step_env(ep, ticks);
        lazy_ += ticks;
        if (lazy_ >= kSyncTicks) sync();    // moves only what isn't live
        if (changed) update_levels();
    }
    // At most kSyncTicks ahead, so the counters carried along stay near.
    const uint32_t next = next_change();
    budget_ = next < kSyncTicks * 8 ? next : kSyncTicks * 8;
}

} // namespace ay8910
