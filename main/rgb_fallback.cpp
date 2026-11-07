// A message on an RGB panel without PSRAM (see rgb_fallback.h).
//
// An RGB panel has no memory of its own: the ESP32-S3 streams every frame
// to it. LovyanGFX's RGB bus keeps the whole frame in PSRAM (800x480x2 =
// 750 KB) - with no PSRAM there is no picture at all, only a black screen.
// So this uses ESP-IDF's own RGB driver in its "no frame buffer" mode: two
// small bounce buffers in internal RAM that the driver asks us to fill as
// the frame goes out (on_bounce_empty, from its interrupt). The message is
// a 1-bit picture of a band across the middle of the screen, drawn once
// with LovyanGFX onto a sprite; every other line is plain navy.
#include "rgb_fallback.h"

#include "board_select.h"

#if BOARD_RGB_PANEL

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_log.h>

#include <cstring>

namespace rgb_fallback {
namespace {

constexpr const char* kTag = "rgb_fallback";
constexpr int kW = BOARD_NATIVE_W, kH = BOARD_NATIVE_H;
constexpr int kBandH = 200;                  // the message's rows
constexpr int kBandY = (kH - kBandH) / 2;
constexpr int kBounceLines = 10;             // 800 x 10 px a bounce buffer (2 x 16 KB)
// Native RGB565 (ESP-IDF's driver sends the low byte on D0-D7)
constexpr uint16_t kNavy = 0x10A8, kCream = 0xFFDB;

uint8_t* s_bits = nullptr;                   // kW x kBandH, 1 bit a pixel, MSB first
esp_lcd_panel_handle_t s_panel = nullptr;

IRAM_ATTR bool fill(esp_lcd_panel_handle_t, void* buf, int pos_px, int len_bytes, void*)
{
    uint16_t* out = static_cast<uint16_t*>(buf);
    const int n = len_bytes / 2;
    int y = pos_px / kW, x = pos_px % kW;
    for (int i = 0; i < n;) {
        const int run = kW - x < n - i ? kW - x : n - i;
        const int by = y - kBandY;
        if (by < 0 || by >= kBandH) {
            for (int k = 0; k < run; ++k) out[i + k] = kNavy;
        } else {
            const uint8_t* row = s_bits + by * (kW / 8);
            for (int k = 0; k < run; ++k) {
                const int xx = x + k;
                out[i + k] = (row[xx >> 3] >> (7 - (xx & 7)) & 1) ? kCream : kNavy;
            }
        }
        i += run;
        x = 0;
        ++y;
        if (y >= kH) y = 0;
    }
    return false;
}

}  // namespace

bool show(const char* title, const char* const* lines, int count)
{
    s_bits = static_cast<uint8_t*>(heap_caps_calloc(1, kW / 8 * kBandH, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!s_bits) return false;
    {
        // The text, drawn once (LovyanGFX without a panel), then copied to
        // the bit picture pixel by pixel - no assumption about its layout
        lgfx::LGFX_Sprite sp;
        sp.setPsram(false);
        sp.setColorDepth(1);
        if (sp.createSprite(kW, kBandH)) {
            sp.fillScreen(0);
            sp.setTextColor(1);
            sp.setTextDatum(textdatum_t::top_center);
            sp.setFont(&fonts::FreeSansBold18pt7b);
            int y = 6;
            sp.drawString(title, kW / 2, y);
            y += sp.fontHeight() + 14;
            sp.setFont(&fonts::DejaVu24);
            for (int i = 0; i < count; ++i) {
                sp.drawString(lines[i], kW / 2, y);
                y += sp.fontHeight() + 6;
            }
            for (int by = 0; by < kBandH; ++by)
                for (int bx = 0; bx < kW; ++bx)
                    if (sp.readPixel(bx, by)) s_bits[by * (kW / 8) + (bx >> 3)] |= uint8_t(0x80 >> (bx & 7));
            sp.deleteSprite();
        }
    }

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = 10 * 1000 * 1000;   // a little slower than games: the interrupt fills the lines
    cfg.timings.h_res = kW;
    cfg.timings.v_res = kH;
    cfg.timings.hsync_pulse_width = 4;
    cfg.timings.hsync_back_porch = 8;
    cfg.timings.hsync_front_porch = 4;
    cfg.timings.vsync_pulse_width = 4;
    cfg.timings.vsync_back_porch = 8;
    cfg.timings.vsync_front_porch = 4;
    cfg.timings.flags.hsync_idle_low = 1;   // as the main panel setup
    cfg.timings.flags.vsync_idle_low = 1;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.bounce_buffer_size_px = kW * kBounceLines;
    cfg.hsync_gpio_num = 39;
    cfg.vsync_gpio_num = 41;
    cfg.de_gpio_num = 40;
    cfg.pclk_gpio_num = 42;
    cfg.disp_gpio_num = -1;
    // B0-B4, G0-G5, R0-R4 (the board file's pins)
    static const int kData[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};
    for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = kData[i];
    cfg.flags.no_fb = 1;
    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err == ESP_OK) {
        esp_lcd_rgb_panel_event_callbacks_t cb = {};
        cb.on_bounce_empty = fill;
        err = esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cb, nullptr);
    }
    if (err == ESP_OK) err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "RGB panel without a frame buffer didn't start: %s", esp_err_to_name(err));
        return false;
    }
    // Backlight on (GPIO 2, high = on)
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);
    return true;
}

}  // namespace rgb_fallback

#else

namespace rgb_fallback {
bool show(const char*, const char* const*, int) { return false; }
}  // namespace rgb_fallback

#endif
