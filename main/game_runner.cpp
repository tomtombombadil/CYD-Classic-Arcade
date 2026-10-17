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
// Drawing: each line is rendered and hashed in segments (line_diff.h); only
// the segments that differ from what the panel shows are sent, and lines
// whose changed part is the same columns go out together in strips.
// On the bigger panels (320x480) the picture is scaled up to fill the
// width, keeping its shape: each screen pixel takes the nearest game pixel
// (no blending), so 224 game pixels become 320 - some two wide, some one.
// Plain blocking writes, no DMA: on the ESP32-32E boards the display's DMA
// stopped working once the SD card's bus had been used (v0.2.0: no picture
// in games, then none in the menu either), and Tom's other CYD projects
// (CYD-BYOG-PoolRadSeries) draw this way on every board.
#include "game_runner.h"

#include "catalog.h"
#include "crash_note.h"
#include "input.h"
#include "line_diff.h"
#include "screen.h"
#include "touch.h"
#include "ui.h"

#include "emu/games.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace game_runner {
namespace {

constexpr const char* kTag = "game";
constexpr int kStrip = 8;               // lines per strip sent (7.5 KB at 480 wide)
constexpr int kMaxLines = 512;
constexpr int kAudioCap = 1024;         // samples per frame, at most
// Internal RAM left free when a game's ROMs are placed: the runner's buffers
// (~13 KB) and Bluetooth's own needs while playing.
constexpr uint32_t kKeepFree = 40 * 1024;

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

Stats s_last;

void message(const char* a, const char* b)
{
    LGFX& lcd = screen::lcd();
    lcd.fillScreen(ui::kBg);
    ui::message(lcd, a, b, ui::kRed);
    const uint32_t t0 = now_ms();
    while (now_ms() - t0 < 4000 && touch::poll().kind != touch::Event::Up) vTaskDelay(pdMS_TO_TICKS(16));
}

}  // namespace

void run(int index, AudioRing& ring)
{
    const games::Info& g = games::kGames[index];
    // The machine first (its object is the biggest block), then the ROMs:
    // into RAM while kKeepFree stays free for the runner's buffers and
    // Bluetooth, the rest streamed into flash and read from there. v0.14.0
    // loaded every ROM into RAM first: the big games left Bluetooth no RAM
    // and the board reset (Tom: Dig Dug, Mr. Do!, Q*bert, Robotron,
    // Defender, Qix, Star Wars).
    ESP_LOGI(kTag, "%s: %u KB free, largest block %u KB", g.id,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    crash_note::step(g.title, "making its machine");
    games::Machine* m = g.make();
    if (!m) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%u KB free, largest %u KB",
                      unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
        ESP_LOGE(kTag, "%s: no memory for the machine (%s)", g.id, buf);
        crash_note::clear();
        message("Not enough memory", buf);
        return;
    }
    crash_note::step(g.title, "loading its ROMs");
    catalog::Loaded roms;
    if (!catalog::load(g, roms, kKeepFree)) {
        delete m;
        crash_note::clear();
        message("Can't load the ROMs", catalog::last_error());
        return;
    }
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
    const line_diff::Layout segs = line_diff::layout(g.width);
    uint32_t* seg_hash = static_cast<uint32_t*>(
        heap_caps_malloc(size_t(g.height) * size_t(segs.segs) * sizeof(uint32_t), MALLOC_CAP_8BIT));
    int16_t* audio = static_cast<int16_t*>(heap_caps_malloc(kAudioCap * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (!m || !strip || !src || !x_map || !seg_hash || !audio || g.height > kMaxLines) {
        ESP_LOGE(kTag, "%s: out of memory (largest block %u)", g.id,
                 unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
        delete m;
        heap_caps_free(strip);
        heap_caps_free(src);
        heap_caps_free(x_map);
        heap_caps_free(seg_hash);
        heap_caps_free(audio);
        catalog::unload(roms);
        crash_note::clear();
        screen::apply_rotation();
        message("Not enough memory", g.title);
        return;
    }
    crash_note::step(g.title, "starting its machine");
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
    line_diff::Drawer drawer;
    drawer.segs = segs;
    drawer.height = g.height;
    drawer.out_w = out_w;
    drawer.out_h = out_h;
    drawer.scaled = scaled;
    drawer.x_map = x_map;
    drawer.src = src;
    drawer.strip = strip;
    drawer.strip_rows = kStrip;
    drawer.seg_hash = seg_hash;
    drawer.setup();
    ring.clear();

    // The start-up tests (RAM / ROM checks, test patterns, noise) run unseen
    // and unheard, as fast as the board goes, behind a "Loading" card -
    // drawn in the game's orientation, so a landscape game's card is the
    // sign to turn the board (Tom). The picture starts with the attract
    // mode. Back (or holding the screen) cancels.
    bool cancelled = false;
    if (g.boot_frames > 0) {
        ui::loading(lcd, g.title, "", 0, g.boot_frames, true);
        crash_note::step(g.title, "running its start-up");
        Input boot_in;
        boot_in.wait_for_release();
        const int64_t t0 = esp_timer_get_time();
        for (int f = 0; f < g.boot_frames; ++f) {
            m->run_frame(audio, kAudioCap);     // the sound is thrown away
            // Every 32 frames: the bar, the back gesture, and a tick for the
            // idle task (watchdog) - little time away from the emulation
            if ((f & 31) == 31) {
                ui::loading(lcd, g.title, "", f + 1, g.boot_frames, false);
                vTaskDelay(1);
                boot_in.update(now_ms());
                if (boot_in.back() || touch::held_long()) {
                    cancelled = true;
                    break;
                }
            }
        }
        ESP_LOGI(kTag, "%s: %d start-up frames in %.1f s", g.id, g.boot_frames,
                 double(esp_timer_get_time() - t0) / 1e6);
        lcd.fillScreen(TFT_BLACK);
    }

    // Sound queued: at least three frames, and always enough to last
    // through a draw (draw_est, in samples, follows the slowest recent draw).
    const uint32_t frame_samples = uint32_t(24000.0 / games::frame_rate(g) + 0.5);
    const uint32_t queue_max = AudioRing::kSize - kAudioCap;
    uint32_t draw_est = frame_samples;

    Input in;
    games::FourWay four_way;
    in.wait_for_release();      // not the press that opened this
    uint32_t stat_t = now_ms(), frames = 0, drawn = 0, lines_sent = 0;
    const int64_t run_t0 = esp_timer_get_time();
    uint32_t all_frames = 0, all_drawn = 0;
    int64_t all_emu_us = 0, all_draw_us = 0;
    int64_t emu_us = 0, draw_us = 0;
    // For the About page: worst frame / picture (after the first 2 s), the
    // time spent sending, pixels sent, and the slowest whole second
    int64_t emu_max_us = 0, draw_max_us = 0, all_send_us = 0;
    uint64_t all_pixels = 0;
    uint32_t sec_t = now_ms(), sec_frames = 0, sec_drawn = 0;
    int worst_frames = -1, worst_drawn = 0;
    crash_note::step(g.title, "playing");
    while (!cancelled) {
        in.update(now_ms());
        // Back to the menu: the controller's gesture, or the screen held
        // for a second (a game started without a controller)
        if (in.back() || touch::held_long()) break;
        const uint32_t pad = g.four_way ? four_way.filter(in.game()) : in.game();
        m->set_inputs(games::map_pad(g, pad));
        m->set_analog(in.analog());

        const uint32_t queue_target = std::min(queue_max, std::max(frame_samples * 3, draw_est + frame_samples * 2));
        while (ring.count() > queue_target) vTaskDelay(1);
        const int64_t t0 = esp_timer_get_time();
        const int n = m->run_frame(audio, kAudioCap);
        ring.push(audio, uint32_t(n));
        const int64_t t1 = esp_timer_get_time();
        emu_us += t1 - t0;
        ++frames;
        ++sec_frames;
        const bool warm = t1 - run_t0 > 2000000;
        if (warm) emu_max_us = std::max(emu_max_us, t1 - t0);
        {
            const uint32_t now = now_ms();
            if (now - sec_t >= 1000) {
                if (warm && (worst_frames < 0 || int(sec_frames) < worst_frames)) {
                    worst_frames = int(sec_frames);
                    worst_drawn = int(sec_drawn);
                }
                sec_t = now;
                sec_frames = sec_drawn = 0;
            }
        }

        // Not enough sound queued to last through a draw: skip this one.
        if (ring.count() < draw_est + frame_samples / 2) continue;

        // Only the changed segments of each line go (line_diff::Drawer).
        // The sound must not run dry while drawing: when less than a frame
        // of it is left, the picture stops at a line boundary and the next
        // one goes on from there (v0.6.1: Galaga's start tune crackled at
        // its end, where the whole screen changes at once and one draw
        // outlasted the queue).
        lcd.startWrite();
        const bool complete = drawer.frame(
            [&](int y, uint16_t* line) { m->render_line(y, line); },
            [&](int x, int y, int w, int h, const uint16_t* px) {
                const int64_t s0 = esp_timer_get_time();
                lcd.pushImage(x0 + x, y0 + y, w, h, reinterpret_cast<const lgfx::swap565_t*>(px));
                all_send_us += esp_timer_get_time() - s0;
                all_pixels += uint32_t(w * h);
                lines_sent += uint32_t(h);
            },
            [&]() { return ring.count() < frame_samples; });
        lcd.endWrite();
        const int64_t took_us = esp_timer_get_time() - t1;
        draw_us += took_us;
        ++drawn;
        ++sec_drawn;
        if (warm) draw_max_us = std::max(draw_max_us, took_us);
        // Up at once to a slower draw, down slowly (a quiet screen draws
        // fast, the next scroll doesn't)
        const uint32_t took = uint32_t(took_us * 24 / 1000);
        if (took > draw_est) draw_est = took;
        else if (complete) draw_est -= (draw_est - took) / 8;
        draw_est = std::min(draw_est, queue_max - frame_samples * 2);

        const uint32_t now = now_ms();
        if (now - stat_t >= 5000) {
            all_frames += frames;
            all_drawn += drawn;
            all_emu_us += emu_us;
            all_draw_us += draw_us;
            ESP_LOGI(kTag, "%s: %lu frames, %lu drawn in 5 s; emu %.2f ms, draw %.2f ms a frame; %lu lines sent; queue %lu",
                     g.id, (unsigned long)frames, (unsigned long)drawn, frames ? emu_us / 1000.0 / frames : 0.0,
                     drawn ? draw_us / 1000.0 / drawn : 0.0, (unsigned long)lines_sent,
                     (unsigned long)(std::max(frame_samples * 3, draw_est + frame_samples * 2)));
            stat_t = now;
            frames = drawn = lines_sent = 0;
            emu_us = draw_us = 0;
        }
    }

    all_frames += frames;
    all_drawn += drawn;
    all_emu_us += emu_us;
    all_draw_us += draw_us;
    const float secs = float(esp_timer_get_time() - run_t0) / 1e6f;
    s_last = Stats{};
    if (secs > 0.5f) {
        s_last.frames_per_s = float(all_frames) / secs;
        s_last.drawn_per_s = float(all_drawn) / secs;
    }
    if (all_frames) s_last.emu_ms = float(all_emu_us) / 1000.0f / float(all_frames);
    if (all_drawn) {
        s_last.draw_ms = float(all_draw_us) / 1000.0f / float(all_drawn);
        s_last.send_ms = float(all_send_us) / 1000.0f / float(all_drawn);
        s_last.kpixels = float(all_pixels) / 1000.0f / float(all_drawn);
    }
    s_last.game_hz = float(games::frame_rate(g));
    s_last.emu_max_ms = float(emu_max_us) / 1000.0f;
    s_last.draw_max_ms = float(draw_max_us) / 1000.0f;
    s_last.worst_frames = std::max(worst_frames, 0);
    s_last.worst_drawn = worst_drawn;
    ESP_LOGI(kTag, "%s ran %.1f s: %.1f frames/s, %.1f drawn/s; emu %.2f ms (max %.2f), draw %.2f ms (max %.2f, "
             "send %.2f, %.1fK pixels); slowest second %d frames, %d drawn", g.id, double(secs),
             double(s_last.frames_per_s), double(s_last.drawn_per_s), double(s_last.emu_ms), double(s_last.emu_max_ms),
             double(s_last.draw_ms), double(s_last.draw_max_ms), double(s_last.send_ms), double(s_last.kpixels),
             s_last.worst_frames, s_last.worst_drawn);

    ring.clear();
    delete m;
    heap_caps_free(strip);
    heap_caps_free(src);
    heap_caps_free(x_map);
    heap_caps_free(seg_hash);
    heap_caps_free(audio);
    crash_note::clear();
    catalog::unload(roms);
    screen::apply_rotation();
    lcd.fillScreen(screen::kBg);
}

const Stats& last_stats() { return s_last; }

}  // namespace game_runner
