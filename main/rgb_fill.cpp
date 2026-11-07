// The 5.0" RGB panel's bounce-buffer refill (see rgb_fill.h). The same work
// ESP-IDF's driver does in its own fill (a copy from the frame buffer through
// the data cache, then a cache preload of the next piece), plus a cycle
// count of each refill so the board can say whether they come in time.
#include "rgb_fill.h"

#include "board_select.h"

#if BOARD_RGB_PANEL

#include <cstring>
#include <esp_attr.h>
#include <esp_cpu.h>
#include <esp32s3/rom/cache.h>

namespace rgb_fill {
namespace {

const uint8_t* s_fb = nullptr;
size_t s_bytes = 0;

// Written by the interrupt (one core), read by take(): plain words, a
// reading a refill apart is good enough for the log
volatile uint32_t s_fills = 0, s_gap_max = 0, s_copy_max = 0, s_late = 0;
uint32_t s_last = 0;

constexpr uint32_t kCpuMhz = 240;
// One bounce buffer at the panel's clock: lines x (width + porches + sync)
constexpr uint32_t kBudgetUs =
    uint32_t(uint64_t(BOARD_RGB_BOUNCE_LINES) * (BOARD_NATIVE_W + 16) * 1000000ull / BOARD_RGB_PCLK_HZ);

}  // namespace

void set_frame(const uint16_t* fb, size_t bytes)
{
    s_fb = reinterpret_cast<const uint8_t*>(fb);
    s_bytes = bytes;
}

IRAM_ATTR bool fill(esp_lcd_panel_handle_t, void* bounce_buf, int pos_px, int len_bytes, void*)
{
    const uint32_t t0 = esp_cpu_get_cycle_count();
    const size_t off = size_t(pos_px) * 2;
    if (s_fb && off + size_t(len_bytes) <= s_bytes) {
        std::memcpy(bounce_buf, s_fb + off, size_t(len_bytes));
        // the next piece into the cache while the DMA sends this one
        size_t next = off + size_t(len_bytes);
        if (next >= s_bytes) next = 0;
        Cache_Start_DCache_Preload(uint32_t(reinterpret_cast<uintptr_t>(s_fb + next)), uint32_t(len_bytes), 0);
    }
    const uint32_t t1 = esp_cpu_get_cycle_count();
    // The refills around a frame's start wait out the vertical blank (16
    // lines of porch and sync): their gaps aren't lateness
    const bool frame_start = pos_px < 3 * BOARD_NATIVE_W * BOARD_RGB_BOUNCE_LINES;
    if (s_last && !frame_start) {
        const uint32_t gap = (t0 - s_last) / kCpuMhz;
        if (gap > s_gap_max) s_gap_max = gap;
        if (gap > kBudgetUs + kBudgetUs / 2) s_late = s_late + 1;
    }
    s_last = t0;
    const uint32_t copy = (t1 - t0) / kCpuMhz;
    if (copy > s_copy_max) s_copy_max = copy;
    s_fills = s_fills + 1;
    return false;
}

Stats take()
{
    Stats s{s_fills, s_gap_max, s_copy_max, s_late, kBudgetUs};
    s_fills = 0;
    s_gap_max = 0;
    s_copy_max = 0;
    s_late = 0;
    return s;
}

}  // namespace rgb_fill

#else

namespace rgb_fill {
void set_frame(const uint16_t*, size_t) {}
Stats take() { return Stats{}; }
}  // namespace rgb_fill

#endif
