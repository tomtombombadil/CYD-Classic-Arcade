// The board's own log, shown in Settings > Log (Tom, v0.14.6: messages that
// go by or are cut off on the news line can't be reported; "add a log
// button ... so you could see game speeds, crash info, etc").
//
// Kept in RTC memory, which survives a crash, a watchdog or a software
// reset (not a power-off): the lines that led up to a crash are still there
// after it. 4 KB, oldest lines dropped first. Every line also goes to the
// serial log. Each line starts with the start-up it belongs to and the
// seconds since then: "[3 12.4s] galaga: start; ...".
#pragma once

#include <cstddef>

namespace devlog {

// At start-up, first thing: keeps the earlier lines unless the board was
// powered on, then logs this start (firmware, board, why it reset).
void init();

// One line (printf style; longer than ~110 characters is cut). Safe from
// any task.
void add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Lines, newest first: line(0) is the latest. Returns false past the end.
int  count();
bool line(int i, char* out, size_t cap);

void clear();

}  // namespace devlog

// For C code (the Bluetooth glue): one line, as devlog::add("%s", text)
extern "C" void devlog_c(const char* text);
