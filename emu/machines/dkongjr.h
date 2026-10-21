// Donkey Kong Jr. (Nintendo, 1982) arcade machine, written for this project
// (MIT). Donkey Kong's board (emu/machines/dkong.*: the same Z80, timing,
// 8257 sprite DMA, tiles, sprites, palette PROMs and 8035 sound CPU) with
// the Junior's changes switched on: 24 KB of program in three 8 KB chips
// whose 2 KB pieces the board spreads over 0000-5FFF, two banks of 256
// tiles (latch 4H at 7C80), the sound board's other wiring (a 4 KB 8035
// program reading the tune latch directly, no data ROM) and its own
// discrete sounds (emu/sound/dkongjr_sound.*). Hardware facts from MAME's
// driver (read for facts only).
#pragma once

#include "../sound/dkongjr_sound.h"
#include "dkong.h"

namespace dkongjr {

using dkong::kScreenW;
using dkong::kScreenH;
using dkong::kCpuClock;
using dkong::kCyclesPerFrame;

// Inputs as Donkey Kong's (dkong::Button).

// DIP switches (DSW0, read at 7D80): as Donkey Kong's but the bonus.
enum Dip : uint8_t {
    kDipLivesMask = 0x03, kDipLives3 = 0x00, kDipLives4 = 0x01, kDipLives5 = 0x02, kDipLives6 = 0x03,
    kDipBonusMask = 0x0c, kDipBonus10000 = 0x00, kDipBonus15000 = 0x04, kDipBonus20000 = 0x08,
                          kDipBonus25000 = 0x0c,
    kDipCoinMask  = 0x70, kDip1Coin1Credit = 0x00, kDip2Coins1Credit = 0x10, kDip1Coin2Credits = 0x20,
    kDipUpright   = 0x80,     // clear = cocktail
};
// 3 lives, bonus at 10000, 1 coin 1 credit, upright.
constexpr uint8_t kDipDefault = kDipLives3 | kDipBonus10000 | kDip1Coin1Credit | kDipUpright;

// The ROM set: MAME "dkongjr" (US set F-2; Tom's older names dkj.5b ...).
enum Region { kRegionCpu, kRegionSound, kRegionTiles, kRegionSprites, kRegionProms, kRegionCount };
extern const romset::Set kRomSet;

class Machine : public dkong::Machine {
public:
    void init(const dkong::Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes = false)
    {
        init_board(roms, audio_rate, swap_rgb565_bytes, &sound_, tile_cols_hi_);
    }
    // kDipDefault is Donkey Kong's default byte too (0x80).
    static_assert(kDipDefault == dkong::kDipDefault, "the base class starts with this");

private:
    dkongjr_sound::Board sound_;
    uint16_t tile_cols_hi_[256 * 8] = {};   // tile bank 1, decoded as bank 0
};

} // namespace dkongjr
