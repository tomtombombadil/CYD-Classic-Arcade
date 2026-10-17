// Menu preview on the PC: the firmware's menu pages (main/ui.*, main/
// menu_model.*) drawn by LovyanGFX onto sprites the size of each panel, and
// written as PPM files - to check the layouts at 240x320 and 320x480 (no
// clipping) and to show them. Build: tools/menu_preview/build.sh.
#include "ui.h"
#include "menu_model.h"

#include "emu/games.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string g_out = ".";

void save(LGFX_Sprite& s, const char* name)
{
    char path[512];
    std::snprintf(path, sizeof path, "%s/%dx%d_%s.ppm", g_out.c_str(), s.width(), s.height(), name);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", s.width(), s.height());
    for (int y = 0; y < s.height(); y++)
        for (int x = 0; x < s.width(); x++) {
            const uint16_t c = s.readPixel(x, y);
            const unsigned char p[3] = {(unsigned char)((c >> 11) << 3 | (c >> 13)),
                                        (unsigned char)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                                        (unsigned char)((c & 31) << 3 | ((c >> 2) & 7))};
            std::fwrite(p, 1, 3, f);
        }
    std::fclose(f);
}

ui::Footer footer(int pads, const char* news)
{
    ui::Footer f;
    f.news = news;
    if (pads >= 1) {
        f.pads[0].connected = true;
        std::snprintf(f.pads[0].name, sizeof f.pads[0].name, "8BitDo Ultimate");
    }
    if (pads >= 2) {
        f.pads[1].connected = true;
        std::snprintf(f.pads[1].name, sizeof f.pads[1].name, "Xbox Wireless");
    }
    return f;
}

void render(int w, int h)
{
    LGFX_Sprite s;
    s.setColorDepth(16);
    if (!s.createSprite(w, h)) return;
    const ui::Metrics m = ui::metrics(s);
    std::printf("%dx%d: header %d, %d rows of %d, pager %d, bottom %d+%d\n", w, h, m.header_h, m.rows, m.row_h,
                m.pager_h, m.news_h, m.pads_h);

    static ui::Item items[menu_model::kMaxItems];
    static int what[menu_model::kMaxItems];
    bool avail[64] = {};
    for (int g = 0; g < games::kGameCount; g++) avail[g] = true;
    avail[1] = avail[9] = avail[14] = false;     // a few without ROMs

    ui::ListPage home;
    home.title = "CYD Classic Arcade";
    home.gear = true;
    home.items = items;
    home.count = menu_model::build_home(items, what, avail);
    home.focus = 0;
    ui::draw_list(s, home, footer(1, "Galaga: 60.6 fps, 60 shown/s"));
    save(s, "1_home");

    home.page = ui::page_count(m, home.count) - 1;
    home.focus = home.page * m.rows + 1;
    home.pressed = home.page * m.rows;
    ui::draw_list(s, home, footer(2, ""));
    save(s, "2_home_last_page");

    bool none[64] = {};
    ui::ListPage empty = home;
    empty.count = menu_model::build_home(items, what, none);
    empty.page = 0;
    empty.focus = -1;
    empty.pressed = -1;
    ui::draw_list(s, empty, footer(0, ""));
    save(s, "3_home_no_roms");

    // Settings
    static ui::Item set_items[menu_model::kMaxItems];
    static int set_what[menu_model::kMaxItems];
    menu_model::SettingsState st;
    st.volume = 50;
    st.swap_ab = true;
    ui::ListPage settings;
    settings.title = "Settings";
    settings.back = true;
    settings.items = set_items;
    settings.count = menu_model::build_settings(set_items, set_what, st);
    settings.focus = 0;
    ui::draw_list(s, settings, footer(1, ""));
    save(s, "4_settings");
    if (ui::page_count(m, settings.count) > 1) {
        settings.page = 1;
        settings.focus = -1;
        ui::draw_list(s, settings, footer(1, "Controllers forgotten: pair them again"));
        save(s, "5_settings_2");
    }

    ui::TextPage help;
    help.title = "How to Add Games";
    help.lines = menu_model::kRomsHelp;
    help.count = menu_model::kRomsHelpCount;
    ui::draw_text(s, help, footer(1, ""));
    save(s, "6_help");

    char about_buf[16][64];
    const char* about_lines[16];
    menu_model::AboutInfo a;
    a.version = "v0.13.0";
    a.board = w >= 320 ? "4.0\" ST7796 Resistive" : "2.8\" ST7789 Resistive";
    a.ram_free_kb = 141;
    a.ram_block_kb = 107;
    a.rom_free_kb = 412;
    a.games_ready = 13;
    a.games_total = games::kGameCount;
    a.paired = 2;
    ui::TextPage about;
    about.title = "About";
    about.lines = about_lines;
    about.count = menu_model::build_about(a, about_buf, about_lines, 16);
    ui::draw_text(s, about, footer(1, ""));
    save(s, "7_about");

    {
        char sp_buf[12][64];
        const char* sp_lines[12];
        menu_model::SpeedInfo sp;
        sp.game = "Galaga";
        sp.game_hz = 60.6f;
        sp.frames_per_s = 57.3f;
        sp.drawn_per_s = 49;
        sp.emu_ms = 11.8f;
        sp.emu_max_ms = 19.4f;
        sp.draw_ms = 5.1f;
        sp.draw_max_ms = 9.7f;
        sp.send_ms = 3.2f;
        sp.kpixels = 13.5f;
        sp.worst_frames = 51;
        sp.worst_drawn = 38;
        ui::TextPage speed;
        speed.title = "Last Game Speed";
        speed.lines = sp_lines;
        speed.count = menu_model::build_speed(sp, sp_buf, sp_lines, 12);
        ui::draw_text(s, speed, footer(1, ""));
        save(s, "7b_speed");
    }

    {   // Settings > Log: a paged text page, newest first
        static const char* const lines[] = {
            "[4 312.5s] galaga: ran 280 s, 60.6 fps (game 60.6), 60 shown/s; emu 11.8 ms (worst 15.2), picture 6.1 (worst 9.4, send 3.2, 13.5K px); slowest second 60 frames, 58 shown",
            "[4 32.1s] galaga: machine 64 KB, game area used 92 of 98 KB, ROM 24 KB in RAM / 8 KB from flash; drawn on core 0",
            "[4 31.6s] galaga: start; game area 98 KB; RAM outside 41 KB free, block 30 KB",
            "[4 6.2s] BT: controller 1 ready: 8BitDo Ultimate",
            "[4 5.9s] BT: connected 8BitDo Ultimate 2.4G (e4:17:d8:12:34:56)",
            "[4 3.1s] Game area 98 KB; RAM left 41 KB, block 30 KB; draw task on core 0",
            "[4 3.0s] Ready: RAM 139 KB free, block 108 KB",
            "[4 2.4s] Games ready 14 of 16; 16 zips stored, 812 KB free; no ROMs: spyhunt rthunder",
            "[4 1.2s] BT: started, 1 controller(s) paired",
            "[4 0.6s] Backtrace: 400d4a2c 400d51f0 400e0a11 4008a8f2",
            "[4 0.6s] Crash: LoadProhibited: Exception was unhandled. (task ui, core 1, at 400d4a2c)",
            "[4 0.6s] RESET: Qix crashed while making its machine (77 KB free, block 28 KB).",
            "[4 0.5s] Start v0.15.0 (abc1234) on 4.0\" ST7796 Resistive; reset: crash",
            "[3 845.0s] qix: start; game area 98 KB; RAM outside 41 KB free, block 30 KB",
            "[3 610.2s] robotron: ran 220 s, 60.1 fps (game 60.1), 41 shown/s; emu 12.0 ms (worst 14.9), picture 9.8",
        };
        ui::TextPage log;
        log.title = "Log";
        log.lines = lines;
        log.count = int(sizeof lines / sizeof lines[0]);
        log.paged = true;
        log.small = true;
        int starts[32];
        const int pages = ui::text_pages(s, log, starts, 32);
        ui::draw_text(s, log, footer(1, ""), starts, pages);
        save(s, "7c_log");
        log.page = pages - 1;
        ui::draw_text(s, log, footer(1, ""), starts, pages);
        save(s, "7d_log_last");
    }

    char info_buf[12][96];
    const char* info_lines[12];
    ui::TextPage info;
    info.title = "Q*bert";
    info.lines = info_lines;
    info.count = menu_model::build_game_info(9, info_buf, info_lines, 12);
    ui::draw_text(s, info, footer(1, ""));
    save(s, "8_game_info");

    ui::loading(s, "Galaga", "", 0, 870, true);
    ui::loading(s, "Galaga", "", 520, 870, false);
    save(s, "A_loading");
    {   // a landscape game: the panel turned
        LGFX_Sprite l;
        l.setColorDepth(16);
        if (l.createSprite(h, w)) {
            ui::loading(l, "Defender", "", 600, 1625, true);
            ui::loading(l, "Defender", "", 1100, 1625, false);
            save(l, "B_loading_landscape");
        }
    }

    home.page = 0;
    home.focus = 0;
    home.pressed = -1;
    ui::draw_list(s, home, footer(0, ""));
    ui::message(s, "No controller",
                "Put one in pairing mode to play. To leave a game, touch and hold the screen.");
    save(s, "9_no_controller");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc > 1) g_out = argv[1];
    render(240, 320);
    render(320, 480);
    return 0;
}
