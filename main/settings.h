// Settings kept in NVS (namespace "arcade").
#pragma once

#include <cstdint>

namespace settings {

struct Values {
    bool    swap_ab = false;    // Swap A/B and X/Y (buttons by printed letter)
    uint8_t volume = 50;        // 0..100 %
};

void load();
void save();
Values& get();

}  // namespace settings
