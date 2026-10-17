// The menu pages' rows and texts (see menu_model.h).
#include "menu_model.h"

#include "emu/games.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace menu_model {
namespace {

void set(char* dst, size_t cap, const char* s) { std::snprintf(dst, cap, "%s", s); }

}  // namespace

int build_home(ui::Item* out, int* what, const bool* available)
{
    int n = 0, ready = 0;
    for (int g = 0; g < games::kGameCount; g++) ready += available[g];
    if (ready == 0) {
        ui::Item& it = out[n];
        it = ui::Item{};
        it.title = "How to Add Games";
        set(it.sub, sizeof it.sub, "Copy your ROM zips here");
        it.widget = ui::Widget::Chevron;
        what[n++] = kHelpRow;
    }
    // A to Z (Tom, 2026-10-17: the order they were added in isn't for a
    // finished product), the playable ones first
    int order[games::kMaxGames];
    int count = 0;
    for (int g = 0; g < games::kGameCount && g < games::kMaxGames; g++) order[count++] = g;
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && strcasecmp(games::kGames[order[j]].title, games::kGames[order[j - 1]].title) < 0; j--) {
            const int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < count && n < kMaxItems; k++) {
            const int g = order[k];
            if (available[g] != (pass == 0)) continue;
            const games::Info& info = games::kGames[g];
            ui::Item& it = out[n];
            it = ui::Item{};
            it.title = info.title;
            if (!available[g]) std::snprintf(it.sub, sizeof it.sub, "No ROMs yet - tap for help");
            else if (info.year) std::snprintf(it.sub, sizeof it.sub, "%s %d", info.maker, info.year);
            it.muted = !available[g];
            it.widget = info.orientation == games::Orientation::Landscape ? ui::Widget::Sideways : ui::Widget::None;
            what[n++] = g;
        }
    }
    return n;
}

void refresh_settings(ui::Item* items, const int* what, int count, const SettingsState& s)
{
    for (int i = 0; i < count; i++) {
        ui::Item& it = items[i];
        switch (what[i]) {
        case kVolume:
            if (s.volume == 0) set(it.value, sizeof it.value, "Off");
            else std::snprintf(it.value, sizeof it.value, "%d%%", s.volume);
            break;
        case kSwap: it.on = s.swap_ab; break;
        case kDemoSounds: it.on = s.demo_sounds; break;
        case kRotate: it.on = s.rotate180; break;
        case kSound: set(it.value, sizeof it.value, s.sound_pwm ? "PWM" : "DAC"); break;
        default: break;
        }
    }
}

int build_settings(ui::Item* out, int* what, const SettingsState& s)
{
    int n = 0;
    auto add = [&](Setting id, const char* title, const char* sub, ui::Widget w) {
        ui::Item& it = out[n];
        it = ui::Item{};
        it.title = title;
        set(it.sub, sizeof it.sub, sub);
        it.widget = w;
        what[n++] = id;
    };
    add(kVolume, "Volume", "Game sound", ui::Widget::Stepper);
    add(kDemoSounds, "Demo Sounds", "Sound in the attract mode", ui::Widget::Switch);
    add(kSwap, "Swap A/B and X/Y", "Nintendo-style button letters", ui::Widget::Switch);
    add(kRotate, "Rotate Screen 180", "Upside-down screen", ui::Widget::Switch);
    if (s.has_sound_choice) add(kSound, "Sound Output", "DAC or PWM; restarts", ui::Widget::Value);
    add(kTest, "Controller Test", "Buttons, sticks, speed", ui::Widget::Chevron);
    add(kForget, "Forget Controllers", "Pair them again afterwards", ui::Widget::None);
    if (s.has_calibration) add(kCalibrate, "Calibrate Touch", "Tap four corner arrows", ui::Widget::Chevron);
    add(kRoms, "How to Add Games", "From SD card or USB", ui::Widget::Chevron);
    add(kSpeed, "Last Game Speed", "Where this board's time goes", ui::Widget::Chevron);
    add(kLog, "Log", "Starts, games, crashes, controllers", ui::Widget::Chevron);
    add(kAbout, "About", "Version, board, memory", ui::Widget::Chevron);
    refresh_settings(out, what, n, s);
    return n;
}

const char* const kRomsHelp[] = {
    "#Your own ROM zips",
    "Games aren't included: use the MAME ROM zips of games you own, as they are.",
    "",
    "#From a computer",
    "On the install web page, open Load ROMs, connect by USB and pick the zips.",
    "",
    "#From an SD card",
    "Put them in a CYD-Classic-Arcade folder on the card, then restart the board.",
};
const int kRomsHelpCount = int(sizeof kRomsHelp / sizeof kRomsHelp[0]);

const char* const kGameHelp[] = {
    "#Playing",
    "Select drops a coin, Start begins. A is the main button.",
    "",
    "#Leaving a game",
    "Hold Select and press Start twice, press Home, or touch and hold the screen.",
};
const int kGameHelpCount = int(sizeof kGameHelp / sizeof kGameHelp[0]);

int build_about(const AboutInfo& a, char (*buf)[64], const char** lines, int cap)
{
    int n = 0;
    auto line = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        std::snprintf(buf[n], 64, fmt, args...);
        lines[n] = buf[n];
        n++;
    };
    line("%s", "#CYD Classic Arcade");
    line("Version %s", a.version);
    line("Board: %s", a.board);
    line("%s", "");
    line("%s", "#Games");
    line("%d of %d ready", a.games_ready, a.games_total);
    line("ROM storage: %u KB free", a.rom_free_kb);
    if (a.paired >= 0) {
        line("%s", "");
        line("%s", "#Controllers");
        line("Paired with this board: %d", a.paired);
    }
    line("%s", "");
    line("%s", "#Memory");
    line("Game area: %u KB (most used %u KB)", a.area_kb, a.area_peak_kb);
    line("RAM outside it: %u KB free, block %u KB", a.ram_free_kb, a.ram_block_kb);
    if (a.psram_kb) line("PSRAM: %u KB free", a.psram_kb);
    return n;
}

int build_speed(const SpeedInfo& a, char (*buf)[64], const char** lines, int cap)
{
    int n = 0;
    auto line = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        std::snprintf(buf[n], 64, fmt, args...);
        lines[n] = buf[n];
        n++;
    };
    if (!a.game[0]) {
        if (cap > 0) lines[n++] = "Play a game, then come back here to see how fast it ran on this board.";
        return n;
    }
    line("#%s", a.game);
    line("%.1f frames a second (the game: %.1f)", double(a.frames_per_s), double(a.game_hz));
    line("%.0f pictures shown a second", double(a.drawn_per_s));
    line("Slowest second: %d frames, %d pictures", a.worst_frames, a.worst_drawn);
    line("%s", "");
    line("#Time a frame: %.1f ms", a.game_hz > 0 ? 1000.0 / double(a.game_hz) : 0.0);
    line("Emulation: %.1f ms, worst %.1f", double(a.emu_ms), double(a.emu_max_ms));
    line("Picture: %.1f ms, worst %.1f", double(a.draw_ms), double(a.draw_max_ms));
    line("Sending it: %.1f ms, %.1fK pixels", double(a.send_ms), double(a.kpixels));
    return n;
}

int build_game_info(int game, char (*buf)[96], const char** lines, int cap)
{
    int n = 0;
    auto line = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        std::snprintf(buf[n], 96, fmt, args...);
        lines[n] = buf[n];
        n++;
    };
    const games::Info& g = games::kGames[game];
    const romset::Set& set = *g.set;
    line("#%s", g.title);
    if (g.year) line("%s %d", g.maker, g.year);
    line("%s", "");
    line("%s", "#ROMs needed");
    if (set.variant_count > 1) {
        char names[80] = "";
        for (int v = 0; v < set.variant_count; v++) {
            const size_t used = std::strlen(names);
            std::snprintf(names + used, sizeof names - used, "%s%s.zip", v ? (v == set.variant_count - 1 ? " or " : ", ") : "",
                          set.variants[v]);
        }
        line("The MAME set %s.", names);
    } else {
        line("The MAME set %s.zip.", set.game);
    }
    if (set.parent) line("For a clone set, add its parent %s.zip too.", set.parent);
    line("%s", "");
    line("%s", "Settings > How to Add Games tells you how to copy it onto the board.");
    return n;
}

}  // namespace menu_model
