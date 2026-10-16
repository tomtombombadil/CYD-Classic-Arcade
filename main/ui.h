// The menus' look: theme, layout per panel size, widgets, and the two page
// kinds (a paged list of rows, and a page of text), drawn with LovyanGFX
// onto the panel - or onto a sprite in the PC preview
// (tools/menu_preview/), so this file uses nothing from ESP-IDF.
//
// Every page: a header bar (back arrow, title, settings gear), the page's
// content, then two small lines at the bottom - news (gold) and which
// controllers are connected. Rows and keys are big enough for a finger or a
// stylus; nothing scrolls by dragging: long lists have < > page keys.
// Layout comes from the panel size only, so nothing is clipped on 240x320
// or 320x480.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <cstdint>

namespace ui {

using Gfx = lgfx::LovyanGFX;

constexpr uint16_t rgb(int r, int g, int b) { return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

// Night-sky navy and gold, as in CYD Classic Games
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kBar = rgb(10, 14, 40);       // header / bottom bars
constexpr uint16_t kCard = rgb(20, 26, 66);      // rows, keys
constexpr uint16_t kCardHi = rgb(44, 54, 120);   // a row being touched
constexpr uint16_t kText = rgb(255, 248, 220);   // cream
constexpr uint16_t kSub = rgb(150, 156, 190);    // second lines
constexpr uint16_t kMuted = rgb(96, 100, 130);   // unavailable
constexpr uint16_t kGold = rgb(255, 200, 40);
constexpr uint16_t kTeal = rgb(40, 190, 150);    // connected, switch on
constexpr uint16_t kOff = rgb(60, 64, 90);       // switch off
constexpr uint16_t kRed = rgb(240, 80, 70);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Sizes for this panel (240 or 320 wide, portrait)
struct Metrics {
    int w, h;
    bool big;           // 320-wide panels
    int margin;         // left / right of rows
    int header_h;
    int pager_h;
    int news_h, pads_h; // the two bottom lines
    int list_y, list_h; // between header and pager
    int row_h, rows;    // rows per page
    int gap;            // between rows
    const lgfx::IFont* title;   // header
    const lgfx::IFont* body;    // row titles, keys
    const lgfx::IFont* sub;     // row second lines, text pages
    const lgfx::IFont* tiny;    // bottom lines
    const lgfx::IFont* text;    // text pages, messages
};
Metrics metrics(Gfx& g);

// ---- What a page shows (filled in by the menu) ----

// The right side of a row
enum class Widget : uint8_t {
    None,
    Chevron,    // opens a page
    Switch,     // on / off (Item::on)
    Stepper,    // [-] value [+]
    Value,      // a word in gold (Item::value), tap to change
    Sideways,   // a landscape game: turn the board on its side
};

struct Item {
    const char* title = "";
    char sub[48] = "";      // second line
    Widget widget = Widget::None;
    bool on = false;
    char value[12] = "";
    bool muted = false;     // e.g. a game without its ROMs
};

struct Pad {
    bool connected = false;
    char name[32] = "";
};

// The bottom lines, shared by every page
struct Footer {
    const char* news = "";  // gold, one line ("" = none)
    Pad pads[2];
};

struct ListPage {
    const char* title = "";
    bool back = false;      // header < (else none)
    bool gear = false;      // header settings key
    const Item* items = nullptr;
    int count = 0;
    int page = 0;
    int focus = -1;         // the controller's row (-1 = none)
    int pressed = -1;       // the row being touched (-1 = none)
    const char* empty = ""; // shown when count == 0
};

int page_count(const Metrics& m, int items);
int page_of(const Metrics& m, int index);

void draw_list(Gfx& g, const ListPage& p, const Footer& f);
void draw_row(Gfx& g, const ListPage& p, int index);     // one row, if it's on the page
void draw_footer(Gfx& g, const Footer& f);               // the two bottom lines

enum class Hit : uint8_t { None, Back, Gear, Row, Minus, Plus, Prev, Next };
struct Target {
    Hit hit = Hit::None;
    int index = -1;
};
Target hit_list(Gfx& g, const ListPage& p, int x, int y);

// A page of text: paragraphs wrapped to the width ("" = a blank line),
// a heading line is drawn in gold when it starts with '#'.
struct TextPage {
    const char* title = "";
    const char* const* lines = nullptr;
    int count = 0;
};
void draw_text(Gfx& g, const TextPage& p, const Footer& f);
Target hit_text(Gfx& g, int x, int y);   // Back only

// Just the header bar (pages drawn elsewhere, e.g. the Controller Test);
// hit_text() finds its back key.
void draw_header(Gfx& g, const char* title, bool back, bool gear);

// A message in a card in the middle of the screen (over what's there).
void message(Gfx& g, const char* title, const char* body, uint16_t title_color = kGold);

// Writes `text` into `out` cut to `max_w` pixels with "..." if needed (the
// current font).
void fit(Gfx& g, const char* text, int max_w, char* out, int cap);

}  // namespace ui
