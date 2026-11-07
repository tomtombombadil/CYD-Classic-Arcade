// Panel + text helpers (see screen.h).
#include "screen.h"

#include "settings.h"

#include <cstring>

#if BOARD_RGB_PANEL
#include "fb_push.h"
#include "rgb_fallback.h"

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>
#endif

namespace screen {
namespace {

LGFX s_lcd;

#if BOARD_RGB_PANEL
fb_push::Frame s_frame;
#endif

void set_rotation(int r)
{
    s_lcd.setRotation(uint8_t((r + BOARD_ROTATION_OFFSET) & 3));
#if BOARD_RGB_PANEL
    s_frame.rot = (r + BOARD_ROTATION_OFFSET) & 3;
#endif
}

}  // namespace

LGFX& lcd() { return s_lcd; }

#if BOARD_RGB_PANEL
// The 5.0" RGB board: the frame buffer (750 KB) can only live in PSRAM.
// Without it LovyanGFX's RGB bus would stream from a null buffer - no
// picture at all - so a message goes up by other means (rgb_fallback).
bool init()
{
    constexpr size_t kFrame = size_t(BOARD_NATIVE_W) * BOARD_NATIVE_H * 2;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < kFrame) {
        static const char* const kLines[] = {
            "This firmware needs an ESP32-S3 with 8 MB",
            "octal PSRAM (N16R8).",
            "The board report is in the log: USB serial 115200 baud,",
            "or Download Log on the web page's Load ROMs.",
        };
        rgb_fallback::show("No PSRAM found", kLines, 4);
        return false;
    }
    // GPIO 19 / 20 (the touch's I2C) are the S3's USB pins: take the
    // USB-Serial-JTAG pad off them (USB here is the CH340C on UART0)
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE | USB_SERIAL_JTAG_DP_PULLUP);
    // GT911: INT held low while LovyanGFX pulses its reset picks address
    // 0x5D (both are tried anyway). INT is also the amp's LRCLK - released
    // before the sound starts.
    gpio_reset_pin(gpio_num_t(BOARD_PIN_TOUCH_INT));
    gpio_set_direction(gpio_num_t(BOARD_PIN_TOUCH_INT), GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num_t(BOARD_PIN_TOUCH_INT), 0);
    const bool ok = s_lcd.init();   // native landscape, rotation 0: the RGB bus sizes its frame from it
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction(gpio_num_t(BOARD_PIN_TOUCH_INT), GPIO_MODE_INPUT);
    // The GT911 may not answer that soon after its reset: once more, later
    lgfx::ITouch* t = s_lcd.touch();
    if (t && !t->init()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!t->init()) ESP_LOGE("screen", "GT911 touch didn't answer");
    }
    s_frame.fb = s_lcd.frame_buffer();
    s_frame.stride = BOARD_NATIVE_W;
    s_frame.pw = BOARD_NATIVE_W;
    s_frame.ph = BOARD_NATIVE_H;
    if (!ok || !s_frame.fb) {
        ESP_LOGE("screen", "RGB panel didn't start");
        return false;
    }
    apply_rotation();
    s_lcd.setBrightness(200);
    s_lcd.fillScreen(kBg);
    menu_font();
    return true;
}

void push(int x, int y, int w, int h, const uint16_t* px) { fb_push::write(s_frame, x, y, w, h, px); }

// Nothing to flush: the panel's DMA reads bounce buffers that its
// interrupt fills from the frame buffer through the data cache, so what the
// CPU wrote is seen as it is (v0.26.0 wrote the rows back to PSRAM here for
// a DMA that read PSRAM itself)
void pushed()
{
    s_frame.row0 = 1 << 30;
    s_frame.row1 = -1;
}

#else

bool init()
{
    s_lcd.init();
    apply_rotation();
    s_lcd.setBrightness(200);
    s_lcd.fillScreen(kBg);
    menu_font();
    return true;
}

void push(int x, int y, int w, int h, const uint16_t* px)
{
    s_lcd.pushImage(x, y, w, h, reinterpret_cast<const lgfx::swap565_t*>(px));
}

void pushed() {}

#endif

void apply_rotation() { set_rotation(settings::get().rotate180 ? 0 : 2); }
void landscape() { set_rotation(settings::get().rotate180 ? 1 : 3); }

// A size smaller than v0.6 (Tom: the menu ran into the help text on the
// 2.8"; "it could all be a little smaller")
void menu_font()
{
    if (s_lcd.width() >= 480)
        s_lcd.setFont(&fonts::DejaVu24);
    else if (s_lcd.width() >= 320)
        s_lcd.setFont(&fonts::DejaVu18);
    else
        s_lcd.setFont(&fonts::DejaVu12);
}

void small_font()
{
    if (s_lcd.width() >= 480)
        s_lcd.setFont(&fonts::DejaVu18);
    else if (s_lcd.width() >= 320)
        s_lcd.setFont(&fonts::DejaVu12);
    else
        s_lcd.setFont(&fonts::DejaVu9);
}

int line_h() { return s_lcd.fontHeight() + 4; }

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
