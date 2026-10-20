// The serial port (USB) as the ROM loader's link: runs core/usb_import on
// UART0 alongside the log output, for the web page's "Load ROMs".
#pragma once

namespace serial_link {

// Starts the reader task (core 0).
void start();

// Stops it and frees its task and the port driver (~11.7 KB on the 3.5",
// docs/MEMORY.md) for the length of a game: ROMs load only from the menu.
// False (and still running) while a file is coming in, or if it wasn't
// running.
bool stop();

// True once after the page finished a load (the menu rescans its games).
bool take_changed();

// A file is coming in right now.
bool busy();

}  // namespace serial_link
