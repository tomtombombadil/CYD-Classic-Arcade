// The game loop (see game_runner.h).
//
// Pacing: each frame's audio goes into the ring the audio task drains at
// 24 kHz; the loop waits while the ring is full enough, so the game runs at
// its own speed, locked to the sound. A frame is drawn only when the ring
// holds enough sound to last through the drawing (the time a draw takes is
// measured as it goes); otherwise drawing is skipped and the next frame is
// emulated. v0.4.0 drew whenever one frame of sound was queued: a scrolling
// Rally-X picture scaled up on the 4.0" takes ~50 ms to send, so each draw
// emptied the ring, one frame was emulated, and it drew again - the game
// crawled, and surged when few lines changed (Tom).
//
// Drawing: each line is rendered, hashed and only sent to the panel if it
// differs from the line already there; changed lines go out in strips.
// On the bigger panels (320x480) the picture is scaled up to fill the
// width, keeping its shape: each screen pixel takes the nearest game pixel
// (no blending), so 224 game pixels become 320 - some two wide, some one.
// Plain blocking writes, no DMA: on the ESP32-32E boards the display's DMA
// stopped working once the SD card's bus had been used (v0.2.0: no picture
// in games, then none in the menu either), and Tom's other CYD projects
// (CYD-BYOG-PoolRadSeries) draw this way on every board.
#include "game_runner.h"

#include "catalog.h"
#include "input.h"
#include "screen.h"

#include "emu/games.h"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace game_runner {
namespace {

constexpr const char* kTag = "game";
constexpr int kStrip = 16;              // lines per strip sent
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
    LGFX& lcd = screen::lcd();
    if (g.orientation == games::Orientation::Landscape) screen::landscape();
    // Scale: as large as fits with the shape kept, never smaller than 1:1
    const int lcd_w = int(lcd.width()), lcd_h = int(lcd.height());   // int32_t is long on the ESP32
    const float scale = std::max(1.0f, std::min(float(lcd_w) / g.width, float(lcd_h) / g.height));
    const int out_w = std::min(lcd_w, int(g.width * scale + 0.5f));
    const int out_h = std::min(lcd_h, int(g.height * scale + 0.5f));
    const size_t strip_bytes = size_t(out_w) * kStrip * sizeof(uint16_t);
    uint16_t* strip = static_cast<uint16_t*>(heap_caps_malloc(strip_bytes, MALLOC_CAP_8BIT));
    uint16_t* src = static_cast<uint16_t*>(heap_caps_malloc(size_t(g.width) * sizeof(uint16_t), MALLOC_CAP_8BIT));
    uint16_t* x_map = static_cast<uint16_t*>(heap_caps_malloc(size_t(out_w) * sizeof(uint16_t), MALLOC_CAP_8BIT));
    uint32_t* line_hash = static_cast<uint32_t*>(heap_caps_malloc(kMaxLines * sizeof(uint32_t), MALLOC_CAP_8BIT));
    int16_t* audio = static_cast<int16_t*>(heap_caps_malloc(kAudioCap * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (!m || !strip || !src || !x_map || !line_hash || !audio || g.height > kMaxLines) {
        ESP_LOGE(kTag, "%s: out of memory (largest block %u)", g.id,
                 unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        delete m;
        heap_caps_free(strip);
        heap_caps_free(src);
        heap_caps_free(x_map);
        heap_caps_free(line_hash);
        heap_caps_free(audio);
        catalog::unload(roms);
        screen::apply_rotation();
        message("Not enough memory", g.title);
        return;
    }
    m->init(roms.regions, 24000, true);
    ESP_LOGI(kTag, "%s started, %u KB free, largest block %u KB", g.id,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));

    lcd.fillScreen(TFT_BLACK);
    const int x0 = (lcd_w - out_w) / 2;
    const int y0 = (lcd_h - out_h) / 2;
    for (int x = 0; x < out_w; ++x) x_map[x] = uint16_t(x * g.width / out_w);
    const bool scaled = out_w != g.width || out_h != g.height;
    ESP_LOGI(kTag, "%s: %dx%d shown as %dx%d", g.id, g.width, g.height, out_w, out_h);
    for (int y = 0; y < kMaxLines; ++y) line_hash[y] = 0;   // a black line hashes otherwise
    ring.clear();

    // Sound queued: at least three frames, and always enough to last
    // through a draw (draw_est, in samples, follows the slowest recent draw).
    const uint32_t frame_samples = uint32_t(24000.0 / games::frame_rate(g) + 0.5);
    const uint32_t queue_max = AudioRing::kSize - kAudioCap;
    uint32_t draw_est = frame_samples;

    Input in;
    games::FourWay four_way;
    in.wait_for_release();      // not the press that opened this
    uint32_t stat_t = now_ms(), frames = 0, drawn = 0, lines_sent = 0;
    int64_t emu_us = 0, draw_us = 0;
    for (;;) {
        in.update(now_ms());
        if (in.back()) break;
        const uint32_t pad = g.four_way ? four_way.filter(in.game()) : in.game();
        m->set_inputs(games::map_pad(g, pad));

        const uint32_t queue_target = std::min(queue_max, std::max(frame_samples * 3, draw_est + frame_samples * 2));
        while (ring.count() > queue_target) vTaskDelay(1);
        const int64_t t0 = esp_timer_get_time();
        const int n = m->run_frame(audio, kAudioCap);
        ring.push(audio, uint32_t(n));
        const int64_t t1 = esp_timer_get_time();
        emu_us += t1 - t0;
        ++frames;

        // Not enough sound queued to last through a draw: skip this one.
        if (ring.count() < draw_est + frame_samples / 2) continue;

        lcd.startWrite();
        int rows = 0, start = 0;
        auto flush = [&]() {
            if (!rows) return;
            lcd.pushImage(x0, y0 + start, out_w, rows, reinterpret_cast<const lgfx::swap565_t*>(strip));
            lines_sent += uint32_t(rows);
            rows = 0;
        };
        // Output row r shows game line r * height / out_h; a game line is
        // rendered and hashed once, and its rows are sent only if it changed.
        int cur = -1;
        bool changed = false;
        for (int r = 0; r < out_h; ++r) {
            const int y = r * g.height / out_h;
            if (y != cur) {
                cur = y;
                uint16_t* line = scaled ? src : strip + rows * out_w;
                m->render_line(y, line);
                const uint32_t h = hash_line(line, g.width);
                changed = h != line_hash[y];
                line_hash[y] = h;
            }
            if (!changed) {
                flush();
                continue;
            }
            uint16_t* row = strip + rows * out_w;
            if (scaled) {
                for (int x = 0; x < out_w; ++x) row[x] = src[x_map[x]];
            }
            if (!rows) start = r;
            ++rows;
            if (rows == kStrip) flush();
        }
        flush();
        lcd.endWrite();
        const int64_t took_us = esp_timer_get_time() - t1;
        draw_us += took_us;
        ++drawn;
        // Up at once to a slower draw, down slowly (a quiet screen draws
        // fast, the next scroll doesn't)
        const uint32_t took = uint32_t(took_us * 24 / 1000);
        draw_est = took > draw_est ? took : draw_est - (draw_est - took) / 8;
        draw_est = std::min(draw_est, queue_max - frame_samples * 2);

        const uint32_t now = now_ms();
        if (now - stat_t >= 5000) {
            ESP_LOGI(kTag, "%s: %lu frames, %lu drawn in 5 s; emu %.2f ms, draw %.2f ms a frame; %lu lines sent; queue %lu",
                     g.id, (unsigned long)frames, (unsigned long)drawn, frames ? emu_us / 1000.0 / frames : 0.0,
                     drawn ? draw_us / 1000.0 / drawn : 0.0, (unsigned long)lines_sent,
                     (unsigned long)(std::max(frame_samples * 3, draw_est + frame_samples * 2)));
            stat_t = now;
            frames = drawn = lines_sent = 0;
            emu_us = draw_us = 0;
        }
    }

    ring.clear();
    delete m;
    heap_caps_free(strip);
    heap_caps_free(src);
    heap_caps_free(x_map);
    heap_caps_free(line_hash);
    heap_caps_free(audio);
    catalog::unload(roms);
    screen::apply_rotation();
    lcd.fillScreen(screen::kBg);
}

}  // namespace game_runner
