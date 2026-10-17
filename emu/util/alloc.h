// Where the emulators' memory comes from: machine objects, Qix's video RAM,
// the tile games' snapshots. The PC tools use malloc / free; the firmware
// points this at its game area (main/arena.h), which is emptied in one go
// when a game ends, so every game starts from the same memory.
#pragma once

#include <cstddef>
#include <cstdlib>

namespace emu {

using AllocFn = void* (*)(size_t);
using FreeFn = void (*)(void*);

struct Allocator {
    AllocFn alloc = nullptr;    // null = malloc / free
    FreeFn  free = nullptr;
};
inline Allocator g_allocator;

inline void set_allocator(AllocFn a, FreeFn f) { g_allocator = Allocator{a, f}; }

// Null when there's no room (never throws, never aborts).
inline void* alloc(size_t n) { return g_allocator.alloc ? g_allocator.alloc(n) : std::malloc(n); }
inline void  free(void* p)
{
    if (!p) return;
    if (g_allocator.free) g_allocator.free(p);
    else std::free(p);
}

}  // namespace emu
