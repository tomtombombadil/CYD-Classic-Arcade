// ROM zips from the SD card (boards with a usable slot). ESP32 boards: at
// start, every zip in /CYD-Classic-Arcade/ on the card that holds a ROM of a
// known game is copied into the ROM store, unless the same file is already
// there. Boards with SDMMC and PSRAM (the S3): the zips stay on the card -
// at start only their directories are read (kept in PSRAM, a few KB each),
// and a game's zips are read whole into PSRAM when it starts.
#pragma once

#include "emu/rom/romset.h"

#include <cstdint>

namespace sd_import {

// Shows what it's doing (one line, e.g. "Copying pacman.zip").
using Status = void (*)(const char* text);

// Returns how many zips were copied (0 when there's no card or nothing new).
int run(Status status);

// The card's library (zips not in the ROM store), as directory-only
// sources: their directories read as zips, enough to tell which games
// they complete, not to unpack. Empty on boards that copy instead.
int card_sources(romset::Source* out, int cap);
int card_count();

// A card zip read whole into PSRAM (nullptr if it can't be); the card is
// mounted on the first call and stays mounted until card_done().
uint8_t* card_read(const char* name, uint32_t* len);
void card_done();

}  // namespace sd_import
