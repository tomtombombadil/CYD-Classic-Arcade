// The touch screen, through LovyanGFX (XPT2046 resistive or FT6336
// capacitive, from the board file), in the panel's current rotation.
//
// A press starts only after two readings agree within 8 px and ends after
// two empty readings (resistive panels jitter and drop out; from CYD
// Classic Games). Menus act on the release, at the point where the press
// started (lift-off readings drift).
#pragma once

#include <cstdint>

namespace touch {

// After screen::init(). Resistive boards: the saved calibration, or the
// calibration screen now (first start, or the BOOT button held at power-on).
void init();

// The calibration screen (tap four corner arrows), saved in NVS.
void calibrate();
bool needs_calibration();   // resistive panels

struct Event {
    enum Kind : uint8_t { None, Down, Up } kind = None;
    int x = 0, y = 0;       // Up: where the press started
};
// Menus: call every loop.
Event poll();

// Games: true once the screen has been held for a second (to leave a game
// without a controller). Reads the panel every 4th call.
bool held_long();

// Wait until nothing touches the screen.
void wait_release();

// A message on screen stays until it's dismissed (Tom, v0.14.6: a crash
// report gone in 8 s can't be copied): waits for a tap or a controller
// button, after both are let go (a press already held doesn't count).
void wait_tap_or_button();

}  // namespace touch
