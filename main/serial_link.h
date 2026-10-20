// The serial port (USB) as the ROM loader's link: runs core/usb_import on
// UART0 alongside the log output, for the web page's "Load ROMs". Runs only
// while Settings > USB Transfer is open (Tom, 2026-10-20: ROMs are loaded
// about once per board - "let's not load it all the time").
#pragma once

namespace serial_link {

// Starts the reader task (core 0) and the port driver (~11.7 KB on the
// 3.5", docs/MEMORY.md). False if they don't fit.
bool start();

// Stops them and frees their RAM. False (and still running) while a file is
// coming in, or if it wasn't running.
bool stop();

// True once after the page finished a load (the menu rescans its games).
bool take_changed();

// A file is coming in right now.
bool busy();

}  // namespace serial_link
