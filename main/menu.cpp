// The menus (see menu.h): Home (the games), Settings and text pages, drawn
// by ui.* from menu_model.*'s rows, driven by touch and the controllers
// alike.
//
// Touch: tap a row, a key, the header's < (back) or gear (Settings); < >
// keys change pages. Controller: up / down move the gold frame (pages
// follow), A or Start chooses, B goes back, left / right change a setting
// or the page, L / R change the page.
#include "menu.h"

#include "arena.h"
#include "audio_out.h"
#include "catalog.h"
#include "crash_note.h"
#include "devlog.h"
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

#include "core/log_pack.h"
#include "emu/games.h"
#include "qrcodegen.h"

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
    int last_game_ = -1;    // for Last Game Speed

    ui::Item home_items[kMaxItems];
    int home_what[kMaxItems] = {};
    ui::ListPage home;

    ui::Item set_items[kMaxItems];
    int set_what[kMaxItems] = {};
    ui::ListPage settings;

    // The open text page and where its < goes (the Send Log page is a text
    // page showing `qr` instead)
    ui::TextPage text;
    ui::QrPage qr;
    bool qr_open = false;
    char qr_note[48] = "";
    Page text_back = Page::Home;
    const char* text_lines[24] = {};
    char text_buf[16][96] = {};
    // Paged text pages (the Log): where each page starts
    int text_starts[48] = {};
    int text_pages = 1;

    ui::Footer footer;
    char news[72] = "";
    bool receiving = false;
    bool usb = false;       // Settings > USB Transfer is open (the loader runs)

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
        s.demo_sounds = v.demo_sounds;
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
        if (page == Page::Text && qr_open) ui::draw_qr(g(), qr, footer);
        else if (page == Page::Text) ui::draw_text(g(), text, footer, text_starts, text_pages);
        else ui::draw_list(g(), *list(), footer);
    }

    void open(Page p)
    {
        page = p;
        if (p == Page::Settings) build_settings();
        draw();
    }

    void open_text(const char* title, const char* const* lines, int count, Page back, bool paged = false)
    {
        qr_open = false;
        text.title = title;
        text.lines = lines;
        text.count = count;
        text.paged = paged;
        text.small = paged;
        text.page = 0;
        text_pages = paged ? ui::text_pages(g(), text, text_starts, int(sizeof text_starts / sizeof text_starts[0])) : 1;
        text_back = back;
        page = Page::Text;
        draw();
    }

    void turn_text_page(int dir)
    {
        const int p = text.page + dir;
        if (!text.paged || p < 0 || p >= text_pages) return;
        text.page = p;
        draw();
    }

    // Settings > Log: the board's log, newest first. Its lines are copied
    // into the game area while the page is open (no game runs then; the
    // next game empties it), so the menu keeps no buffer for them.
    void open_log()
    {
        arena::reset();
        constexpr size_t kLine = devlog::kLineMax;
        const int want = devlog::count();
        int n = 0;
        const char** lines = static_cast<const char**>(arena::alloc(sizeof(char*) * size_t(want > 0 ? want : 1)));
        char* buf = static_cast<char*>(arena::alloc(kLine * size_t(want > 0 ? want : 1)));
        if (lines && buf)
            for (int i = 0; i < want; ++i)
                if (devlog::line(i, buf + kLine * size_t(n), kLine)) {
                    lines[n] = buf + kLine * size_t(n);
                    ++n;
                }
        if (!lines || !buf || n == 0) {
            static const char* const kEmpty[] = {"The log is empty."};
            open_text("Log", kEmpty, 1, Page::Settings);
            return;
        }
        open_text("Log", lines, n, Page::Settings, true);
    }

    // Settings > Send Log (Tom, 2026-10-20: "we need a better way to
    // transfer the logs"): as CYD Classic Games does - the newest lines that
    // fit, behind a header, packed (core/log_pack.*) after the address of
    // the site's log page (web/l/) into one QR code: a phone's camera opens
    // the page, which offers to email it to the developer. Everything is
    // in the game area while the page is open (the deflate tables are 16
    // KB), like the Log page's lines; the next game empties it.
    static bool qr_dark(const void* ctx, int x, int y)
    {
        return qrcodegen_getModule(static_cast<const uint8_t*>(ctx), x, y);
    }

    void open_send_log()
    {
        static const char* const kNone[] = {""};
        open_text("Send Log", kNone, 0, Page::Settings);
        qr_open = true;
        qr = ui::QrPage{};
        qr.title = "Send Log";
        qr.caption = "Point your phone's camera at the code to get a link that sends the log to the developer.";
        qr.note = "Newest 000 of 000 lines";    // its height counts for the room
        int maxv = (ui::qr_room(g(), qr) / 2 - 4 - 17) / 4;     // modules at least 2 pixels
        if (maxv > qrcodegen_VERSION_MAX) maxv = qrcodegen_VERSION_MAX;

        arena::reset();
        constexpr size_t kLine = devlog::kLineMax;
        const int total = devlog::count();
        const size_t cap = 256 + kLine * size_t(total > 0 ? total : 1);
        const size_t url_n = std::strlen(logpack::kUrl);
        const size_t qlen = maxv >= 1 ? qrcodegen_BUFFER_LEN_FOR_VERSION(maxv) : 0;
        char* lines = static_cast<char*>(arena::alloc(kLine * size_t(total > 0 ? total : 1)));
        char* report = static_cast<char*>(arena::alloc(cap));
        uint8_t* work = static_cast<uint8_t*>(arena::alloc(cap + 64));
        const size_t b43_cap = (cap + 64) * 3 / 2 + 4;
        char* b43 = static_cast<char*>(arena::alloc(b43_cap));
        void* tables = arena::alloc(logpack::kTablesBytes);
        uint8_t* ub = static_cast<uint8_t*>(arena::alloc(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_BYTE, url_n)));
        uint8_t* ab = static_cast<uint8_t*>(
            arena::alloc(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_ALPHANUMERIC, b43_cap)));
        uint8_t* code = qlen ? static_cast<uint8_t*>(arena::alloc(qlen)) : nullptr;
        uint8_t* tmp = qlen ? static_cast<uint8_t*>(arena::alloc(qlen)) : nullptr;
        int n = 0;
        if (lines)
            for (int i = 0; i < total; ++i)
                if (devlog::line(i, lines + kLine * size_t(n), kLine)) ++n;

        // The report with the newest `k` lines, oldest first
        const auto build = [&](int k) -> size_t {
            int len = std::snprintf(report, cap, "CYD Classic Arcade log\nBoard: %s\nFirmware: %s%s%s%s\nLines: newest %d of %d\n",
                                    BOARD_NAME, CYD_ARCADE_VERSION, CYD_ARCADE_BUILD[0] ? " (" : "", CYD_ARCADE_BUILD,
                                    CYD_ARCADE_BUILD[0] ? ")" : "", k, n);
            if (len < 0 || size_t(len) >= cap) return 0;
            for (int i = k - 1; i >= 0; --i) {
                const int w = std::snprintf(report + len, cap - size_t(len), "%s\n", lines + kLine * size_t(i));
                if (w < 0 || size_t(len + w) >= cap) return 0;
                len += w;
            }
            return size_t(len);
        };
        const auto encode = [&](int k, bool final) -> bool {
            const size_t len = build(k);
            const size_t z = len ? logpack::pack(report, len, work, cap + 64, b43, b43_cap, tables) : 0;
            if (!z) return false;
            qrcodegen_Segment segs[2] = {qrcodegen_makeBytes(reinterpret_cast<const uint8_t*>(logpack::kUrl), url_n, ub),
                                         qrcodegen_makeAlphanumeric(b43, ab)};
            return qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, maxv,
                                                    final ? qrcodegen_Mask_AUTO : qrcodegen_Mask_0, final, tmp, code);
        };
        int best = -1;
        if (lines && report && work && b43 && tables && ub && ab && code && tmp) {
            int lo = 0, hi = n;     // as many of the newest lines as fit
            while (lo <= hi) {
                const int mid = (lo + hi) / 2;
                if (encode(mid, false)) { best = mid; lo = mid + 1; }
                else hi = mid - 1;
            }
        }
        if (best >= 0 && encode(best, true)) {
            qr.n = qrcodegen_getSize(code);
            qr.dark = qr_dark;
            qr.ctx = code;
            if (best == n) std::snprintf(qr_note, sizeof qr_note, "All %d lines of the log", n);
            else std::snprintf(qr_note, sizeof qr_note, "Newest %d of %d lines", best, n);
            devlog::add("Send Log: %d of %d lines in a %d x %d QR code", best, n, qr.n, qr.n);
        } else {
            std::snprintf(qr_note, sizeof qr_note, "%s", "The log didn't fit a QR code.");
            qr.caption = "";
            devlog::add("Send Log: no QR code (game area %u KB)", unsigned(arena::size() / 1024));
        }
        qr.note = qr_note;
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
        a.area_kb = arena::size() / 1024;
        a.area_peak_kb = arena::peak() / 1024;
        {
            flash_store::Lock lock;
            a.rom_free_kb = unsigned(flash_store::store().free_bytes() / 1024);
        }
        a.games_ready = catalog::available_count();
        a.games_total = games::kGameCount;
        a.paired = pad_paired_count();
        const int n = menu_model::build_about(a, buf, text_lines, 16);
        open_text("About", text_lines, n, Page::Settings);
    }

    // Settings > USB Transfer: the ROM loader runs only while this page is
    // open (Tom, 2026-10-20: ROMs go onto a board about once - "let's not
    // load it all the time"). Its task and port driver take ~11.7 KB, so the
    // (empty) game area is given back first and taken again on leaving.
    void open_usb()
    {
        game_runner::release_area();
        if (!serial_link::start()) {
            game_runner::reserve_area();
            if (settings::get().usb_transfer) {
                settings::get().usb_transfer = false;
                settings::save();
            }
            devlog::add("USB Transfer couldn't start: %u KB free",
                        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
            set_news("USB Transfer couldn't start: not enough memory");
            return;
        }
        usb = true;
        if (!settings::get().usb_transfer) {
            settings::get().usb_transfer = true;
            settings::save();
        }
        devlog::add("USB Transfer open");
        set_news("Waiting for the Load ROMs page", false);
        open_text("USB Transfer", menu_model::kUsbHelp, menu_model::kUsbHelpCount, Page::Settings);
    }

    // False while ROMs are still coming in (the page stays)
    bool close_usb()
    {
        if (!usb) return true;
        if (!serial_link::stop()) {
            set_news("Wait: ROMs are still coming in");
            return false;
        }
        usb = false;
        game_runner::reserve_area();
        settings::get().usb_transfer = false;
        settings::save();
        devlog::add("USB Transfer closed");
        if (std::strcmp(news, "Waiting for the Load ROMs page") == 0) news[0] = 0;
        return true;
    }

    void open_speed()
    {
        static char buf[12][64];
        menu_model::SpeedInfo a;
        const game_runner::Stats& st = game_runner::last_stats();
        if (st.frames_per_s > 0 && last_game_ >= 0) {
            a.game = games::kGames[last_game_].title;
            a.game_hz = st.game_hz;
            a.frames_per_s = st.frames_per_s;
            a.drawn_per_s = st.drawn_per_s;
            a.emu_ms = st.emu_ms;
            a.emu_max_ms = st.emu_max_ms;
            a.draw_ms = st.draw_ms;
            a.draw_max_ms = st.draw_max_ms;
            a.send_ms = st.send_ms;
            a.kpixels = st.kpixels;
            a.worst_frames = st.worst_frames;
            a.worst_drawn = st.worst_drawn;
        }
        const int n = menu_model::build_speed(a, buf, text_lines, 12);
        open_text("Last Game Speed", text_lines, n, Page::Settings);
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
        last_game_ = game;
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
        case menu_model::kDemoSounds:
            v.demo_sounds = !v.demo_sounds;
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
        case menu_model::kDemoSounds:
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
        case menu_model::kUsb:
            open_usb();
            return;
        case menu_model::kSpeed:
            open_speed();
            return;
        case menu_model::kLog:
            open_log();
            return;
        case menu_model::kSendLog:
            open_send_log();
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
        else if (page == Page::Text && close_usb()) open(text_back);
    }

    // ---- touch ----

    void on_touch(const touch::Event& e)
    {
        ui::ListPage* l = list();
        if (e.kind == touch::Event::Down) {
            down = l ? ui::hit_list(g(), *l, e.x, e.y) : ui::hit_text(g(), e.x, e.y, text.paged);
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
        case ui::Hit::Prev: if (l) turn_page(*l, -1); else turn_text_page(-1); break;
        case ui::Hit::Next: if (l) turn_page(*l, +1); else turn_text_page(+1); break;
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
            if (p & (PAD_LEFT | PAD_L | PAD_UP)) turn_text_page(-1);
            else if (p & (PAD_RIGHT | PAD_R | PAD_DOWN)) turn_text_page(+1);
            else if (p & (PAD_A | PAD_B | PAD_START)) back();
            return;
        }
        // Select: Settings from the game list, and back (Tom, 2026-10-17:
        // there was no way to Settings with a controller)
        if (p & PAD_SELECT) {
            open(page == Page::Home ? Page::Settings : Page::Home);
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
            devlog::add("USB Transfer: %d game%s ready", n, n == 1 ? "" : "s");
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
    // It stays until a tap or a button (Tom: it went by too fast to copy);
    // the details are in Settings > Log
    if (const char* crash = crash_note::start_message()) {
        char body[220];
        std::snprintf(body, sizeof body, "%s Details: Settings > Log.", crash);
        m.g().fillScreen(ui::kBg);
        ui::message(m.g(), "The board restarted", body, ui::kRed);
        touch::wait_tap_or_button();
        std::snprintf(m.news, sizeof m.news, "%s", "The board restarted: see Settings > Log");
        m.in.wait_for_release();
    }
    // The board restarted with USB Transfer open (opening the port resets
    // CH340 boards on Windows): straight back to it, the loader running
    if (settings::get().usb_transfer) {
        m.page = Page::Settings;
        m.build_settings();
        m.open_usb();
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
