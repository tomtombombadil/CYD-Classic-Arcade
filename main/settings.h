// Settings kept in NVS (namespace "arcade").
#pragma once

#include <cstdint>

namespace settings {

struct Values {
    bool    swap_ab = false;    // Swap A/B and X/Y (buttons by printed letter)
    uint8_t volume = 50;        // 0..100 %
    bool    rotate180 = false;  // Rotate Screen 180 degrees. Off = the USB port
                                // at the top on most boards (Tom: the board
                                // stands up, cable out the top); which end is
                                // up differs between boards (2.8" ILI9341)
};

void load();
void save();
Values& get();

}  // namespace settings
