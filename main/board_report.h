// A hardware report at every start on boards that ask for it
// (BOARD_REPORT: the 5.0" RGB board), to tell look-alike boards apart
// (Tom, 2026-11-07: a Hoysond 5" 800x480 ESP32-S3 board of unknown
// make-up): chip model / revision / features, flash size and ID, PSRAM size
// and mode, every address answering on the touch's I2C bus, and the GT911's
// product ID, firmware, config version and resolution. Lines "HW ..." in the
// log (Settings > Log, Send Log, Download Log, serial) and on About.
#pragma once

namespace board_report {

// Reads the hardware and logs it (after screen::init: the touch's reset done).
void run();

// The report's lines (for About); 0 on other boards.
int lines(const char* const** out);

}  // namespace board_report
