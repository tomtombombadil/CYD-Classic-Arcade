// The start menu (see menu.h).
#include "menu.h"

#include "audio_out.h"
#include "catalog.h"
#include "game_runner.h"
#include "input.h"
#include "pad.h"
#include "pad_test.h"
#include "screen.h"
#include "serial_link.h"
#include "settings.h"
#include "version_gen.h"

#include "emu/games.h"

#include <cstdio>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace menu {
namespace {

using namespace screen;

enum Item : int { kTest = 1000, kSwap, kVolume, kRotate, kForget };

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

int item_count() { return games::kGameCount + 5; }
int item_id(int i) { return i < games::kGameCount ? i : kTest + (i - games::kGameCount); }

void item_text(int id, char* buf, size_t cap, uint16_t& color)
{
    color = kText;
    if (id < games::kGameCount) {
        const bool ok = catalog::available(id);
        std::snprintf(buf, cap, "%s%s", games::kGames[id].title, ok ? "" : "  (no ROMs)");
        color = ok ? kText : kMuted;
    } else if (id == kTest) {
        std::snprintf(buf, cap, "Controller Test");
    } else if (id == kSwap) {
        std::snprintf(buf, cap, "Swap A/B and X/Y: %s", settings::get().swap_ab ? "On" : "Off");
    } else if (id == kVolume) {
        std::snprintf(buf, cap, "Volume: %d %%", settings::get().volume);
    } else if (id == kRotate) {
        std::snprintf(buf, cap, "Rotate Screen 180: %s", settings::get().rotate180 ? "On" : "Off");
    } else {
        std::snprintf(buf, cap, "Forget Controllers");
    }
}

void draw_item(int i, int sel, int top)
{
    char buf[64];
    uint16_t color;
    item_text(item_id(i), buf, sizeof buf, color);
    const bool on = i == sel;
    line(top + i * line_h(), on ? kBg : color, buf, on ? kGold : kBg);
}

// The line above the help text, for news (ROMs loaded, receiving).
int status_y()
{
    const char* longest = "Add ROM zips: put them in a CYD-Classic-Arcade folder on the SD card, or send them over "
                          "USB from the install web page.";
    small_font();
    const int y = lcd().height() - text_box(0, kMuted, longest, false) - 4 - line_h();
    menu_font();
    return y;
}

void draw_all(int sel, int top, const char* status)
{
    LGFX& l = lcd();
    l.fillScreen(kBg);
    char buf[64];
    std::snprintf(buf, sizeof buf, "CYD Classic Arcade %s", CYD_ARCADE_VERSION);
    line(0, kGold, buf);
    for (int i = 0; i < item_count(); ++i) draw_item(i, sel, top);
    const char* help = catalog::available_count() == 0
        ? "Add ROM zips: put them in a CYD-Classic-Arcade folder on the SD card, or send them over USB "
          "from the install web page."
        : "A: play. In a game, hold Select and press Start twice to come back here.";
    const int sy = status_y();
    small_font();
    const int y = l.height() - text_box(0, kMuted, help, false) - 4;
    text_box(y, kMuted, help);
    if (status && status[0]) line(sy, kGold, status);
    menu_font();
}

}  // namespace

void run(AudioRing& ring)
{
    int sel = 0;
    Input in;
    menu_font();
    const int top = line_h() * 2;
    char status[64] = "";
    bool redraw = true;
    bool was_busy = false;
    for (;;) {
        if (redraw) {
            draw_all(sel, top, status);
            redraw = false;
        }
        // ROMs arriving over USB
        const bool busy = serial_link::busy();
        if (busy != was_busy) {
            const int sy = status_y();
            small_font();
            line(sy, kGold, busy ? "Receiving ROMs over USB..." : "");
            menu_font();
            was_busy = busy;
        }
        if (serial_link::take_changed()) {
            std::snprintf(status, sizeof status, "ROMs loaded: %d game%s ready", catalog::available_count(),
                          catalog::available_count() == 1 ? "" : "s");
            redraw = true;
            continue;
        }

        in.update(now_ms());
        const uint32_t p = in.pressed();
        const int old = sel;
        if (p & PAD_UP) sel = (sel + item_count() - 1) % item_count();
        if (p & PAD_DOWN) sel = (sel + 1) % item_count();
        if (sel != old) {
            draw_item(old, sel, top);
            draw_item(sel, sel, top);
        }
        const int id = item_id(sel);
        const bool choose = p & (PAD_A | PAD_START);
        if (id == kSwap && (choose || (p & (PAD_LEFT | PAD_RIGHT)))) {
            settings::get().swap_ab = !settings::get().swap_ab;
            pad_set_swap_ab(settings::get().swap_ab);
            settings::save();
            draw_item(sel, sel, top);
        } else if (id == kVolume && (p & (PAD_LEFT | PAD_RIGHT))) {
            int v = settings::get().volume + ((p & PAD_RIGHT) ? 10 : -10);
            v = v < 0 ? 0 : v > 100 ? 100 : v;
            settings::get().volume = uint8_t(v);
            audio::set_volume(v);
            settings::save();
            draw_item(sel, sel, top);
        } else if (id == kRotate && (choose || (p & (PAD_LEFT | PAD_RIGHT)))) {
            settings::get().rotate180 = !settings::get().rotate180;
            settings::save();
            apply_rotation();
            redraw = true;
        } else if (choose && id == kForget) {
            // Pairings this board keeps: a controller paired since with
            // another board can fail to reconnect here until forgotten
            pad_forget_all();
            std::snprintf(status, sizeof status, "Controllers forgotten: pair them again");
            in.wait_for_release();
            redraw = true;
        } else if (choose && id == kTest) {
            pad_test::run(ring);
            in.wait_for_release();
            redraw = true;
        } else if (choose && id < games::kGameCount && catalog::available(id)) {
            status[0] = 0;
            game_runner::run(id, ring);
            in.wait_for_release();
            menu_font();
            redraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

}  // namespace menu
