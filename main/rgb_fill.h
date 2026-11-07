// The 5.0" RGB panel's bounce-buffer refill, done by us and timed (see
// rgb_fill.cpp). The panel's DMA reads two small buffers in internal RAM;
// each time it finishes one, the driver's interrupt calls fill() to copy the
// next piece of the frame buffer (PSRAM) into it. A refill that finishes
// late shows a slice of old data: Tom's striped header (v0.26.1-v0.26.5,
// both boards; Rotate Screen 180 left the stripes on the same place of the
// glass - a fault in sending the picture, not in the picture).
#pragma once

#include <cstddef>
#include <cstdint>

#if __has_include(<esp_lcd_types.h>)
#include <esp_lcd_types.h>
#endif

namespace rgb_fill {

// The frame buffer the refills copy from (native rows, RGB565 as LovyanGFX
// writes it) and its size.
void set_frame(const uint16_t* fb, size_t bytes);

// The driver's on_bounce_empty callback (esp_lcd_rgb_panel_bounce_buf_fill_cb_t).
#if __has_include(<esp_lcd_types.h>)
bool fill(esp_lcd_panel_handle_t panel, void* bounce_buf, int pos_px, int len_bytes, void* user_ctx);
#endif

struct Stats {
    uint32_t fills;        // refills since the last take()
    uint32_t gap_us_max;   // longest time from one refill's start to the next
    uint32_t copy_us_max;  // longest refill (copy + preload)
    uint32_t late;         // refills whose start came > 1.5 budgets after the last
    uint32_t budget_us;    // the time the DMA takes for one bounce buffer
};
// The numbers since the last call (the maxima start over).
Stats take();

}  // namespace rgb_fill
