// The menus (see menu.h): Home (the games), Settings and text pages, drawn
// by ui.* from menu_model.*'s rows, driven by touch and the controllers
// alike.
//
// Touch: tap a row, a key, the header's < (back) or gear (Settings); < >
// keys change pages. Controller: up / down move the gold frame (pages
// follow), A or Start chooses, B goes back, left / right change a setting
// or the page, L / R change the page.
#include "menu.h"

#include "audio_out.h"
#include "catalog.h"
#include "crash_note.h"
#include "flash_store.h"
#include "game_runner.h"
#include "input.h"
#include "menu_model.h"
#include "pad.h"
#include "pad_test.h"
#include "screen.h"
#include "serial_link.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"
#include "version_gen.h"

#include "emu/games.h"

#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace menu {
namespace {

using menu_model::kMaxItems;

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

enum class Page { Home, Settings, Text };

struct Menu {
    AudioRing* ring = nullptr;
    Input in;
    Page page = Page::Home;

    ui::Item home_items[kMaxItems];
    int home_what[kMaxItems] = {};
    ui::ListPage home;

    ui::Item set_items[kMaxItems];
    int set_what[kMaxItems] = {};
    ui::ListPage settings;

    // The open text page and where its < goes
    ui::TextPage text;
    Page text_back = Page::Home;
    const char* text_lines[24] = {};
    char text_buf[16][96] = {};

    ui::Footer footer;
    char news[72] = "";
    bool receiving = false;

    // Touch: what the press started on
    ui::Target down;

    LGFX& g() { return screen::lcd(); }
    ui::ListPage* list() { return page == Page::Home ? &home : page == Page::Settings ? &settings : nullptr; }

    menu_model::SettingsState settings_state() const
    {
        menu_model::SettingsState s;
        const settings::Values& v = settings::get();
        s.volume = v.volume;
        s.swap_ab = v.swap_ab;
        s.rotate180 = v.rotate180;
        s.sound_pwm = v.sound_pwm;
        s.has_sound_choice = audio::has_output_choice();
        s.has_calibration = touch::needs_calibration();
        return s;
    }

    void build_home()
    {
        bool avail[games::kMaxGames] = {};
        for (int i = 0; i < games::kGameCount && i < games::kMaxGames; i++) avail[i] = catalog::available(i);
        home.title = "CYD Classic Arcade";
        home.gear = true;
        home.items = home_items;
        home.count = menu_model::build_home(home_items, home_what, avail);
        if (home.focus >= home.count) home.focus = home.count - 1;
        if (home.focus < 0) home.focus = 0;
        const ui::Metrics m = ui::metrics(g());
        home.page = ui::page_of(m, home.focus);
    }

    void build_settings()
    {
        settings.title = "Settings";
        settings.back = true;
        settings.items = set_items;
        settings.count = menu_model::build_settings(set_items, set_what, settings_state());
        if (settings.focus < 0 || settings.focus >= settings.count) settings.focus = 0;
    }

    void read_pads(bool force)
    {
        bool changed = force;
        for (int i = 0; i < 2 && i < PAD_MAX; i++) {
            pad_state_t p;
            pad_get(i, &p);
            ui::Pad& d = footer.pads[i];
            if (d.connected != bool(p.connected) || std::strcmp(d.name, p.name) != 0) {
                d.connected = p.connected;
                std::snprintf(d.name, sizeof d.name, "%s", p.name);
                changed = true;
            }
        }
        footer.news = shown_news();
        if (changed && !force) ui::draw_footer(g(), footer);
    }

    const char* shown_news() const { return receiving ? "Receiving ROMs over USB..." : news; }

    void set_news(const char* s, bool draw = true)
    {
        std::snprintf(news, sizeof news, "%s", s);
        footer.news = shown_news();
        if (draw) ui::draw_footer(g(), footer);
    }

    void draw()
    {
        footer.news = shown_news();
        if (page == Page::Text) ui::draw_text(g(), text, footer);
        else ui::draw_list(g(), *list(), footer);
    }

    void open(Page p)
    {
        page = p;
        if (p == Page::Settings) build_settings();
        draw();
    }

    void open_text(const char* title, const char* const* lines, int count, Page back)
    {
        text.title = title;
        text.lines = lines;
        text.count = count;
        text_back = back;
        page = Page::Text;
        draw();
    }

    void open_about()
    {
        static char buf[16][64];
        menu_model::AboutInfo a;
        a.version = CYD_ARCADE_VERSION;
        a.board = BOARD_NAME;
        a.ram_free_kb = unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
        a.ram_block_kb = unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
        a.psram_kb = unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        {
            flash_store::Lock lock;
            a.rom_free_kb = unsigned(flash_store::store().free_bytes() / 1024);
        }
        a.games_ready = catalog::available_count();
        a.games_total = games::kGameCount;
        const int n = menu_model::build_about(a, buf, text_lines, 16);
        open_text("About", text_lines, n, Page::Settings);
    }

    void open_game_info(int game)
    {
        const int n = menu_model::build_game_info(game, text_buf, text_lines, 16);
        open_text(games::kGames[game].title, text_lines, n, Page::Home);
    }

    // Moves the controller's frame; the page follows
    void move_focus(ui::ListPage& l, int to)
    {
        if (l.count == 0) return;
        to = (to + l.count) % l.count;
        const int old = l.focus;
        l.focus = to;
        const ui::Metrics m = ui::metrics(g());
        if (ui::page_of(m, to) != l.page) {
            l.page = ui::page_of(m, to);
            draw();
        } else {
            ui::draw_row(g(), l, old);
            ui::draw_row(g(), l, to);
        }
    }

    void turn_page(ui::ListPage& l, int dir)
    {
        const ui::Metrics m = ui::metrics(g());
        const int pages = ui::page_count(m, l.count);
        const int p = l.page + dir;
        if (p < 0 || p >= pages) return;
        l.page = p;
        l.focus = p * m.rows;
        draw();
    }

    // ---- actions ----

    void play(int game)
    {
        if (footer.pads[0].connected + footer.pads[1].connected == 0) {
            ui::message(g(), "No controller",
                        "Put one in pairing mode to play. To leave a game, touch and hold the screen.");
            const uint32_t t0 = now_ms();
            while (now_ms() - t0 < 3500) {
                if (touch::poll().kind == touch::Event::Up) break;
                vTaskDelay(pdMS_TO_TICKS(16));
            }
        }
        news[0] = 0;
        game_runner::run(game, *ring);
        const game_runner::Stats& st = game_runner::last_stats();
        if (st.frames_per_s > 0)
            std::snprintf(news, sizeof news, "%s: %.1f fps, %.0f shown/s", games::kGames[game].title,
                          double(st.frames_per_s), double(st.drawn_per_s));
        in.wait_for_release();
        touch::wait_release();
        read_pads(true);
        draw();
    }

    void choose_home(int i)
    {
        if (i < 0 || i >= home.count) return;
        const int what = home_what[i];
        if (what == menu_model::kHelpRow)
            open_text("How to Add Games", menu_model::kRomsHelp, menu_model::kRomsHelpCount, Page::Home);
        else if (catalog::available(what)) play(what);
        else open_game_info(what);
    }

    void change_setting(int i, int dir)   // dir: -1 / +1 (stepper), 0 = chosen
    {
        if (i < 0 || i >= settings.count) return;
        settings::Values& v = settings::get();
        switch (set_what[i]) {
        case menu_model::kVolume: {
            int vol = v.volume + (dir < 0 ? -10 : dir > 0 ? 10 : 0);
            vol = vol < 0 ? 0 : vol > 100 ? 100 : vol;
            if (vol == v.volume) return;
            v.volume = uint8_t(vol);
            audio::set_volume(vol);
            break;
        }
        case menu_model::kSwap:
            v.swap_ab = !v.swap_ab;
            pad_set_swap_ab(v.swap_ab);
            break;
        case menu_model::kRotate:
            v.rotate180 = !v.rotate180;
            settings::save();
            screen::apply_rotation();
            menu_model::refresh_settings(set_items, set_what, settings.count, settings_state());
            draw();
            return;
        case menu_model::kSound:
            // The pin's driver is chosen at start-up: restart with the other
            v.sound_pwm = !v.sound_pwm;
            settings::save();
            ui::message(g(), "Restarting", v.sound_pwm ? "Sound by PWM" : "Sound by the DAC");
            vTaskDelay(pdMS_TO_TICKS(800));
            esp_restart();
            return;
        default:
            return;
        }
        settings::save();
        menu_model::refresh_settings(set_items, set_what, settings.count, settings_state());
        ui::draw_row(g(), settings, i);
    }

    void choose_setting(int i)
    {
        if (i < 0 || i >= settings.count) return;
        switch (set_what[i]) {
        case menu_model::kVolume:
            return;   // the - + keys
        case menu_model::kSwap:
        case menu_model::kRotate:
        case menu_model::kSound:
            change_setting(i, 0);
            return;
        case menu_model::kTest:
            pad_test::run(*ring);
            in.wait_for_release();
            touch::wait_release();
            draw();
            return;
        case menu_model::kForget:
            pad_forget_all();
            set_news("Controllers forgotten: pair them again");
            return;
        case menu_model::kCalibrate:
            touch::calibrate();
            draw();
            return;
        case menu_model::kRoms:
            open_text("How to Add Games", menu_model::kRomsHelp, menu_model::kRomsHelpCount, Page::Settings);
            return;
        case menu_model::kAbout:
            open_about();
            return;
        default:
            return;
        }
    }

    void choose(int i)
    {
        if (page == Page::Home) choose_home(i);
        else if (page == Page::Settings) choose_setting(i);
    }

    void back()
    {
        if (page == Page::Settings) open(Page::Home);
        else if (page == Page::Text) open(text_back);
    }

    // ---- touch ----

    void on_touch(const touch::Event& e)
    {
        ui::ListPage* l = list();
        if (e.kind == touch::Event::Down) {
            down = l ? ui::hit_list(g(), *l, e.x, e.y) : ui::hit_text(g(), e.x, e.y);
            if (l && down.hit == ui::Hit::Row) {
                l->pressed = down.index;
                ui::draw_row(g(), *l, down.index);
            }
            return;
        }
        if (e.kind != touch::Event::Up) return;
        const ui::Target t = down;
        down = ui::Target{};
        if (l && l->pressed >= 0) {
            const int was = l->pressed;
            l->pressed = -1;
            if (t.hit == ui::Hit::Row) {
                const int old = l->focus;
                l->focus = was;          // the frame follows the finger
                ui::draw_row(g(), *l, old);
                ui::draw_row(g(), *l, was);
            } else {
                ui::draw_row(g(), *l, was);
            }
        }
        switch (t.hit) {
        case ui::Hit::Back: back(); break;
        case ui::Hit::Gear: open(Page::Settings); break;
        case ui::Hit::Prev: if (l) turn_page(*l, -1); break;
        case ui::Hit::Next: if (l) turn_page(*l, +1); break;
        case ui::Hit::Minus: change_setting(t.index, -1); break;
        case ui::Hit::Plus: change_setting(t.index, +1); break;
        case ui::Hit::Row: choose(t.index); break;
        case ui::Hit::None: break;
        }
    }

    // ---- controller ----

    void on_buttons(uint32_t p)
    {
        if (!p) return;
        if (page == Page::Text) {
            if (p & (PAD_A | PAD_B | PAD_START)) back();
            return;
        }
        ui::ListPage& l = *list();
        if (p & PAD_UP) move_focus(l, l.focus - 1);
        else if (p & PAD_DOWN) move_focus(l, l.focus + 1);
        else if (p & PAD_L) turn_page(l, -1);
        else if (p & PAD_R) turn_page(l, +1);
        else if (p & (PAD_LEFT | PAD_RIGHT)) {
            const int dir = (p & PAD_RIGHT) ? 1 : -1;
            const bool setting = page == Page::Settings && l.focus >= 0 && l.focus < l.count &&
                                 l.items[l.focus].widget != ui::Widget::Chevron &&
                                 l.items[l.focus].widget != ui::Widget::None;
            if (setting) change_setting(l.focus, dir);
            else turn_page(l, dir);
        } else if (p & (PAD_A | PAD_START)) {
            choose(l.focus);
        } else if (p & PAD_B) {
            back();
        }
    }

    // ---- news from the USB loader ----

    void poll_usb()
    {
        const bool busy = serial_link::busy();
        if (busy != receiving) {
            receiving = busy;
            footer.news = shown_news();
            ui::draw_footer(g(), footer);
        }
        if (serial_link::take_changed()) {
            char buf[48];
            const int n = catalog::available_count();
            std::snprintf(buf, sizeof buf, "ROMs loaded: %d game%s ready", n, n == 1 ? "" : "s");
            set_news(buf, false);
            build_home();
            draw();
        }
    }
};

}  // namespace

void run(AudioRing& ring)
{
    static Menu m;
    m.ring = &ring;
    if (!audio::running()) std::snprintf(m.news, sizeof m.news, "No sound: the audio output didn't start");
    m.build_home();
    m.read_pads(true);
    // The last start ended in a crash or a watchdog reset: say what was
    // going on (crash_note), so a report can name it
    char crash[160];
    if (crash_note::last(crash, sizeof crash)) {
        m.g().fillScreen(ui::kBg);
        ui::message(m.g(), "The board restarted", crash, ui::kRed);
        const uint32_t t0 = now_ms();
        while (now_ms() - t0 < 8000 && touch::poll().kind != touch::Event::Up) {
            m.in.update(now_ms());
            if (m.in.pressed()) break;
            vTaskDelay(pdMS_TO_TICKS(16));
        }
        std::snprintf(m.news, sizeof m.news, "Last start: %s", crash);
        m.in.wait_for_release();
    }
    m.draw();
    for (;;) {
        m.read_pads(false);
        m.poll_usb();
        m.in.update(now_ms());
        m.on_buttons(m.in.pressed());
        m.on_touch(touch::poll());
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

}  // namespace menu
