// Donkey Kong Jr. ROM set - see dkongjr.h; the machine is dkong::Machine
// with the Junior board switched on.
#include "dkongjr.h"

namespace dkongjr {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x6000},
    {"soundcpu", 0x1000},
    {"gfx1",     0x2000},
    {"gfx2",     0x2000},
    {"proms",    0x0300},
};

// CRCs as in MAME's "dkongjr" (US set F-2). The program chips are loaded
// whole, one after another; the board's address lines put their pieces
// in place (dkong.cpp, kJrRomMap).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,     0x0000, 0x2000, 0xdea28158, "dkj.5b",   false},
    {kRegionCpu,     0x2000, 0x2000, 0x6fb5faf6, "dkj.5c",   false},
    {kRegionCpu,     0x4000, 0x2000, 0xd042b6a8, "dkj.5e",   false},
    {kRegionSound,   0x0000, 0x1000, 0x715da5f8, "c_3h.bin", false},
    {kRegionTiles,   0x0000, 0x1000, 0x8d51aca9, "dkj.3n",   false},
    {kRegionTiles,   0x1000, 0x1000, 0x4ef64ba5, "dkj.3p",   false},
    {kRegionSprites, 0x0000, 0x0800, 0xdc7f4164, "v_7c.bin", false},
    {kRegionSprites, 0x0800, 0x0800, 0x0ce7dcf6, "v_7d.bin", false},
    {kRegionSprites, 0x1000, 0x0800, 0x24d1ff17, "v_7e.bin", false},
    {kRegionSprites, 0x1800, 0x0800, 0x0f8c083f, "v_7f.bin", false},
    {kRegionProms,   0x0000, 0x0100, 0x463dc7ad, "c-2e.bpr", false},
    {kRegionProms,   0x0100, 0x0100, 0x47ba0042, "c-2f.bpr", false},
    {kRegionProms,   0x0200, 0x0100, 0xdbf185bf, "v-2n.bpr", false},
};

} // namespace

const romset::Set kRomSet = {
    "dkongjr", nullptr, "Donkey Kong Jr.", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

} // namespace dkongjr
