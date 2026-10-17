// Which games the stored ROM zips make playable, and loading a game's ROMs.
#pragma once

#include "emu/games.h"

#include <cstdint>

namespace catalog {

// Rechecks every game against the stored zips (call after the store changed).
void refresh();

bool available(int game);       // index into games::kGames
int  available_count();

// The playable games' ids, space-separated (for the USB loader's answer).
void list_ids(char* out, size_t cap);

// A game's ROM regions: in RAM where there's room (PSRAM first on boards
// that have it), the rest in the scratch flash, read in place.
struct Loaded {
    uint8_t* regions[romset::kMaxRegions] = {};
    uint32_t sizes[romset::kMaxRegions] = {};
    bool     in_flash[romset::kMaxRegions] = {};
    int      count = 0;
    uint32_t flash_used = 0;    // bytes of ROM read from flash
};
// Call after the machine is made (its object is the biggest block). A region
// goes into internal RAM only while at least `keep_free` bytes of it stay
// free afterwards (the runner's buffers, Bluetooth); the others stream into
// the scratch flash ROM by ROM, so a big game's ROMs never all sit in RAM at
// once (v0.15.0: v0.14.0 loaded them all first and the board reset).
// false = a ROM is missing / unreadable or no room (logged; the reason in
// last_error()).
bool load(const games::Info& game, Loaded& out, uint32_t keep_free);
void unload(Loaded& l);
// Why the last load failed, one short line for the screen.
const char* last_error();

}  // namespace catalog
