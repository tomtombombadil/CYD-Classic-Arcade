// The panel (LovyanGFX, from the board file) and small text helpers shared
// by the menu screens. Colours: night-sky navy and gold, like CYD Classic
// Games.
#pragma once

#include "board_select.h"

namespace screen {

constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kText = 0xFFDB;   // cream
constexpr uint16_t kMuted = 0x8410;  // grey
constexpr uint16_t kGold = 0xFE60;
constexpr uint16_t kKey = 0x10A8;    // navy
constexpr uint16_t kRed = 0xF8A2;

LGFX& lcd();
void init();

// The menu font for this panel size, and its line height.
void menu_font();
int  line_h();

// One line of text across the screen (cleared first).
void line(int y, uint16_t color, const char* text, uint16_t bg = kBg);

}  // namespace screen
