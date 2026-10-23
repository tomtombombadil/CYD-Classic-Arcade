// Where the game loader puts ROM regions in the scratch flash (MIT; plain
// C++, host-tested in core_tests). The scratch area is the last 128 KB of
// the `roms` partition, kept out of the ROM store; its first sector is a
// header, then the regions one after another. A game whose regions going
// to flash don't all fit there (1942: ~180 KB) puts the rest just below
// the area, in the store's free sectors (rom_store::Store::forget_erased):
// those regions get negative offsets. Regions that fit above keep the
// places they always had, so the games that fit write nothing new.
#pragma once

#include <cstdint>

namespace scratch_layout {

constexpr uint32_t kSector = 4096;

struct Result {
    uint32_t top = 0;       // bytes used from the area's start (header included)
    uint32_t below = 0;     // bytes used below the area
};

// sizes[i] (bytes) of the n regions going to flash, in order; offset[i]
// gets each one's place (from the area's start; negative = below it).
// `area` = the area's size, `first` = where the regions start (after the
// header).
inline Result place(const uint32_t* sizes, int n, uint32_t area, uint32_t first, int32_t* offset)
{
    Result r;
    r.top = first;
    for (int i = 0; i < n; ++i) {
        const uint32_t span = (sizes[i] + kSector - 1) / kSector * kSector;
        if (r.top + span <= area) {
            offset[i] = int32_t(r.top);
            r.top += span;
        } else {
            r.below += span;
            offset[i] = -int32_t(r.below);
        }
    }
    return r;
}

}  // namespace scratch_layout
