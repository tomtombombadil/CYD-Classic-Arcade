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

// A game's ROM regions: PSRAM first on boards that have it, then what's
// left of the game area (main/arena.h), the rest in the scratch flash, read
// in place - streamed there ROM by ROM, so a big game's ROMs never all sit
// in RAM at once.
struct Loaded {
    uint8_t* regions[romset::kMaxRegions] = {};
    uint32_t sizes[romset::kMaxRegions] = {};
    bool     in_flash[romset::kMaxRegions] = {};
    bool     in_psram[romset::kMaxRegions] = {};
    int      count = 0;
    uint32_t flash_used = 0;    // bytes of ROM read from flash
    uint32_t ram_used = 0;      // bytes of ROM in the game area
};
// Call after the machine and the runner's buffers are in the game area.
// false = a ROM is missing / unreadable or no room (logged; the reason in
// last_error()).
bool load(const games::Info& game, Loaded& out);
void unload(Loaded& l);
// Why the last load failed, one short line for the screen.
const char* last_error();

}  // namespace catalog
