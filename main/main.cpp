// CYD Classic Arcade - first milestone: the controller test.
// Shows each connected Bluetooth controller's buttons and stick, free
// memory with Bluetooth running, how fast a 224x288 arcade frame reaches
// the panel, and plays a tone through the speaker while A or B is held.
// Bluetooth runs on core 0 (app_main ends in BTstack's run loop); the
// screen and sound run on core 1, where the emulators will run.
#include <cstdio>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "audio_out.h"
#include "board_select.h"
#include "pad.h"
#include "version_gen.h"

namespace {

constexpr const char* kTag = "arcade";

LGFX lcd;

// Colors (RGB565): night-sky navy and gold, like CYD Classic Games
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kText = 0xFFDB;   // cream
constexpr uint16_t kMuted = 0x8410;  // grey
constexpr uint16_t kGold = 0xFE60;
constexpr uint16_t kKey = 0x10A8;    // navy key, not pressed

int s_frame_ms10 = 0;  // 224x288 frame push time, in 1/10 ms

// ---- Tone while A / B is held ---------------------------------------------

volatile int s_tone_hz = 0;

void tone_fill(int16_t* out, int n, void*) {
    static uint32_t phase = 0;
    const int hz = s_tone_hz;
    const uint32_t step = (uint32_t)((uint64_t)hz * 0x100000000ULL / audio::kRate);
    for (int i = 0; i < n; i++) {
        if (hz) {
            phase += step;
            out[i] = (phase & 0x80000000u) ? 12000 : -12000;
        } else {
            out[i] = 0;
        }
    }
}

// ---- Screen benchmark ------------------------------------------------------

// Sends a 224x288 frame (Pac-Man, Galaga) in 16-line strips by DMA, as the
// emulators will, and returns the time per frame in 1/10 ms.
int measure_frame_push() {
    constexpr int kW = 224, kH = 288, kStrip = 16;
    static uint16_t strips[2][kW * kStrip];
    const int x0 = (lcd.width() - kW) / 2;
    const int y0 = lcd.height() - kH;
    lcd.startWrite();
    const int64_t t0 = esp_timer_get_time();
    constexpr int kFrames = 10;
    for (int f = 0; f < kFrames; f++) {
        for (int y = 0; y < kH; y += kStrip) {
            uint16_t* s = strips[(y / kStrip) & 1];
            // a moving pattern so it is visibly drawing
            for (int i = 0; i < kW * kStrip; i++) s[i] = (uint16_t)((i + y + f * 8) * 37);
            lcd.pushImageDMA(x0, y0 + y, kW, kStrip, (lgfx::swap565_t*)s);
        }
    }
    lcd.waitDMA();
    const int64_t t1 = esp_timer_get_time();
    lcd.endWrite();
    lcd.fillRect(x0, y0, kW, kH, kBg);
    return (int)((t1 - t0) / (kFrames * 100));
}

// ---- Controller test screen ------------------------------------------------

struct Key {
    uint32_t bit;
    const char* label;
};

constexpr Key kKeys[] = {
    {PAD_UP, "Up"}, {PAD_DOWN, "Dn"}, {PAD_LEFT, "Lt"}, {PAD_RIGHT, "Rt"}, {PAD_A, "A"},
    {PAD_B, "B"}, {PAD_X, "X"}, {PAD_Y, "Y"}, {PAD_L, "L"}, {PAD_R, "R"},
    {PAD_START, "St"}, {PAD_SELECT, "Se"}, {PAD_HOME, "Hm"},
};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

int line_h() { return lcd.fontHeight() + 4; }

void draw_line(int y, uint16_t color, const char* text) {
    lcd.fillRect(0, y, lcd.width(), line_h(), kBg);
    lcd.setTextColor(color, kBg);
    lcd.drawString(text, 6, y + 2);
}

void draw_static() {
    lcd.fillScreen(kBg);
    char buf[64];
    snprintf(buf, sizeof(buf), "CYD Classic Arcade %s", CYD_ARCADE_VERSION);
    draw_line(0, kGold, buf);
    draw_line(line_h(), kText, "Controller Test");
    draw_line(line_h() * 2, kMuted, BOARD_NAME);
    snprintf(buf, sizeof(buf), "224x288 frame: %d.%d ms (%d fps max)", s_frame_ms10 / 10,
             s_frame_ms10 % 10, s_frame_ms10 ? 10000 / s_frame_ms10 : 0);
    draw_line(line_h() * 4, kText, buf);
}

// Draws controller `idx` in the band starting at y; only what changed.
void draw_pad(int idx, int y, const pad_state_t& p, const pad_state_t& was, bool force) {
    char buf[64];
    const int lh = line_h();
    if (force || p.connected != was.connected || strcmp(p.name, was.name) != 0) {
        if (p.connected)
            snprintf(buf, sizeof(buf), "%d: %s", idx + 1, p.name);
        else
            snprintf(buf, sizeof(buf), "%d: put a controller in pairing mode", idx + 1);
        draw_line(y, p.connected ? kGold : kMuted, buf);
    }
    // Buttons: a row of small keys, gold while pressed
    const int cols = 7;
    const int kw = (lcd.width() - 12) / cols;
    const int kh = lh + 4;
    for (int k = 0; k < kKeyCount; k++) {
        const bool on = p.buttons & kKeys[k].bit;
        const bool was_on = was.buttons & kKeys[k].bit;
        if (!force && on == was_on) continue;
        const int kx = 6 + (k % cols) * kw;
        const int ky = y + lh + (k / cols) * (kh + 2);
        lcd.fillRoundRect(kx, ky, kw - 3, kh, 3, on ? kGold : kKey);
        lcd.setTextColor(on ? kBg : kText, on ? kGold : kKey);
        lcd.drawCenterString(kKeys[k].label, kx + (kw - 3) / 2, ky + 4);
    }
    if (force || p.lx != was.lx || p.ly != was.ly || p.reports != was.reports) {
        snprintf(buf, sizeof(buf), "stick %4d %4d   reports %lu", p.lx, p.ly,
                 (unsigned long)p.reports);
        draw_line(y + lh + 2 * (kh + 2), kMuted, buf);
    }
}

void draw_memory(int y) {
    char buf[64];
    snprintf(buf, sizeof(buf), "RAM %u KB free, %u KB block, %u KB low",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    draw_line(y, kText, buf);
}

void ui_task(void*) {
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(200);
    if (lcd.width() >= 320)
        lcd.setFont(&fonts::Font2);
    else
        lcd.setFont(&fonts::Font0);
    lcd.fillScreen(kBg);
    s_frame_ms10 = measure_frame_push();
    ESP_LOGI(kTag, "frame push %d.%d ms", s_frame_ms10 / 10, s_frame_ms10 % 10);

    const bool sound = audio::start(tone_fill, nullptr);
    draw_static();
    const int lh = line_h();
    const int band = lh * 2 + 2 * (lh + 6) + 6;
    const int pad_y = lh * 6;
    draw_line(pad_y + band * PAD_MAX, kMuted,
              sound ? "Hold A for a tone, B for a higher one" : "Sound: the DAC did not start");

    pad_state_t was[PAD_MAX] = {};
    bool first = true;
    int64_t next_mem = 0;
    for (;;) {
        int tone = 0;
        for (int i = 0; i < PAD_MAX; i++) {
            pad_state_t p;
            pad_get(i, &p);
            draw_pad(i, pad_y + band * i, p, was[i], first);
            if (p.buttons & PAD_A) tone = 440;
            if (p.buttons & PAD_B) tone = 660;
            was[i] = p;
        }
        s_tone_hz = tone;
        first = false;
        const int64_t now = esp_timer_get_time();
        if (now >= next_mem) {
            draw_memory(lh * 3);
            next_mem = now + 1000000;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace

extern "C" void app_main(void) {
    // BTstack keeps controller pairings in NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_LOGI(kTag, "CYD Classic Arcade %s (%s) on %s", CYD_ARCADE_VERSION, CYD_ARCADE_BUILD,
             BOARD_NAME);
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, nullptr, 5, nullptr, 1);
    pad_bt_run();  // never returns
}
