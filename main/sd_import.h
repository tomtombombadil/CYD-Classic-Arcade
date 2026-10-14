// ROM zips from the SD card (boards with a usable slot): at start, every
// zip in /CYD-Classic-Arcade/ on the card that holds a ROM of a known game
// is copied into the ROM store, unless the same file is already there.
#pragma once

namespace sd_import {

// Shows what it's doing (one line, e.g. "Copying pacman.zip").
using Status = void (*)(const char* text);

// Returns how many zips were copied (0 when there's no card or nothing new).
int run(Status status);

}  // namespace sd_import
