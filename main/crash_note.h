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
// reset was a crash, a watchdog or a brownout. Clears the note. Also writes
// the crash's details to the log (Settings > Log): what it was (the
// exception, or abort()'s / an assert's text), the task, the address and a
// backtrace (decode with the build's firmware.elf).
bool last(char* out, size_t cap);

// At start-up, right after the log's first line: reads the last reset
// (last()) once; start_message() then gives its line, or null.
void at_start();
const char* start_message();

// The panic handler's hook (main/CMakeLists.txt links ESP-IDF's
// esp_panic_handler through it): keeps the crash in RTC memory.
void on_panic(const void* panic_info);

}  // namespace crash_note
