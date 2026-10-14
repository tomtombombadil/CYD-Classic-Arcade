// Both controllers as one (see input.h).
#include "input.h"

namespace {

constexpr uint32_t kDpad = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
constexpr uint32_t kBackHoldMs = 1000;
constexpr uint32_t kRepeatFirstMs = 400, kRepeatMs = 120;

}  // namespace

void Input::update(uint32_t now)
{
    uint32_t h = 0;
    int conn = 0;
    for (int i = 0; i < PAD_MAX; ++i) {
        pad_state_t p;
        pad_get(i, &p);
        if (p.connected) { h |= p.buttons; ++conn; }
    }
    connected_ = conn;
    prev_ = held_;
    held_ = h;
    pressed_ = held_ & ~prev_;
    // d-pad repeat
    if (pressed_ & kDpad) {
        repeat_at_ = now + kRepeatFirstMs;
    } else if ((held_ & kDpad) && int32_t(now - repeat_at_) >= 0) {
        pressed_ |= held_ & kDpad;
        repeat_at_ = now + kRepeatMs;
    }
    // back: Home, or Start + Select held for a second
    back_ = (pressed_ & PAD_HOME) != 0;
    const bool combo = (held_ & (PAD_START | PAD_SELECT)) == (PAD_START | PAD_SELECT);
    if (!combo) {
        combo_since_ = now;
        combo_done_ = false;
    } else if (!combo_done_ && now - combo_since_ >= kBackHoldMs) {
        combo_done_ = true;
        back_ = true;
    }
}
