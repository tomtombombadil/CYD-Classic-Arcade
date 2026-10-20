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
// Two cores (v0.14.6; Tom, v0.14.3: Galaga still bogged down in busy
// waves - 57.3 frames a second): machines that can copy their picture's
// state (Machine::snapshot - the tile games) are drawn by a task on core 0
// while core 1 emulates the next frame, so core 1 only emulates. Core 0
// otherwise runs Bluetooth, which needs little; the draw task has the same
// priority as BTstack's loop, so they share it. A snapshot is taken after a
// frame whenever the draw task is free (drawing slower than the game skips
// pictures, never frames). The others (vector, Williams, Qix, Mr. Do!,
// Burger Time, Invaders) are drawn between frames on core 1 as before.
// Touch shares the display's bus on the ESP32-32E boards: the screen and
// the touch read are kept apart by a mutex.
// Plain blocking writes, no DMA: on the ESP32-32E boards the display's DMA
// stopped working once the SD card's bus had been used (v0.2.0: no picture
// in games, then none in the menu either), and Tom's other CYD projects
// (CYD-BYOG-PoolRadSeries) draw this way on every board.
#include "game_runner.h"

#include <sdkconfig.h>

#include "arena.h"
#include "catalog.h"
#include "crash_note.h"
#include "devlog.h"
#include "input.h"
#include "line_diff.h"
#include "memlog.h"
#include "screen.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

#include "emu/games.h"
#include "emu/util/alloc.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

namespace game_runner {
namespace {

constexpr const char* kTag = "game";
constexpr int kStrip = 8;               // lines per strip sent (7.5 KB at 480 wide)
constexpr int kMaxLines = 512;
constexpr int kAudioCap = 1024;         // samples per frame, at most
// Internal RAM kept out of the game area (main/arena.h) for Bluetooth,
// the USB loader and the system. Before v0.15 a game simply took what it
// needed from the free RAM: Star Wars played on Tom's 3.2" (ESP32, ~77 KB
// free) with ~15 KB left, and Bluetooth was fine. v0.15.0-v0.15.4 kept
// 40 KB back, which left a 31 KB area on his 3.5" and only Pac-Man and
// Space Invaders fitted (Tom: "these games were playing fine a couple
// revisions ago"). 12 KB is kept back on the ESP32 now, as v0.14 left;
// the S3 has RAM to spare.
#if CONFIG_IDF_TARGET_ESP32
constexpr uint32_t kSystemReserve = 12 * 1024;
#else
constexpr uint32_t kSystemReserve = 40 * 1024;
#endif

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

Stats s_last;

// The draw task's work (async drawing, see the top of the file). The task
// is made once at start-up (its stack outside the game area) and waits
// for pictures; a game only fills in this job.
struct DrawJob {
    games::Machine* m = nullptr;
    line_diff::Drawer* drawer = nullptr;
    LGFX* lcd = nullptr;
    int x0 = 0, y0 = 0;
    std::atomic<bool> busy{false};     // a snapshot is being drawn
    std::atomic<uint32_t> drawn{0};
    // Read between pictures (and, roughly, for the 5 s log line)
    volatile int64_t draw_us = 0, draw_max_us = 0, send_us = 0;
    volatile uint64_t pixels = 0;
    volatile uint32_t lines_sent = 0;
    volatile bool warm = false;        // past the first 2 s: count the worst
};

DrawJob s_job;
TaskHandle_t s_draw = nullptr;
SemaphoreHandle_t s_bus = nullptr;    // the display's SPI bus (shared with touch on some boards)

void draw_task(void*)
{
    DrawJob& j = s_job;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!j.busy || !j.m) continue;
        const int64_t t0 = esp_timer_get_time();
        xSemaphoreTake(s_bus, portMAX_DELAY);
        j.lcd->startWrite();
        j.drawer->frame([&](int y, uint16_t* line) { j.m->render_snapshot_line(y, line); },
                        [&](int x, int y, int w, int h, const uint16_t* px) {
                            const int64_t s0 = esp_timer_get_time();
                            j.lcd->pushImage(j.x0 + x, j.y0 + y, w, h,
                                             reinterpret_cast<const lgfx::swap565_t*>(px));
                            j.send_us = j.send_us + (esp_timer_get_time() - s0);
                            j.pixels = j.pixels + uint64_t(w * h);
                            j.lines_sent = j.lines_sent + uint32_t(h);
                        },
                        []() { return false; });
        j.lcd->endWrite();
        xSemaphoreGive(s_bus);
        const int64_t took = esp_timer_get_time() - t0;
        j.draw_us = j.draw_us + took;
        if (j.warm && took > j.draw_max_us) j.draw_max_us = took;
        j.drawn.fetch_add(1);
        j.busy = false;
        // Core 0's idle task must run now and then (the task watchdog
        // checks it), even when pictures come back to back
        vTaskDelay(1);
    }
}

// A message that stays until a tap or a button (Tom: one that goes by
// can't be reported)
void message(const char* a, const char* b)
{
    LGFX& lcd = screen::lcd();
    lcd.fillScreen(ui::kBg);
    ui::message(lcd, a, b, ui::kRed);
    touch::wait_tap_or_button();
}

void emu_free(void* p) { arena::free(p); }

// "70 KB (48 + 14 + 8)": the area and its blocks, for the log and messages
const char* area_text()
{
    static char text[48];
    int n = std::snprintf(text, sizeof text, "%u KB", unsigned(arena::size() / 1024));
    for (int i = 0; i < arena::chunks() && n < int(sizeof text); ++i)
        n += std::snprintf(text + n, sizeof text - size_t(n), "%s%u%s", i ? " + " : " (",
                           unsigned(arena::chunk_size(i) / 1024), i + 1 == arena::chunks() ? ")" : "");
    return text;
}

}  // namespace

bool init()
{
    // Everything the emulators allocate comes from the game area
    emu::set_allocator(arena::alloc, emu_free);
    s_bus = xSemaphoreCreateMutex();
    if (s_bus) xTaskCreatePinnedToCore(draw_task, "draw", 5120, nullptr, 1, &s_draw, 0);
    const bool ok = arena::reserve(kSystemReserve);
    devlog::add("Game area %s%s; RAM left %u KB, block %u KB; draw task %s", area_text(), ok ? "" : " (NONE)", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                s_draw ? "on core 0" : "NONE");
    return ok;
}

void run(int index, AudioRing& ring)
{
    const games::Info& g = games::kGames[index];
    // Everything this game needs comes from the game area, which starts
    // empty: the machine, the runner's buffers, the snapshot for drawing on
    // core 0, then as many ROM regions as fit (the rest stream into flash).
    // When the game ends the area is emptied in one go, so the next game
    // starts from the same memory, whatever ran before (Tom, v0.14.6).
    const uint32_t before_kb = arena::renew(kSystemReserve) / 1024;
    const uint32_t area_kb = arena::size() / 1024;
    devlog::add("%s: start; game area %s, was %u KB; RAM outside %u KB free, block %u KB", g.id, area_text(),
                unsigned(before_kb),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
    memlog::blocks("outside the area");
    games::Machine* m = nullptr;
    catalog::Loaded roms;
    auto give_up = [&](const char* title, const char* text) {
        delete m;
        catalog::unload(roms);
        arena::reset();
        crash_note::clear();
        screen::apply_rotation();
        devlog::add("%s: %s - %s", g.id, title, text);
        message(title, text);
    };
    char why[112];

    crash_note::step(g.title, "making its machine");
    m = g.make();
    if (!m) {
        std::snprintf(why, sizeof why, "Its machine doesn't fit the %s game area; %u KB free outside it",
                      area_text(), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
        give_up("Not enough memory", why);
        return;
    }
    const uint32_t machine_kb = arena::used() / 1024;
    LGFX& lcd = screen::lcd();
    if (g.orientation == games::Orientation::Landscape) screen::landscape();
    // Scale: as large as fits with the shape kept, never smaller than 1:1
    const int lcd_w = int(lcd.width()), lcd_h = int(lcd.height());   // int32_t is long on the ESP32
    const float scale = std::max(1.0f, std::min(float(lcd_w) / g.width, float(lcd_h) / g.height));
    const int out_w = std::min(lcd_w, int(g.width * scale + 0.5f));
    const int out_h = std::min(lcd_h, int(g.height * scale + 0.5f));
    const size_t strip_bytes = size_t(out_w) * kStrip * sizeof(uint16_t);
    const line_diff::Layout segs = line_diff::layout(g.width);
    uint16_t* strip = static_cast<uint16_t*>(arena::alloc(strip_bytes));
    uint16_t* src = static_cast<uint16_t*>(arena::alloc(size_t(g.width) * sizeof(uint16_t)));
    uint16_t* x_map = static_cast<uint16_t*>(arena::alloc(size_t(out_w) * sizeof(uint16_t)));
    uint32_t* seg_hash =
        static_cast<uint32_t*>(arena::alloc(size_t(g.height) * size_t(segs.segs) * sizeof(uint32_t)));
    int16_t* audio = static_cast<int16_t*>(arena::alloc(kAudioCap * sizeof(int16_t)));
    if (!strip || !src || !x_map || !seg_hash || !audio || g.height > kMaxLines) {
        std::snprintf(why, sizeof why, "Machine %u KB + buffers don't fit the %s game area; %u KB free outside it",
                      unsigned(machine_kb), area_text(),
                      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
        give_up("Not enough memory", why);
        return;
    }
    // Drawing on core 0 while core 1 emulates, for machines that can copy
    // their picture (see the top of the file); else between frames here.
    // The first snapshot makes the copy's room, before the ROMs take theirs.
    const bool async = s_draw && m->snapshot();

    crash_note::step(g.title, "loading its ROMs");
    if (!catalog::load(g, roms)) {
        give_up("Can't load the ROMs", catalog::last_error());
        return;
    }
    crash_note::step(g.title, "starting its machine");
    m->init(roms.regions, 24000, true);
    m->set_demo_sounds(settings::get().demo_sounds);
    devlog::add("%s: machine %u KB, game area used %u of %u KB, ROM %u KB in RAM / %u KB from flash; drawn %s",
                g.id, unsigned(machine_kb), unsigned(arena::used() / 1024), unsigned(area_kb),
                unsigned(roms.ram_used / 1024), unsigned(roms.flash_used / 1024),
                async ? "on core 0" : "between frames");

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

    DrawJob& job = s_job;
    job.busy = false;
    job.drawn = 0;
    job.draw_us = job.draw_max_us = job.send_us = 0;
    job.pixels = 0;
    job.lines_sent = 0;
    job.warm = false;
    job.drawer = &drawer;
    job.lcd = &lcd;
    job.x0 = x0;
    job.y0 = y0;
    job.m = async ? m : nullptr;

    // The start-up tests (RAM / ROM checks, test patterns, noise) run unseen
    // and unheard, as fast as the board goes, behind a "Loading" card -
    // drawn in the game's orientation, so a landscape game's card is the
    // sign to turn the board (Tom). The picture starts with the attract
    // mode. Back (or holding the screen) cancels.
    bool cancelled = false;
    const char* ended_by = "the controller";    // for the log
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
                    ended_by = boot_in.back() ? "the controller (loading)" : "a held touch (loading)";
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
    // Async: core 1 only emulates; four frames of sound absorb the slower
    // frames of busy moments
    const uint32_t async_target = std::min(queue_max, frame_samples * 4);

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
    uint32_t sec_drawn_base = 0, log_drawn_base = 0, log_lines_base = 0;
    int64_t log_draw_base = 0;
    crash_note::step(g.title, "playing");
    while (!cancelled) {
        in.update(now_ms());
        // Back to the menu: the controller's gesture, or the screen held
        // for a second (a game started without a controller)
        // (async: only while the screen's bus is free - touch shares it on
        // the ESP32-32E boards)
        bool held = false;
        if (!async) {
            held = touch::held_long();
        } else if (xSemaphoreTake(s_bus, 0) == pdTRUE) {
            held = touch::held_long();
            xSemaphoreGive(s_bus);
        }
        if (in.back() || held) {
            ended_by = in.back() ? "the controller" : "a held touch";
            break;
        }
        const uint32_t pad = g.four_way ? four_way.filter(in.game()) : in.game();
        m->set_inputs(games::map_pad(g, pad));
        m->set_analog(in.analog());

        const uint32_t queue_target =
            async ? async_target : std::min(queue_max, std::max(frame_samples * 3, draw_est + frame_samples * 2));
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
                if (async) {
                    const uint32_t d = job.drawn.load();
                    sec_drawn = d - sec_drawn_base;
                    sec_drawn_base = d;
                }
                if (warm && (worst_frames < 0 || int(sec_frames) < worst_frames)) {
                    worst_frames = int(sec_frames);
                    worst_drawn = int(sec_drawn);
                }
                sec_t = now;
                sec_frames = sec_drawn = 0;
            }
        }

        if (async) {
            // Core 0 free: it draws this frame's picture from a copy
            if (!job.busy) {
                job.warm = warm;
                if (m->snapshot()) {
                    job.busy = true;
                    xTaskNotifyGive(s_draw);
                }
            }
        } else if (ring.count() >= draw_est + frame_samples / 2) {
            // (With less sound queued than a draw takes, it's skipped.)
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
        }

        const uint32_t now = now_ms();
        if (now - stat_t >= 5000) {
            if (async) {
                const uint32_t d = job.drawn.load();
                const int64_t du = job.draw_us;
                const uint32_t ls = job.lines_sent;
                drawn = d - log_drawn_base;
                draw_us = du - log_draw_base;
                lines_sent = ls - log_lines_base;
                log_drawn_base = d;
                log_draw_base = du;
                log_lines_base = ls;
            }
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
    all_emu_us += emu_us;
    if (async) {
        // Let the last picture finish; the task then waits for the next game
        while (job.busy) vTaskDelay(1);
        job.m = nullptr;
        all_drawn = job.drawn.load();
        all_draw_us = job.draw_us;
        all_send_us = job.send_us;
        all_pixels = job.pixels;
        draw_max_us = job.draw_max_us;
    } else {
        all_drawn += drawn;
        all_draw_us += draw_us;
    }
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
    devlog::add("%s: left by %s", g.id, ended_by);
    devlog::add("%s: ran %.0f s, %.1f fps (game %.1f), %.0f shown/s; emu %.1f ms (worst %.1f), picture %.1f "
                "(worst %.1f, send %.1f, %.1fK px); slowest second %d frames, %d shown",
                g.id, double(secs), double(s_last.frames_per_s), double(s_last.game_hz), double(s_last.drawn_per_s),
                double(s_last.emu_ms), double(s_last.emu_max_ms), double(s_last.draw_ms), double(s_last.draw_max_ms),
                double(s_last.send_ms), double(s_last.kpixels), s_last.worst_frames, s_last.worst_drawn);

    ring.clear();
    delete m;
    catalog::unload(roms);
    arena::reset();     // the game area is empty again for the next game
    memlog::stacks();
    crash_note::clear();
    screen::apply_rotation();
    lcd.fillScreen(screen::kBg);
}

const Stats& last_stats() { return s_last; }

}  // namespace game_runner
