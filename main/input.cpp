// Both controllers as one (see input.h).
#include "input.h"

namespace {

constexpr uint32_t kDpad = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
constexpr uint32_t kRepeatFirstMs = 400, kRepeatMs = 120;
constexpr uint32_t kDoublePressMs = 2000;   // both Start presses within this
constexpr int      kCoinUpdates = 6;        // ~0.1 s in a game

}  // namespace

void Input::update(uint32_t now)
{
    uint32_t h = 0;
    int conn = 0;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    for (int i = 0; i < PAD_MAX; ++i) {
        pad_state_t p;
        pad_get(i, &p);
        if (p.connected) {
            h |= p.buttons;
            ++conn;
            lx += p.lx; ly += p.ly; rx += p.rx; ry += p.ry;
        }
    }
    auto clamp = [](int v) { return int16_t(v < -512 ? -512 : v > 511 ? 511 : v); };
    analog_.lx = clamp(lx);
    analog_.ly = clamp(ly);
    analog_.rx = clamp(rx);
    analog_.ry = clamp(ry);
    connected_ = conn;
    prev_ = held_;
    held_ = h;
    pressed_ = held_ & ~prev_;
    const uint32_t released = prev_ & ~held_;
    if (wait_release_) {
        if (held_) {
            pressed_ = 0;
            game_ = 0;
            back_ = false;
            return;
        }
        wait_release_ = false;
        hotkey_used_ = false;
        start_presses_ = 0;
        coin_left_ = 0;
    }

    // d-pad repeat (menus)
    if (pressed_ & kDpad) {
        repeat_at_ = now + kRepeatFirstMs;
    } else if ((held_ & kDpad) && int32_t(now - repeat_at_) >= 0) {
        pressed_ |= held_ & kDpad;
        repeat_at_ = now + kRepeatMs;
    }

    // back: Home, or Select held + Start pressed twice
    back_ = (pressed_ & PAD_HOME) != 0;
    if (held_ & PAD_SELECT) {
        if ((pressed_ & PAD_START) && start_presses_ >= 0) {
            hotkey_used_ = true;
            if (start_presses_ == 0 || now - first_start_ > kDoublePressMs) {
                start_presses_ = 1;
                first_start_ = now;
            } else {
                start_presses_ = -1;    // no more until Select is let go
                back_ = true;
            }
        }
    } else {
        // Select let go: a coin, unless it was used as the hotkey
        if ((released & PAD_SELECT) && !hotkey_used_) coin_left_ = kCoinUpdates;
        hotkey_used_ = false;
        start_presses_ = 0;
    }

    game_ = held_ & ~(PAD_SELECT | PAD_HOME);
    if (held_ & PAD_SELECT) game_ &= ~PAD_START;
    if (coin_left_ > 0) {
        game_ |= PAD_SELECT;
        --coin_left_;
    }
}
