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

// A game's ROM regions, unpacked from the stored zips into RAM.
struct Loaded {
    uint8_t* regions[8] = {};
    int      count = 0;
};
// false = a ROM is missing / unreadable or out of memory (logged).
bool load(const games::Info& game, Loaded& out);
void unload(Loaded& l);

}  // namespace catalog
