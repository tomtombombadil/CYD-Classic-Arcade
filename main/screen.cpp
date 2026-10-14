// Panel + text helpers (see screen.h).
#include "screen.h"

#include "settings.h"

#include <cstring>

namespace screen {
namespace {

LGFX s_lcd;

}  // namespace

LGFX& lcd() { return s_lcd; }

void init()
{
    s_lcd.init();
    apply_rotation();
    s_lcd.setBrightness(200);
    s_lcd.fillScreen(kBg);
    menu_font();
}

void apply_rotation() { s_lcd.setRotation(settings::get().rotate180 ? 0 : 2); }
void landscape() { s_lcd.setRotation(settings::get().rotate180 ? 1 : 3); }

void menu_font()
{
    if (s_lcd.width() >= 320)
        s_lcd.setFont(&fonts::Font4);
    else
        s_lcd.setFont(&fonts::Font2);
}

void small_font() { s_lcd.setFont(&fonts::Font2); }

int line_h() { return s_lcd.fontHeight() + 6; }

void line(int y, uint16_t color, const char* text, uint16_t bg)
{
    s_lcd.fillRect(0, y, s_lcd.width(), line_h(), bg);
    s_lcd.setTextColor(color, bg);
    s_lcd.drawString(text, 8, y + 3);
}

int text_box(int y, uint16_t color, const char* text, bool draw)
{
    const int max_w = s_lcd.width() - 16;
    const int h = line_h();
    char buf[96];
    int used = 0;
    const char* p = text;
    for (;;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        // as many whole words as fit (a word too long alone is cut by the edge)
        int fit = 0, i = 0;
        for (;;) {
            int end = i;
            while (p[end] && p[end] != ' ') ++end;
            if (end >= int(sizeof buf)) break;
            std::memcpy(buf, p, size_t(end));
            buf[end] = 0;
            if (s_lcd.textWidth(buf) > max_w) {
                if (fit == 0) fit = end;
                break;
            }
            fit = end;
            if (!p[end]) break;
            i = end;
            while (p[i] == ' ') ++i;
        }
        if (fit == 0) fit = int(sizeof buf) - 1;
        std::memcpy(buf, p, size_t(fit));
        buf[fit] = 0;
        if (draw) line(y + used, color, buf);
        used += h;
        p += fit;
    }
    return used;
}

}  // namespace screen
