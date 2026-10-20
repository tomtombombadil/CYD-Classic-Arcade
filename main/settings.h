// Settings kept in NVS (namespace "arcade").
#pragma once

#include <cstdint>

namespace settings {

struct Values {
    bool    swap_ab = false;    // Swap A/B and X/Y (buttons by printed letter)
    uint8_t volume = 50;        // 0..100 %
    bool    sound_pwm = false;  // Sound Output: DAC (false) or PWM (Tom's 2.8"
                                // ILI9341 was silent with the DAC)
    bool    rotate180 = false;  // Rotate Screen 180 degrees. Off = the USB port
                                // at the top on most boards (Tom: the board
                                // stands up, cable out the top); which end is
                                // up differs between boards (2.8" ILI9341)
    bool    demo_sounds = true;   // Demo Sounds (Tom, 2026-10-17: leave a game in
                                  // its demo and hear the classic sounds) - the
                                  // cabinet's demo sounds switch where it has one;
                                  // on, as the cabinets leave the factory
    bool    usb_transfer = false; // Settings > USB Transfer is open: kept so a
                                  // board reset by the port opening (CH340
                                  // boards on Windows) comes back to it
};

void load();
void save();
Values& get();

}  // namespace settings
