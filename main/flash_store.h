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

struct Lock {
    Lock() { lock(); }
    ~Lock() { unlock(); }
};

}  // namespace flash_store
