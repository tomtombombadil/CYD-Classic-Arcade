// Both controllers as one: what's held, what was just pressed (with key
// repeat for the d-pad in menus), and the "back" gesture: Start + Select
// held together for a second (Home isn't on every controller), or Home.
#pragma once

#include "pad.h"

#include <cstdint>

class Input {
public:
    // Reads the controllers; call once per loop.
    void update(uint32_t now_ms);

    uint32_t held() const { return held_; }
    // Newly pressed this update; the d-pad repeats while held (menus).
    uint32_t pressed() const { return pressed_; }
    // The back gesture completed this update (reported once per hold).
    bool back() const { return back_; }
    int  connected() const { return connected_; }

private:
    uint32_t held_ = 0, prev_ = 0, pressed_ = 0;
    uint32_t combo_since_ = 0, repeat_at_ = 0;
    bool     combo_done_ = false, back_ = false;
    int      connected_ = 0;
};
