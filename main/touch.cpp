// Touch screen (see touch.h).
#include "touch.h"

#include "board_select.h"
#include "screen.h"
#include "ui.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include <cstdlib>

namespace touch {
namespace {

constexpr const char* kTag = "touch";
constexpr const char* kNs = "arcade";
constexpr int kAgree = 8;       // px between the two readings that start a press

bool s_down = false;
int s_x = 0, s_y = 0;           // where the press started
int s_cand_x = -1, s_cand_y = -1;
int s_empty = 0;
uint32_t s_since = 0;
int s_calls = 0;

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

// Calibration from another board build is ignored
uint32_t board_hash()
{
    uint32_t h = 2166136261u;
    for (const char* p = BOARD_NAME; *p; ++p) {
        h ^= uint8_t(*p);
        h *= 16777619u;
    }
    return h;
}

bool load(uint16_t params[8])
{
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
    uint32_t board = 0;
    size_t len = 8 * sizeof(uint16_t);
    const bool ok = nvs_get_u32(h, "tcal_b", &board) == ESP_OK && board == board_hash() &&
                    nvs_get_blob(h, "tcal", params, &len) == ESP_OK && len == 8 * sizeof(uint16_t);
    nvs_close(h);
    return ok;
}

void save(const uint16_t params[8])
{
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "tcal", params, 8 * sizeof(uint16_t));
    nvs_set_u32(h, "tcal_b", board_hash());
    nvs_commit(h);
    nvs_close(h);
}

bool boot_button_held()
{
    gpio_reset_pin(gpio_num_t(BOARD_PIN_BOOT_BTN));
    gpio_set_direction(gpio_num_t(BOARD_PIN_BOOT_BTN), GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num_t(BOARD_PIN_BOOT_BTN), GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(5));
    return gpio_get_level(gpio_num_t(BOARD_PIN_BOOT_BTN)) == 0;
}

bool raw(int& x, int& y)
{
    lgfx::touch_point_t tp;
    if (screen::lcd().getTouch(&tp, 1) == 0) return false;
    x = tp.x;
    y = tp.y;
    return true;
}

}  // namespace

bool needs_calibration() { return BOARD_TOUCH_RESISTIVE; }

void calibrate()
{
    LGFX& g = screen::lcd();
    g.fillScreen(ui::kBg);
    ui::message(g, "Touch Setup",
                "Tap the tip of each arrow as it appears in a corner. A stylus works best.");
    vTaskDelay(pdMS_TO_TICKS(2500));
    g.fillScreen(ui::kBg);
    uint16_t params[8];
    g.calibrateTouch(params, ui::kGold, ui::kBg, g.width() >= 320 ? 28 : 20);
    save(params);
    g.fillScreen(ui::kBg);
    ESP_LOGI(kTag, "calibration saved");
    wait_release();
}

void init()
{
#if BOARD_TOUCH_RESISTIVE
    uint16_t params[8];
    if (!boot_button_held() && load(params)) {
        screen::lcd().setTouchCalibrate(params);
        ESP_LOGI(kTag, "saved calibration loaded");
        return;
    }
    calibrate();
#endif
}

Event poll()
{
    Event e;
    int x, y;
    if (raw(x, y)) {
        s_empty = 0;
        if (s_down) return e;
        const bool agree = !BOARD_TOUCH_RESISTIVE ||
                           (s_cand_x >= 0 && std::abs(x - s_cand_x) <= kAgree && std::abs(y - s_cand_y) <= kAgree);
        s_cand_x = x;
        s_cand_y = y;
        if (!agree) return e;
        s_down = true;
        s_x = x;
        s_y = y;
        s_since = now_ms();
        e.kind = Event::Down;
        e.x = x;
        e.y = y;
        return e;
    }
    s_cand_x = -1;
    if (s_down && ++s_empty >= 2) {
        s_down = false;
        e.kind = Event::Up;
        e.x = s_x;
        e.y = s_y;
    }
    return e;
}

bool held_long()
{
    if (++s_calls & 3) return false;
    const Event e = poll();
    (void)e;
    return s_down && now_ms() - s_since >= 1000;
}

void wait_release()
{
    for (int quiet = 0; quiet < 3;) {
        int x, y;
        quiet = raw(x, y) ? 0 : quiet + 1;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_down = false;
    s_cand_x = -1;
    s_empty = 0;
}

}  // namespace touch
