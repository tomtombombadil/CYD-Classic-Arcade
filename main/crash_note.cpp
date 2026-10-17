// Crash note (see crash_note.h).
#include "crash_note.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

namespace crash_note {
namespace {

constexpr uint32_t kMagic = 0x43524E31;     // "CRN1"

struct Note {
    uint32_t magic;
    char     game[24];
    char     what[32];
    uint32_t free_kb, block_kb;
};

RTC_NOINIT_ATTR Note s_note;

void copy(char* dst, size_t cap, const char* src)
{
    std::strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}

}  // namespace

void step(const char* game, const char* what)
{
    copy(s_note.game, sizeof s_note.game, game);
    copy(s_note.what, sizeof s_note.what, what);
    s_note.free_kb = uint32_t(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    s_note.block_kb = uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    s_note.magic = kMagic;
}

void clear() { s_note.magic = 0; }

bool last(char* out, size_t cap)
{
    const esp_reset_reason_t r = esp_reset_reason();
    const char* kind = r == ESP_RST_PANIC     ? "crashed"
                       : r == ESP_RST_INT_WDT ? "hung (interrupt watchdog)"
                       : r == ESP_RST_TASK_WDT ? "hung (task watchdog)"
                       : r == ESP_RST_WDT     ? "hung (watchdog)"
                       : r == ESP_RST_BROWNOUT ? "lost power (brownout)"
                                               : nullptr;
    const bool noted = s_note.magic == kMagic;
    s_note.magic = 0;
    if (!kind) return false;
    s_note.game[sizeof s_note.game - 1] = 0;
    s_note.what[sizeof s_note.what - 1] = 0;
    if (noted)
        std::snprintf(out, cap, "%s %s while %s (%u KB free, block %u KB).", s_note.game, kind, s_note.what,
                      unsigned(s_note.free_kb), unsigned(s_note.block_kb));
    else
        std::snprintf(out, cap, "The board %s.", kind);
    return true;
}

}  // namespace crash_note
