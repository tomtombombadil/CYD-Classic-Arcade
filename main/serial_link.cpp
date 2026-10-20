// UART0 reader for the ROM loader (see serial_link.h). Boards with native
// USB (BOARD_USB_SERIAL_JTAG, the ESP32-S3 ones) use the USB-Serial-JTAG
// port instead; the log is copied there too (dropped when nobody reads), so
// the console stays on UART0 and never writes into our replies.
#include "serial_link.h"

#include "board_select.h"
#include "catalog.h"
#include "core/usb_import.h"
#include "flash_store.h"
#include "version_gen.h"

#include <cstdarg>
#include <cstdio>
#if BOARD_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#else
#include <driver/uart.h>
#endif
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>

namespace serial_link {
namespace {

constexpr const char* kTag = "usb";
#if BOARD_USB_SERIAL_JTAG
int port_read(uint8_t* buf, int len, TickType_t wait) { return usb_serial_jtag_read_bytes(buf, uint32_t(len), wait); }
void port_write(const char* data, size_t len) { usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(200)); }

vprintf_like_t s_uart_log = nullptr;

// The log to UART0 as before, and to USB without waiting
int log_both(const char* fmt, va_list ap)
{
    char buf[256];
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, copy);
    va_end(copy);
    if (n > 0) usb_serial_jtag_write_bytes(buf, size_t(n) < sizeof buf ? size_t(n) : sizeof buf - 1, 0);
    return s_uart_log ? s_uart_log(fmt, ap) : n;
}
#else
constexpr uart_port_t kPort = UART_NUM_0;
int port_read(uint8_t* buf, int len, TickType_t wait) { return uart_read_bytes(kPort, buf, uint32_t(len), wait); }
void port_write(const char* data, size_t len) { uart_write_bytes(kPort, data, len); }
#endif

usb_import::Session s_session;
std::atomic<bool> s_changed{false};
std::atomic<bool> s_busy{false};
std::atomic<bool> s_stop{false};
std::atomic<bool> s_parked{false};
TaskHandle_t s_task = nullptr;

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }

void send(void*, const char* line)
{
    // A newline first, so the reply starts a line even mid-way through log
    // output; one write, so no log line lands inside it
    char buf[704];
    const int n = std::snprintf(buf, sizeof buf, "\n%s\n", line);
    if (n > 0) port_write(buf, size_t(n) < sizeof buf ? size_t(n) : sizeof buf - 1);
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
        // stop() waits for this, then deletes the task (deleted while
        // suspended, its stack is freed at once - not later by the idle task)
        if (s_stop) {
            s_parked = true;
            vTaskSuspend(nullptr);
        }
        const int n = port_read(buf, sizeof buf, pdMS_TO_TICKS(50));
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
    // RX buffer: the web page sends a 2 KB chunk and waits for its ACK, so
    // 4 KB holds a chunk plus the command lines while a flash erase holds
    // the task up (it was 16 KB until v0.10.1 - RAM games need).
#if BOARD_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = {};
    cfg.tx_buffer_size = 2048;
    cfg.rx_buffer_size = 4096;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err == ESP_OK) s_uart_log = esp_log_set_vprintf(log_both);
#else
    esp_err_t err = uart_driver_install(kPort, 4096, 0, 0, nullptr, 0);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "serial port not available (%s): no ROM loading over USB", esp_err_to_name(err));
        return;
    }
    s_session.init(flash_store::store(), send, games, nullptr, CYD_ARCADE_VERSION);
    s_stop = false;
    s_parked = false;
    if (xTaskCreatePinnedToCore(task, "usb", 6144, nullptr, 4, &s_task, 0) != pdPASS) {
        s_task = nullptr;
#if BOARD_USB_SERIAL_JTAG
        esp_log_set_vprintf(s_uart_log);
        usb_serial_jtag_driver_uninstall();
#else
        uart_driver_delete(kPort);
#endif
        ESP_LOGE(kTag, "no room for the usb task: no ROM loading over USB");
    }
}

bool stop()
{
    if (!s_task || s_busy) return false;
    s_stop = true;
    while (!s_parked) vTaskDelay(pdMS_TO_TICKS(5));
    vTaskDelete(s_task);
    s_task = nullptr;
#if BOARD_USB_SERIAL_JTAG
    esp_log_set_vprintf(s_uart_log);
    usb_serial_jtag_driver_uninstall();
#else
    uart_driver_delete(kPort);
#endif
    s_busy = false;
    return true;
}

bool take_changed() { return s_changed.exchange(false); }
bool busy() { return s_busy; }

}  // namespace serial_link
