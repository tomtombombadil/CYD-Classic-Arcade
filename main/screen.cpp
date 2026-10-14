// Panel + text helpers (see screen.h).
#include "screen.h"

#include "settings.h"

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

void apply_rotation() { s_lcd.setRotation(settings::get().usb_top ? 2 : 0); }

void menu_font()
{
    if (s_lcd.width() >= 320)
        s_lcd.setFont(&fonts::Font4);
    else
        s_lcd.setFont(&fonts::Font2);
}

int line_h() { return s_lcd.fontHeight() + 6; }

void line(int y, uint16_t color, const char* text, uint16_t bg)
{
    s_lcd.fillRect(0, y, s_lcd.width(), line_h(), bg);
    s_lcd.setTextColor(color, bg);
    s_lcd.drawString(text, 8, y + 3);
}

}  // namespace screen
