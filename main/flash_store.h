// The `roms` flash partition as the ROM store's flash (core/rom_store.h),
// memory-mapped so stored zips are read in place.
#pragma once

#include "core/rom_store.h"

namespace flash_store {

// Finds and maps the partition; false = no `roms` partition (logged).
bool open();

rom_store::Flash& flash();
rom_store::Store& store();

// The store is shared by the menu, the game loader and the USB / SD
// loaders: hold this while using it.
void lock();
void unlock();

// Scratch flash at the partition's end (kept out of the ROM store) for ROM
// regions that don't fit in RAM: read in place, memory-mapped.
uint32_t scratch_size();     // 0 = none (an old store already reaches into it)
// Puts `len` bytes at sector-aligned `offset` in the scratch area (erasing
// only when they differ from what's there) and returns where they're
// mapped, or null.
const uint8_t* scratch_put(uint32_t offset, const uint8_t* data, uint32_t len);

struct Lock {
    Lock() { lock(); }
    ~Lock() { unlock(); }
};

}  // namespace flash_store
