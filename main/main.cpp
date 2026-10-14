// CYD Classic Arcade: start-up and the two cores' jobs.
// Core 0: Bluetooth (app_main ends in BTstack's run loop) and the USB ROM
// loader. Core 1: screen, sound and the emulators (the menu task).
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "audio_out.h"
#include "audio_ring.h"
#include "board_select.h"
#include "catalog.h"
#include "flash_store.h"
#include "menu.h"
#include "pad.h"
#include "screen.h"
#include "sd_import.h"
#include "serial_link.h"
#include "settings.h"
#include "version_gen.h"

namespace {

constexpr const char* kTag = "arcade";

AudioRing s_ring;

void audio_fill(int16_t* out, int n, void*) { s_ring.pop(out, uint32_t(n)); }

void sd_status(const char* text)
{
    screen::line(screen::line_h() * 3, screen::kText, text);
}

void ui_task(void*)
{
    screen::init();
    screen::line(0, screen::kGold, "CYD Classic Arcade");
    screen::line(screen::line_h(), screen::kMuted, CYD_ARCADE_VERSION);

    if (flash_store::open()) {
        // New zips on the SD card go into the store first (ESP32-32E boards)
        if (sd_import::run(sd_status) > 0) sd_status("ROMs copied from the SD card");
        catalog::refresh();
        serial_link::start();
    } else {
        screen::line(screen::line_h() * 3, screen::kRed, "No ROM storage - reinstall");
    }
    audio::start(audio_fill, nullptr);
    audio::set_volume(settings::get().volume);
    ESP_LOGI(kTag, "ready: %u KB free, largest block %u KB",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    menu::run(s_ring);
}

}  // namespace

extern "C" void app_main(void)
{
    // BTstack keeps controller pairings in NVS; our settings live there too
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings::load();
    pad_set_swap_ab(settings::get().swap_ab);
    ESP_LOGI(kTag, "CYD Classic Arcade %s (%s) on %s", CYD_ARCADE_VERSION, CYD_ARCADE_BUILD, BOARD_NAME);
    xTaskCreatePinnedToCore(ui_task, "ui", 12288, nullptr, 5, nullptr, 1);
    pad_bt_run();  // never returns
}
