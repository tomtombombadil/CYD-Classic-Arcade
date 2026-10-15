// ROM set description and matcher, written for this project (MIT).
// A game lists its memory regions and the ROM images that fill them, each
// with its CRC-32. The matcher takes any number of ZIP files and loose
// files and fills the regions by CRC (file names differ between MAME
// versions, and a clone's ZIP often holds only the files that differ from
// its parent's, so every ROM is looked for in every source).
//
// A game may accept more than one set (MAME's parent and a clone with
// other program ROMs, e.g. Donkey Kong's "dkong" and "dkongo"): the set
// names its variants, and the ROMs that only one variant has carry its
// number. The first variant whose ROMs are all there is loaded.
#pragma once

#include <cstddef>
#include <cstdint>

namespace romset {

struct Region {
    const char* name;           // "maincpu", "gfx1", ...
    uint32_t    size;
};

struct Rom {
    uint8_t     region;         // index into Set::regions
    uint32_t    offset;         // in the region
    uint32_t    size;
    uint32_t    crc;
    const char* name;           // the usual file name (for messages and name matches)
    bool        optional;       // not needed to run (e.g. an unused timing PROM)
    uint8_t     variant = 0;    // 0 = every variant, n = only Set::variants[n - 1]
};

// The most regions a set may have (the firmware's loader holds this many;
// a host test checks every game).
constexpr int kMaxRegions = 12;

struct Set {
    const char*   game;         // short name, e.g. "pacman"
    const char*   parent;       // MAME parent set ("puckman"), or null
    const char*   title;
    const Region* regions;
    int           region_count;
    const Rom*    roms;
    int           rom_count;
    // MAME set names of the variants, best first ("dkong", "dkongo"), or
    // null for a game with one set.
    const char* const* variants = nullptr;
    int           variant_count = 0;
};

// One file the user gave: a ZIP (recognised by its signature) or a loose
// ROM image. `name` is used only to report a loose file with a wrong CRC.
struct Source {
    const char*    name;
    const uint8_t* data;
    size_t         len;
};

enum class Status : uint8_t {
    Missing,        // nothing with this CRC or name
    Ok,             // loaded, CRC checked
    WrongCrc,       // a file with this name and size exists, but its CRC differs
    WrongSize,      // a file with this name exists with another size
    Bad,            // the CRC matched in a ZIP directory, but unpacking failed
    Unused,         // another variant's ROM (not needed for the one loaded)
};

// Fills `regions[r]` (each Set::regions[r].size bytes, cleared first) from
// all sources and writes one status per ROM. Returns true when every ROM
// that isn't optional is Ok. With variants, only pick_variant()'s ROMs are
// loaded; the others' status is Unused.
bool load(const Set& set, const Source* sources, int source_count, uint8_t* const* regions,
          Status* status);

// True when every ROM that isn't optional is in the sources, by size and
// CRC (from ZIP directories - nothing is unpacked), for at least one
// variant. For game lists.
bool available(const Set& set, const Source* sources, int source_count);

// The variant load() takes (1-based; 0 for a set without variants): the
// first one that is complete, else the one with the most ROMs found.
int pick_variant(const Set& set, const Source* sources, int source_count);

// Writes one line per ROM that isn't Ok into `out` ("missing ROM pacman.6e
// crc c1e6ab10 (4096 bytes)"), NUL-terminated, cut to `cap`. Returns the
// count of required ROMs that aren't Ok (optional ones are listed, not
// counted). Unused ROMs (another variant's) are left out.
int report(const Set& set, const Status* status, char* out, size_t cap);

const char* status_name(Status s);

} // namespace romset
