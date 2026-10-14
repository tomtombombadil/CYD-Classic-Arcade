// The game loop (see game_runner.h).
//
// Pacing: each frame's audio goes into the ring the audio task drains at
// 24 kHz; the loop waits while the ring holds more than a few frames, so
// the game runs at its own speed, locked to the sound. When the loop falls
// behind (the ring is nearly empty), it skips drawing that frame.
//
// Drawing: each line is rendered, hashed and only sent to the panel if it
// differs from the line already there; changed lines go out in strips by
// DMA while the next strip is rendered.
#include "game_runner.h"

#include "catalog.h"
#include "input.h"
#include "screen.h"

#include "emu/games.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace game_runner {
namespace {

constexpr const char* kTag = "game";
constexpr int kStrip = 16;              // lines per DMA strip
constexpr int kMaxLines = 512;
constexpr int kAudioCap = 1024;         // samples per frame, at most

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

uint32_t hash_line(const uint16_t* p, int n)
{
    uint32_t h = 2166136261u;
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
    for (int i = 0; i < n / 2; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
}

void message(const char* a, const char* b)
{
    LGFX& lcd = screen::lcd();
    lcd.fillScreen(screen::kBg);
    screen::menu_font();
    const int lh = screen::line_h();
    screen::line(lcd.height() / 2 - lh, screen::kRed, a);
    screen::line(lcd.height() / 2, screen::kText, b);
    vTaskDelay(pdMS_TO_TICKS(3000));
}

}  // namespace

void run(int index, AudioRing& ring)
{
    const games::Info& g = games::kGames[index];
    catalog::Loaded roms;
    if (!catalog::load(g, roms)) {
        message("Can't load the ROMs", "Reload them, see the web page");
        return;
    }
    games::Machine* m = g.make();
    const size_t strip_bytes = size_t(g.width) * kStrip * sizeof(uint16_t);
    uint16_t* strips[2] = {
        static_cast<uint16_t*>(heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA)),
        static_cast<uint16_t*>(heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA)),
    };
    uint32_t* line_hash = static_cast<uint32_t*>(heap_caps_malloc(kMaxLines * sizeof(uint32_t), MALLOC_CAP_8BIT));
    int16_t* audio = static_cast<int16_t*>(heap_caps_malloc(kAudioCap * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (!m || !strips[0] || !strips[1] || !line_hash || !audio || g.height > kMaxLines) {
        ESP_LOGE(kTag, "%s: out of memory (largest block %u)", g.id,
                 unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        delete m;
        heap_caps_free(strips[0]);
        heap_caps_free(strips[1]);
        heap_caps_free(line_hash);
        heap_caps_free(audio);
        catalog::unload(roms);
        message("Not enough memory", g.title);
        return;
    }
    m->init(roms.regions, 24000, true);
    ESP_LOGI(kTag, "%s started, %u KB free, largest block %u KB", g.id,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));

    LGFX& lcd = screen::lcd();
    lcd.fillScreen(TFT_BLACK);
    const int x0 = (lcd.width() - g.width) / 2;
    const int y0 = (lcd.height() - g.height) / 2;
    for (int y = 0; y < kMaxLines; ++y) line_hash[y] = 0;   // a black line hashes otherwise
    ring.clear();

    // About three frames of sound queued: enough to ride out a slow frame.
    const uint32_t frame_samples = uint32_t(24000.0 / games::frame_rate(g) + 0.5);
    const uint32_t queue_target = frame_samples * 3;

    Input in;
    uint32_t stat_t = now_ms(), frames = 0, drawn = 0, lines_sent = 0;
    int64_t emu_us = 0, draw_us = 0;
    for (;;) {
        in.update(now_ms());
        if (in.back()) break;
        m->set_inputs(games::map_pad(g, in.held()));

        while (ring.count() > queue_target) vTaskDelay(1);
        const int64_t t0 = esp_timer_get_time();
        const int n = m->run_frame(audio, kAudioCap);
        ring.push(audio, uint32_t(n));
        const int64_t t1 = esp_timer_get_time();
        emu_us += t1 - t0;
        ++frames;

        // Behind (less than a frame of sound left): skip drawing this one.
        if (ring.count() < frame_samples) continue;

        lcd.startWrite();
        int buf = 0, rows = 0, start = 0;
        uint16_t* strip = strips[0];
        auto flush = [&]() {
            if (!rows) return;
            lcd.waitDMA();
            lcd.pushImageDMA(x0, y0 + start, g.width, rows, reinterpret_cast<lgfx::swap565_t*>(strip));
            lines_sent += uint32_t(rows);
            buf ^= 1;
            strip = strips[buf];
            rows = 0;
        };
        for (int y = 0; y < g.height; ++y) {
            uint16_t* row = strip + rows * g.width;
            m->render_line(y, row);
            const uint32_t h = hash_line(row, g.width);
            if (h == line_hash[y]) {
                flush();
                continue;
            }
            line_hash[y] = h;
            if (!rows) start = y;
            ++rows;
            if (rows == kStrip) flush();
        }
        flush();
        lcd.waitDMA();
        lcd.endWrite();
        draw_us += esp_timer_get_time() - t1;
        ++drawn;

        const uint32_t now = now_ms();
        if (now - stat_t >= 5000) {
            ESP_LOGI(kTag, "%s: %lu frames, %lu drawn in 5 s; emu %.2f ms, draw %.2f ms a frame; %lu lines sent",
                     g.id, (unsigned long)frames, (unsigned long)drawn, frames ? emu_us / 1000.0 / frames : 0.0,
                     drawn ? draw_us / 1000.0 / drawn : 0.0, (unsigned long)lines_sent);
            stat_t = now;
            frames = drawn = lines_sent = 0;
            emu_us = draw_us = 0;
        }
    }

    ring.clear();
    delete m;
    heap_caps_free(strips[0]);
    heap_caps_free(strips[1]);
    heap_caps_free(line_hash);
    heap_caps_free(audio);
    catalog::unload(roms);
    lcd.fillScreen(screen::kBg);
}

}  // namespace game_runner
