// Memory report (see memlog.h).
#include "memlog.h"

#include "devlog.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>

namespace memlog {
namespace {

constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

struct Top {
    static constexpr int kN = 8;
    uint32_t size[kN] = {};
    int      more = 0;          // free blocks of 1 KB and up not in the list
    void add(uint32_t s)
    {
        if (s < 1024) return;
        int i = kN;
        while (i > 0 && size[i - 1] < s) --i;
        if (i == kN) {
            ++more;
            return;
        }
        if (size[kN - 1]) ++more;
        for (int j = kN - 1; j > i; --j) size[j] = size[j - 1];
        size[i] = s;
    }
};

bool walk(walker_heap_into_t, walker_block_info_t b, void* user)   // (sic: IDF's spelling)
{
    if (!b.used) static_cast<Top*>(user)->add(uint32_t(b.size));
    return true;
}

}  // namespace

void stage(const char* what)
{
    devlog::add("Mem %s: %.1f KB free, block %.1f KB", what, double(heap_caps_get_free_size(kCaps)) / 1024.0,
                double(heap_caps_get_largest_free_block(kCaps)) / 1024.0);
}

void blocks(const char* what)
{
    Top t;
    heap_caps_walk(kCaps, walk, &t);
    char text[96];
    int n = std::snprintf(text, sizeof text, "Free blocks %s:", what);
    for (int i = 0; i < Top::kN && t.size[i] && n < int(sizeof text); ++i)
        n += std::snprintf(text + n, sizeof text - size_t(n), " %.1f", double(t.size[i]) / 1024.0);
    if (t.more && n < int(sizeof text)) std::snprintf(text + n, sizeof text - size_t(n), " +%d", t.more);
    devlog::add("%s", text);
}

void stacks()
{
    // Task name, stack size as created (bytes); the least free since start
    static const struct {
        const char* name;
        uint32_t    size;
    } kTasks[] = {{"main", CONFIG_ESP_MAIN_TASK_STACK_SIZE}, {"ui", 12288}, {"usb", 6144}, {"draw", 5120},
                  {"audio", 3072}};
    char text[112];
    int n = std::snprintf(text, sizeof text, "Stacks used / size KB:");
    for (const auto& t : kTasks) {
        TaskHandle_t h = xTaskGetHandle(t.name);
        if (!h || n >= int(sizeof text)) continue;
        const uint32_t free_min = uint32_t(uxTaskGetStackHighWaterMark(h)) * sizeof(StackType_t);
        n += std::snprintf(text + n, sizeof text - size_t(n), " %s %.1f/%.0f", t.name,
                           double(t.size - free_min) / 1024.0, double(t.size) / 1024.0);
    }
    devlog::add("%s", text);
}

}  // namespace memlog

extern "C" void memlog_stage_c(const char* what) { memlog::stage(what); }
