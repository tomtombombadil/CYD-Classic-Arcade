// ROM set description and matcher, written for this project (MIT).
// A game lists its memory regions and the ROM images that fill them, each
// with its CRC-32. The matcher takes any number of ZIP files and loose
// files and fills the regions by CRC (file names differ between MAME
// versions, and a clone's ZIP often holds only the files that differ from
// its parent's, so every ROM is looked for in every source).
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
};

struct Set {
    const char*   game;         // short name, e.g. "pacman"
    const char*   parent;       // MAME parent set ("puckman"), or null
    const char*   title;
    const Region* regions;
    int           region_count;
    const Rom*    roms;
    int           rom_count;
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
};

// Fills `regions[r]` (each Set::regions[r].size bytes, cleared first) from
// all sources and writes one status per ROM. Returns true when every ROM
// that isn't optional is Ok.
bool load(const Set& set, const Source* sources, int source_count, uint8_t* const* regions,
          Status* status);

// Writes one line per ROM that isn't Ok into `out` ("missing ROM pacman.6e
// crc c1e6ab10 (4096 bytes)"), NUL-terminated, cut to `cap`. Returns the
// count of required ROMs that aren't Ok (optional ones are listed, not
// counted).
int report(const Set& set, const Status* status, char* out, size_t cap);

const char* status_name(Status s);

} // namespace romset
