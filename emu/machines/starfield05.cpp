// Namco 05XX starfield - see starfield05.h.
#include "starfield05.h"

namespace starfield05 {

namespace {

// Steps added to (or dropped from) a frame per SCROLL_X value: 0-3 add
// 0-3 steps (the stars move 1-4 pixels against the scan), 4-7 drop 4-1
// (3, 2, 1, 0 pixels with it); SCROLL_Y = 0 runs 22 lines before the
// visible 224 and 10 after.
constexpr int kScrollSteps[8] = {0, 1, 2, 3, -4, -3, -2, -1};

} // namespace

void Generator::init()
{
    uint16_t s = kSeed;
    int n = 0;
    for (int i = 0; i < kPeriod; ++i) {
        if (is_star(s) && n < kStars) hits_[n++] = {uint16_t(i), star_set(s), star_colour(s)};
        s = step(s);
    }
    pos_ = 0;
}

void Generator::set_controls(uint8_t scroll_x, uint8_t set_a, uint8_t set_b, bool enabled)
{
    pre_ = 22 * kWidth + kScrollSteps[scroll_x & 7];
    post_ = 10 * kWidth;
    set_a_ = set_a & 3;
    set_b_ = set_b & 3;
    if (!enabled) pos_ = 0;
    enabled_ = enabled;
}

int Generator::frame(Star* out)
{
    if (!enabled_) return 0;
    // Visible steps run from `start` for 224 lines of 256.
    const int start = (pos_ + pre_) % kPeriod;
    constexpr int kVisible = kVisibleLines * kWidth;
    int n = 0;
    for (const Hit& h : hits_) {
        int rel = int(h.index) - start;
        if (rel < 0) rel += kPeriod;
        if (rel >= kVisible || !h.colour || (h.set != set_a_ && h.set != set_b_)) continue;
        if (n == kMaxShown) break;
        out[n++] = {uint8_t(rel % kWidth), uint8_t(rel / kWidth), h.colour};
    }
    pos_ = (pos_ + pre_ + kVisible + post_) % kPeriod;
    return n;
}

} // namespace starfield05
