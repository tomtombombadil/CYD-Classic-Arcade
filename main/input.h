// Both controllers as one: what's held, what was just pressed (with key
// repeat for the d-pad in menus), what the game gets, and the "back"
// gesture: hold Select and press Start twice (as on most retro handhelds,
// Tom), or Home.
//
// Select is the coin button, but it's also the hotkey: while it's held,
// Start goes to the gesture, not the game, and the coin only drops when
// Select is let go without Start having been pressed.
#pragma once

#include "pad.h"

#include <cstdint>

class Input {
public:
    // Reads the controllers; call once per loop (once per frame in games).
    void update(uint32_t now_ms);

    uint32_t held() const { return held_; }
    // Newly pressed this update; the d-pad repeats while held (menus).
    uint32_t pressed() const { return pressed_; }
    // The buttons a game should see this update (Select / Start filtered
    // as above; a coin is a few updates long).
    uint32_t game() const { return game_; }
    // The back gesture completed this update.
    bool back() const { return back_; }
    int  connected() const { return connected_; }

    // Ignore every button until all are let go (after leaving a game: the
    // Start that ended it must not start the next one).
    void wait_for_release() { wait_release_ = true; }

private:
    uint32_t held_ = 0, prev_ = 0, pressed_ = 0, game_ = 0;
    uint32_t repeat_at_ = 0;
    // hotkey state
    bool     hotkey_used_ = false;   // Start pressed during this Select hold
    int      start_presses_ = 0;    // -1: the gesture fired; Start is ignored until Select is let go
    uint32_t first_start_ = 0;
    int      coin_left_ = 0;         // updates the coin stays pressed
    bool     back_ = false;
    bool     wait_release_ = false;
    int      connected_ = 0;
};
