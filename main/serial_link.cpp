// UART0 reader for the ROM loader (see serial_link.h).
#include "serial_link.h"

#include "catalog.h"
#include "core/usb_import.h"
#include "flash_store.h"
#include "version_gen.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>

namespace serial_link {
namespace {

constexpr const char* kTag = "usb";
constexpr uart_port_t kPort = UART_NUM_0;

usb_import::Session s_session;
std::atomic<bool> s_changed{false};
std::atomic<bool> s_busy{false};

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

void send(void*, const char* line)
{
    // A newline first, so the reply starts a line even mid-way through log output
    uart_write_bytes(kPort, "\n", 1);
    uart_write_bytes(kPort, line, std::strlen(line));
    uart_write_bytes(kPort, "\n", 1);
}

void games(void*, char* out, size_t cap)
{
    catalog::refresh();
    catalog::list_ids(out, cap);
}

void task(void*)
{
    static uint8_t buf[512];
    for (;;) {
        const int n = uart_read_bytes(kPort, buf, sizeof buf, pdMS_TO_TICKS(50));
        // A stored zip only shows in the directory once it's whole, so the
        // menu may read the store between these pieces.
        if (n > 0) {
            flash_store::Lock lock;
            s_session.feed(buf, size_t(n), now_ms());
        }
        {
            flash_store::Lock lock;
            s_session.tick(now_ms());
        }
        s_busy = s_session.receiving();
        if (s_session.take_done()) s_changed = true;
    }
}

}  // namespace

void start()
{
    // RX buffer for about a second at 115200 baud: flash erases can hold the
    // task up while a chunk arrives.
    esp_err_t err = uart_driver_install(kPort, 16384, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "serial port not available (%s): no ROM loading over USB", esp_err_to_name(err));
        return;
    }
    s_session.init(flash_store::store(), send, games, nullptr, CYD_ARCADE_VERSION);
    xTaskCreatePinnedToCore(task, "usb", 4096, nullptr, 4, nullptr, 0);
}

bool take_changed() { return s_changed.exchange(false); }
bool busy() { return s_busy; }

}  // namespace serial_link
