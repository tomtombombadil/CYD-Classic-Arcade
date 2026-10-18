// The game area: internal RAM set aside at start-up (after Bluetooth has
// started) and again at each game start - up to kMaxChunks blocks, the
// biggest first (the ESP32's RAM comes in separate regions) - used only by
// the game being played - its machine, the runner's buffers, its snapshot and the ROM regions kept in RAM - and
// emptied in one go when the game ends (Tom, v0.14.6: "Exiting a game
// should be similar almost to a reboot ... start each game from a unified
// ready state"). Before it, every game allocated from the shared heap, and
// what Bluetooth and earlier games left behind split the free RAM into
// pieces: Qix needs two 32 KB blocks and found a 28 KB one with 77 KB free.
// Allocation goes up from the bottom of a block; freeing a block's newest
// allocation gives its room back, any other free waits for reset().
#pragma once

#include <cstddef>
#include <cstdint>

namespace arena {

constexpr int kMaxChunks = 4;

// Takes the biggest free blocks while `keep_free` bytes of internal RAM stay
// for the rest of the system (Bluetooth, USB loader). False if nothing usable.
bool reserve(uint32_t keep_free);
// At each game start: gives the (empty) area back and reserves it again
// from what is free now, so one start-up that found little RAM (Tom,
// v0.15.1 on the 3.2": a 37 KB area, Galaga "doesn't fit") isn't kept
// until the next reboot. Returns the size before.
uint32_t renew(uint32_t keep_free);

void* alloc(size_t n);      // 8-byte aligned; null when there's no room
void  free(void* p);        // gives the room back only for the newest block
void  reset();              // everything freed: the next game starts clean

uint32_t size();            // bytes in the area
uint32_t used();
uint32_t room();            // bytes a single alloc can still get
uint32_t room_total();      // bytes left in all the blocks together
int      chunks();          // blocks in the area
uint32_t chunk_size(int i);
uint32_t peak();            // most used since start-up

}  // namespace arena
