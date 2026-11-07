// A message on an RGB-panel board whose PSRAM didn't start (no frame
// buffer can exist, so LovyanGFX can't draw anything): ESP-IDF's RGB driver
// with no frame buffer, the lines made on the fly. Landscape (the panel's
// own way round). Nothing else is drawn after it.
#pragma once

namespace rgb_fallback {

// A title and up to ~5 lines, centred. False if the panel didn't start
// (or the board has no RGB panel).
bool show(const char* title, const char* const* lines, int count);

}  // namespace rgb_fallback
