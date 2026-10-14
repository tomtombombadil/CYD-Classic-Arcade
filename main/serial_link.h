// The serial port (USB) as the ROM loader's link: runs core/usb_import on
// UART0 alongside the log output, for the web page's "Load ROMs".
#pragma once

namespace serial_link {

// Starts the reader task (core 0).
void start();

// True once after the page finished a load (the menu rescans its games).
bool take_changed();

// A file is coming in right now.
bool busy();

}  // namespace serial_link
