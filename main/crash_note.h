// What the board was doing, kept in memory that survives a reset (RTC), so
// a crash or watchdog reset can be named at the next start: "Dig Dug
// stopped the board while loading its ROMs (31 KB free)". The panic's own
// report still goes to the serial port; this is what the player sees.
#pragma once

#include <cstddef>

namespace crash_note {

// The game (title) and the step it's at ("making the machine", ...);
// also notes the free RAM. Cheap: call at each step of a game's start.
void step(const char* game, const char* what);
// Back in the menu, nothing risky going on
void clear();

// At start-up: true (and a line for the screen in `out`) when the last
// reset was a crash, a watchdog or a brownout. Clears the note.
bool last(char* out, size_t cap);

}  // namespace crash_note
