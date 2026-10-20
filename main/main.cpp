// CYD Classic Arcade: start-up and the two cores' jobs.
// Core 0: Bluetooth (app_main ends in BTstack's run loop) and the USB ROM
// loader. Core 1: screen, sound and the emulators (the menu task).
#include <cstdio>

#include <esp_app_format.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

#include "audio_out.h"
#include "audio_ring.h"
#include "board_select.h"
#include "catalog.h"
#include "crash_note.h"
#include "devlog.h"
#include "game_runner.h"
#include "flash_store.h"
#include "memlog.h"
#include "menu.h"
#include "pad.h"
#include "screen.h"
#include "sd_import.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"
#include "version_gen.h"

namespace {

constexpr const char* kTag = "arcade";

AudioRing s_ring;

void audio_fill(int16_t* out, int n, void*) { s_ring.pop(out, uint32_t(n)); }

void sd_status(const char* text) { ESP_LOGI(kTag, "%s", text); }

void ui_task(void*)
{
    // The SD card first, finished and its bus released before the display
    // starts: the two SPI buses mustn't be busy with DMA at the same time.
    const bool store = flash_store::open();
    const int copied = store ? sd_import::run(sd_status) : 0;
    memlog::stage("after SD import");

    screen::init();
    // Touch first: a resistive panel is calibrated once (first start)
    touch::init();
    memlog::stage("after screen + touch");
    {
        LGFX& g = screen::lcd();
        const ui::Metrics m = ui::metrics(g);
        g.fillScreen(ui::kBg);
        g.setTextDatum(textdatum_t::middle_center);
        g.setFont(m.title);
        g.setTextColor(ui::kGold);
        g.drawString("CYD Classic Arcade", m.w / 2, m.h / 2 - m.header_h / 3);
        g.setFont(m.text);
        g.setTextColor(ui::kSub);
        g.drawString(CYD_ARCADE_VERSION, m.w / 2, m.h / 2 + m.header_h / 3);
        g.setTextDatum(textdatum_t::top_left);
        screen::menu_font();
    }
    if (store) {
        if (copied > 0) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%d zip%s copied from the SD card", copied, copied == 1 ? "" : "s");
            screen::line(screen::line_h() * 3, screen::kText, buf);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        catalog::refresh();
        memlog::stage("after ROM catalog");
    } else {
        screen::line(screen::line_h() * 3, screen::kRed, "No ROM storage - reinstall");
    }
    audio::start(audio_fill, nullptr, settings::get().sound_pwm);
    audio::set_volume(settings::get().volume);
    memlog::stage("after sound");
    // The game area is set aside once Bluetooth has started (its own RAM
    // taken first), so every game starts from the same memory
    for (int i = 0; i < 100 && !pad_bt_ready(); ++i) vTaskDelay(pdMS_TO_TICKS(50));
    devlog::add("Ready%s: RAM %u KB free, block %u KB", pad_bt_ready() ? "" : " (Bluetooth not started yet)",
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
    game_runner::init();
    memlog::stage("after game area");
    memlog::blocks("at the menu");
    menu::run(s_ring);
}

// The flash clock the app's image header asks for (the ESP32 sets it from
// there at start-up) and the mode built in: shows a build's flash settings
// reached the board.
void log_flash_setup()
{
    const esp_partition_t* app = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    esp_image_header_t h = {};
    if (!app || esp_partition_read(app, 0, &h, sizeof h) != ESP_OK || h.magic != ESP_IMAGE_HEADER_MAGIC) return;
    const char* clock = h.spi_speed == ESP_IMAGE_SPI_SPEED_DIV_1   ? "80 MHz"
                        : h.spi_speed == ESP_IMAGE_SPI_SPEED_DIV_2 ? "40 MHz"
                                                                   : "under 40 MHz";
#if CONFIG_ESPTOOLPY_FLASHMODE_QIO
    const char* mode = "QIO";
#else
    const char* mode = "DIO";
#endif
    devlog::add("Flash: %s, %s", mode, clock);
}

}  // namespace

extern "C" void app_main(void)
{
    devlog::init();
    {
        const esp_reset_reason_t r = esp_reset_reason();
        const char* why = r == ESP_RST_POWERON ? "power on" : r == ESP_RST_SW ? "restart" : r == ESP_RST_PANIC ? "crash"
                          : r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ? "watchdog"
                          : r == ESP_RST_BROWNOUT ? "brownout" : "other";
        devlog::add("Start %s (%s) on %s; reset: %s", CYD_ARCADE_VERSION, CYD_ARCADE_BUILD, BOARD_NAME, why);
        crash_note::at_start();   // a crash's details into the log
        log_flash_setup();
        devlog::add("Heap: %.1f KB internal in all", double(heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) / 1024.0);
        memlog::stage("at start");
    }
    // BTstack keeps controller pairings in NVS; our settings live there too
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings::load();
    pad_set_swap_ab(settings::get().swap_ab);
    memlog::stage("after NVS");
    ESP_LOGI(kTag, "CYD Classic Arcade %s (%s) on %s", CYD_ARCADE_VERSION, CYD_ARCADE_BUILD, BOARD_NAME);
    xTaskCreatePinnedToCore(ui_task, "ui", 12288, nullptr, 5, nullptr, 1);
    memlog::stage("before Bluetooth");
    pad_bt_run();  // never returns
}
