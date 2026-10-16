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
    ui::TextPage about;
    about.title = "About";
    about.lines = about_lines;
    about.count = menu_model::build_about(a, about_buf, about_lines, 16);
    ui::draw_text(s, about, footer(1, ""));
    save(s, "7_about");

    char info_buf[12][96];
    const char* info_lines[12];
    ui::TextPage info;
    info.title = "Q*bert";
    info.lines = info_lines;
    info.count = menu_model::build_game_info(9, info_buf, info_lines, 12);
    ui::draw_text(s, info, footer(1, ""));
    save(s, "8_game_info");

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
