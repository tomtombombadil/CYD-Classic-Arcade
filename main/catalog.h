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

// A game's ROM regions, unpacked from the stored zips into RAM; some may
// move to flash (move_to_flash) when RAM runs short.
struct Loaded {
    uint8_t* regions[romset::kMaxRegions] = {};
    uint32_t sizes[romset::kMaxRegions] = {};
    bool     in_flash[romset::kMaxRegions] = {};
    int      count = 0;
    uint32_t flash_used = 0;    // of the scratch area
};
// false = a ROM is missing / unreadable or out of memory (logged; the
// reason in last_error()).
bool load(const games::Info& game, Loaded& out);
void unload(Loaded& l);
// Moves the biggest region still in RAM to the scratch flash and frees its
// RAM; false = none left or no room / write failed.
bool move_to_flash(Loaded& l);
// Why the last load failed, one short line for the screen.
const char* last_error();

}  // namespace catalog
