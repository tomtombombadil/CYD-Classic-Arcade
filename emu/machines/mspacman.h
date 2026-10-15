// Ms. Pac-Man (Midway / General Computer, 1981), written for this project
// (MIT). A Pac-Man board (pacman.h) with the auxiliary board that plugs into
// its Z80 socket: three more program ROMs (U5 2 KB, U6 4 KB, U7 4 KB) with
// their address and data lines wired out of order, and logic that lays them
// over the Pac-Man program. Hardware facts from public documentation (US
// patent 4,525,599) and MAME's driver (read for facts only).
//
// With the aux board's latch set ("decode on"):
//   0000-2FFF  Pac-Man ROMs, but forty 8-byte patch windows read aux ROM
//              8000-81EF instead
//   3000-3FFF  U7                 8000-87FF  U5      8800-97FF  U6
//   9800-9FFF  Pac-Man 1800-1FFF  A000-BFFF  Pac-Man 2000-3FFF
// With it clear: Pac-Man's ROMs at 0000-3FFF, mirrored at 8000-BFFF.
// Any access to one of eight 8-byte trap windows changes the latch: 3FF8
// sets it (the VBLANK interrupt's vector fetch from 3FFC hits it every
// frame), 0038 03B0 1600 2120 3FF0 8000 97F0 clear it (Pac-Man's ROM check
// walks into 0038 first, so it checksums the plain ROMs). 2120 isn't
// emulated (see mspacman.cpp: it costs much and nothing reaches it).
#pragma once

#include "pacman.h"

namespace mspacman {

// The ROM set (MAME "mspacman", a parent set with everything in its zip);
// regions in this order.
enum Region { kRegionCpu, kRegionAux, kRegionGfx, kRegionProms, kRegionSound, kRegionCount };
extern const romset::Set kRomSet;

// The aux region: U5 at 0000, U6 at 0800, U7 at 1800.
constexpr uint32_t kAuxSize = 0x2800;

// The aux board's wiring, for one byte the Z80 sees (exposed for tests).
// U5: aux address 8000 + i (i < 0x800) reads U5 at u5_address(i).
// U6: 8800 + i (i < 0x1000) reads U6 at u67_address(i) ^ 0x800 (its two
//     halves swap). U7: 3000 + i reads U7 at u67_address(i).
// Every byte's data lines: decode_data().
uint16_t u5_address(uint16_t i);
uint16_t u67_address(uint16_t i);
uint8_t  decode_data(uint8_t raw);

// The patch windows: Z80 address -> aux ROM offset from 8000.
struct Patch {
    uint16_t addr;
    uint16_t aux;
};
constexpr int kPatchCount = 40;
extern const Patch kPatches[kPatchCount];

class Machine : public pacman::Machine {
public:
    // `aux`: the kRegionAux region (kAuxSize bytes).
    void init(const pacman::Roms& roms, const uint8_t* aux, uint32_t audio_rate, bool swap_rgb565_bytes = false);

    bool decode_on() const { return decode_; }

    // Pages of 0000-2FFF with a patch window: copied at init.
    static constexpr int kPatchedPages = 10;

private:
    static uint8_t aux_read(void* ctx, uint16_t addr);
    static void    aux_write(void* ctx, uint16_t addr, uint8_t v);
    void trap(uint16_t addr);
    void set_decode(bool on);

    // Pages (1 KB) of 0000-3FFF then 8000-BFFF, per latch state.
    static constexpr int kRomPages = 32;

    const uint8_t* page_[2][kRomPages] = {};
    // Per page, the offsets of its trap windows (none: kNoWindow).
    static constexpr uint16_t kNoWindow = 0xffff;
    uint16_t window_[kRomPages][2] = {};
    bool decode_ = true;    // power-on state unknown; the game sets it at its first VBLANK

    uint8_t aux_[0x1800] = {};                      // 8000-97FF: U5, U6 decoded
    uint8_t u7_[0x1000] = {};                       // 3000-3FFF: U7 decoded
    uint8_t patched_[kPatchedPages][0x400] = {};    // Pac-Man pages with the windows filled in
};

} // namespace mspacman
