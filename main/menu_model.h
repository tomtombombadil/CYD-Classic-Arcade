// What the menu pages list (the rows and texts), apart from how they're
// drawn (ui.*) and how they're driven (menu.cpp) - plain C++, also built by
// the PC preview (tools/menu_preview/).
#pragma once

#include "ui.h"

namespace menu_model {

constexpr int kMaxItems = 40;

// Home: the games, the playable ones first (in the game table's order),
// then the ones without ROMs, muted. With no ROMs at all, a "How to add
// ROMs" row comes first. `what[i]` = the game index of row i, or kHelpRow.
constexpr int kHelpRow = -1;
int build_home(ui::Item* out, int* what, const bool* available);

// Settings rows
enum Setting : int {
    kVolume,
    kSwap,
    kRotate,
    kSound,         // DAC / PWM, only where the board has the choice
    kTest,
    kForget,
    kCalibrate,     // resistive touch only
    kRoms,
    kSpeed,
    kAbout,
};

struct SettingsState {
    int  volume = 50;
    bool swap_ab = false;
    bool rotate180 = false;
    bool sound_pwm = false;
    bool has_sound_choice = true;
    bool has_calibration = true;
};
int build_settings(ui::Item* out, int* what, const SettingsState& s);
// Updates the rows' values after a change (same rows)
void refresh_settings(ui::Item* items, const int* what, int count, const SettingsState& s);

// Text pages
extern const char* const kRomsHelp[];
extern const int kRomsHelpCount;
extern const char* const kGameHelp[];   // how to play / leave a game
extern const int kGameHelpCount;

// "About": version, board, memory; lines written into `buf` (kept alive by
// the caller), `lines` pointing into it. Returns the line count.
struct AboutInfo {
    const char* version = "";
    const char* board = "";
    unsigned ram_free_kb = 0, ram_block_kb = 0, psram_kb = 0;
    unsigned rom_free_kb = 0;
    int games_ready = 0, games_total = 0;
    int paired = -1;    // controllers paired (-1 = not known: BLE-only chips)
};
int build_about(const AboutInfo& a, char (*buf)[64], const char** lines, int cap);

// Settings > Last Game Speed: where the last game's time went on this board
struct SpeedInfo {
    const char* game = "";
    float game_hz = 0, frames_per_s = 0, drawn_per_s = 0;
    float emu_ms = 0, emu_max_ms = 0, draw_ms = 0, draw_max_ms = 0, send_ms = 0, kpixels = 0;
    int worst_frames = 0, worst_drawn = 0;
};
int build_speed(const SpeedInfo& s, char (*buf)[64], const char** lines, int cap);

// A game without its ROMs: which zip it needs
int build_game_info(int game, char (*buf)[96], const char** lines, int cap);

}  // namespace menu_model
