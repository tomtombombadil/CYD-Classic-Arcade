// Controller Test (see pad_test.h).
#include "pad_test.h"

#include "input.h"
#include "screen.h"
#include "touch.h"
#include "ui.h"

#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace pad_test {
namespace {

using namespace screen;

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

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

int lh() { return lcd().fontHeight() + 4; }

void text(int y, uint16_t color, const char* s)
{
    lcd().fillRect(0, y, lcd().width(), lh(), kBg);
    lcd().setTextColor(color, kBg);
    lcd().drawString(s, 6, y + 2);
}

// Sends a 224x288 frame in 16-line strips, as a game does, and
// returns the time per frame in 1/10 ms.
int measure_frame_push()
{
    constexpr int kW = 224, kH = 288, kStrip = 16, kFrames = 10;
    static uint16_t strips[2][kW * kStrip];
    LGFX& l = lcd();
    const int x0 = (l.width() - kW) / 2;
    const int y0 = l.height() - kH;
    l.startWrite();
    const int64_t t0 = esp_timer_get_time();
    for (int f = 0; f < kFrames; f++) {
        for (int y = 0; y < kH; y += kStrip) {
            uint16_t* s = strips[(y / kStrip) & 1];
            for (int i = 0; i < kW * kStrip; i++) s[i] = uint16_t((i + y + f * 8) * 37);
            l.pushImage(x0, y0 + y, kW, kStrip, reinterpret_cast<const lgfx::swap565_t*>(s));
        }
    }
    const int64_t t1 = esp_timer_get_time();
    l.endWrite();
    l.fillRect(x0, y0, kW, kH, kBg);
    return int((t1 - t0) / (kFrames * 100));
}

void draw_pad(int idx, int y, const pad_state_t& p, const pad_state_t& was, bool force)
{
    char buf[64];
    const int h = lh();
    if (force || p.connected != was.connected || std::strcmp(p.name, was.name) != 0) {
        if (p.connected)
            std::snprintf(buf, sizeof buf, "%d: %s", idx + 1, p.name);
        else
            std::snprintf(buf, sizeof buf, "%d: put a controller in pairing mode", idx + 1);
        text(y, p.connected ? kGold : kMuted, buf);
    }
    const int cols = 7;
    const int kw = (lcd().width() - 12) / cols;
    const int kh = h + 4;
    for (int k = 0; k < kKeyCount; k++) {
        const bool on = p.buttons & kKeys[k].bit;
        const bool was_on = was.buttons & kKeys[k].bit;
        if (!force && on == was_on) continue;
        const int kx = 6 + (k % cols) * kw;
        const int ky = y + h + (k / cols) * (kh + 2);
        lcd().fillRoundRect(kx, ky, kw - 3, kh, 3, on ? kGold : kKey);
        lcd().setTextColor(on ? kBg : kText, on ? kGold : kKey);
        lcd().drawCenterString(kKeys[k].label, kx + (kw - 3) / 2, ky + 4);
    }
    if (force || p.lx != was.lx || p.ly != was.ly || p.reports != was.reports) {
        std::snprintf(buf, sizeof buf, "stick %4d %4d   reports %lu", p.lx, p.ly, (unsigned long)p.reports);
        text(y + h + 2 * (kh + 2), kMuted, buf);
    }
}

void draw_memory(int y)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "RAM %uK free, %uK block, %uK low",
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                  unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    text(y, kText, buf);
}

}  // namespace

void run(AudioRing& ring)
{
    LGFX& l = lcd();
    l.fillScreen(kBg);
    const int ms10 = measure_frame_push();
    ui::draw_header(l, "Controller Test", true, false);
    const int top = ui::metrics(l).header_h + 4;
    l.setFont(l.width() >= 320 ? static_cast<const lgfx::IFont*>(&fonts::Font2)
                               : static_cast<const lgfx::IFont*>(&fonts::Font0));
    l.setTextDatum(textdatum_t::top_left);
    const int h = lh();
    char buf[64];
    text(top, kMuted, BOARD_NAME);
    std::snprintf(buf, sizeof buf, "224x288 frame: %d.%d ms (%d fps max)", ms10 / 10, ms10 % 10,
                  ms10 ? 10000 / ms10 : 0);
    text(top + h * 2, kText, buf);
    const int band = h * 2 + 2 * (h + 6) + 6;
    const int pad_y = top + h * 4;
    text(pad_y + band * PAD_MAX, kMuted, "Hold A for a tone, B for a higher one");
    text(pad_y + band * PAD_MAX + h, kMuted, "Back: tap <, or Select + Start twice");

    pad_state_t was[PAD_MAX] = {};
    bool first = true;
    uint32_t next_mem = 0, phase = 0;
    Input in;
    int16_t tone[480];
    in.wait_for_release();      // not the press that opened this
    for (;;) {
        const uint32_t now = now_ms();
        in.update(now);
        if (in.back()) break;
        const touch::Event te = touch::poll();
        if (te.kind == touch::Event::Up && ui::hit_text(l, te.x, te.y).hit == ui::Hit::Back) break;
        int hz = 0;
        for (int i = 0; i < PAD_MAX; i++) {
            pad_state_t p;
            pad_get(i, &p);
            draw_pad(i, pad_y + band * i, p, was[i], first);
            if (p.buttons & PAD_A) hz = 440;
            if (p.buttons & PAD_B) hz = 660;
            was[i] = p;
        }
        first = false;
        // 20 ms of square wave (or silence) a loop, kept a little ahead
        const uint32_t step = uint32_t(uint64_t(hz) * 0x100000000ULL / 24000);
        for (int i = 0; i < 480; i++) {
            phase += step;
            tone[i] = hz ? ((phase & 0x80000000u) ? 12000 : -12000) : 0;
        }
        while (ring.count() > 960) vTaskDelay(1);
        ring.push(tone, 480);
        if (int32_t(now - next_mem) >= 0) {
            draw_memory(top + h);
            next_mem = now + 1000;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ring.clear();
    l.fillScreen(kBg);
    menu_font();
}

}  // namespace pad_test
