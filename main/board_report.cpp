// What hardware this is (see board_report.h).
#include "board_report.h"

#include "board_select.h"
#include "devlog.h"

#include <sdkconfig.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if BOARD_REPORT
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#if CONFIG_SPIRAM
#include <esp_psram.h>
#endif
#endif

namespace board_report {
namespace {

constexpr int kLines = 7;
char s_text[kLines][64];
const char* s_ptr[kLines];
int s_n = 0;

// A line for the log and About
void add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void add(const char* fmt, ...)
{
    if (s_n >= kLines) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(s_text[s_n], sizeof s_text[s_n], fmt, ap);
    va_end(ap);
    devlog::add("HW %s", s_text[s_n]);
    s_ptr[s_n] = s_text[s_n];
    ++s_n;
}

}  // namespace

#if BOARD_REPORT

void run()
{
    s_n = 0;
    {
        esp_chip_info_t c;
        esp_chip_info(&c);
        add("Chip: %s rev %d.%d, %d cores%s%s", c.model == CHIP_ESP32S3 ? "ESP32-S3" : "ESP32 family",
            c.revision / 100, c.revision % 100, c.cores, (c.features & CHIP_FEATURE_EMB_PSRAM) ? ", PSRAM in chip" : "",
            (c.features & CHIP_FEATURE_EMB_FLASH) ? ", flash in chip" : "");
    }
    {
        uint32_t size = 0, id = 0;
        esp_flash_get_size(nullptr, &size);
        esp_flash_read_id(nullptr, &id);
        add("Flash: %u MB (ID %06X); built for %s", unsigned(size >> 20), unsigned(id), CONFIG_ESPTOOLPY_FLASHSIZE);
    }
    {
#if CONFIG_SPIRAM
#if CONFIG_SPIRAM_MODE_OCT
        const char* built = "octal";
#else
        const char* built = "quad";
#endif
        const bool up = esp_psram_is_initialized();
        if (up)
            add("PSRAM: %u KB, %s (as built), %u KB free", unsigned(esp_psram_get_size() / 1024), built,
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        else
            add("PSRAM: none found (built for %s)", built);
#else
        add("PSRAM: not in this build");
#endif
    }
    // The panel timing in use (the board file's), to set against what a
    // look-alike board's maker documents
    add("Panel: %dx%d RGB, PCLK %u kHz, H/V 4/4/8, falling edge", BOARD_NATIVE_W, BOARD_NATIVE_H,
        unsigned(BOARD_RGB_PCLK_HZ / 1000));
    // Every I2C address that answers on the touch's bus (LovyanGFX's I2C;
    // a 1-byte read: harmless for the GT911 and most chips)
    char found[64] = "";
    int nfound = 0;
    uint8_t gt911 = 0;
    if (lgfx::i2c::init(BOARD_I2C_PORT, BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL).has_value()) {
        for (int a = 0x08; a < 0x78; ++a) {
            uint8_t b;
            if (!lgfx::i2c::transactionRead(BOARD_I2C_PORT, a, &b, 1, 100000).has_value()) continue;
            const size_t len = std::strlen(found);
            if (len + 6 < sizeof found) std::snprintf(found + len, sizeof found - len, "%s%02X", nfound ? " " : "", a);
            ++nfound;
            if ((a == 0x5D || a == 0x14) && !gt911) gt911 = uint8_t(a);
        }
    }
    add("I2C SDA %d SCL %d: %s", BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, nfound ? found : "nothing answers");
    if (gt911) {
        auto rd = [&](uint16_t reg, uint8_t* out, size_t n) {
            const uint8_t r[2] = {uint8_t(reg >> 8), uint8_t(reg)};
            return lgfx::i2c::transactionWriteRead(BOARD_I2C_PORT, gt911, r, 2, out, n, 100000).has_value();
        };
        uint8_t id[4] = {}, fw[2] = {}, cfg = 0, res[4] = {};
        const bool ok = rd(0x8140, id, 4) && rd(0x8144, fw, 2) && rd(0x8047, &cfg, 1) && rd(0x8048, res, 4);
        char name[5];
        for (int i = 0; i < 4; ++i) name[i] = (id[i] >= 0x20 && id[i] < 0x7F) ? char(id[i]) : (id[i] ? '?' : 0);
        name[4] = 0;
        if (ok)
            add("Touch %02X: ID \"%s\", fw %04X, config v%02X, %d x %d", gt911, name, unsigned(fw[0] | fw[1] << 8),
                cfg, res[0] | res[1] << 8, res[2] | res[3] << 8);
        else
            add("Touch %02X: answers, registers unread", gt911);
    }
}

#else

void run() {}

#endif

int lines(const char* const** out)
{
    *out = s_ptr;
    return s_n;
}

}  // namespace board_report
