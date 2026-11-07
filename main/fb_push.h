// Writing a block of pixels into a frame buffer panel's memory (the 5.0"
// RGB board: 800x480 landscape, in PSRAM), turned the way LovyanGFX turns
// it, without LovyanGFX. Plain C++ (no ESP-IDF), host-tested
// (tools/host_tests/fb_push_tests.cpp checks it against LovyanGFX's own
// per-pixel mapping).
//
// Why: the menus and portrait games are portrait, the panel's memory is
// landscape, so every row the runner sends is a column there. LovyanGFX
// 1.2.31 (Panel_FrameBufferBase::writeImage with rotation 1 / 3) does that
// through its general affine pixel copy: an index computed (shifts,
// multiply, add), a transparency compare and a colour conversion call per
// pixel - ~12 instructions a pixel. Here a strip of h rows is written as
// one run of h pixels per frame buffer row: a load and a store a pixel.
// The memory traffic is the same either way (h pixels a frame buffer row).
//
// Rotation r is LovyanGFX's internal one (0-3; ours: the logical rotation
// + BOARD_ROTATION_OFFSET). Logical (x, y) -> native (X, Y), pw x ph the
// native size (Panel_FrameBufferBase::drawPixelPreclipped):
//   r0: X = x,          Y = y
//   r1: X = pw - 1 - y, Y = x
//   r2: X = pw - 1 - x, Y = ph - 1 - y
//   r3: X = y,          Y = ph - 1 - x
#pragma once

#include <cstdint>
#include <cstring>

namespace fb_push {

struct Frame {
    uint16_t* fb = nullptr;
    int stride = 0;     // pixels a native row
    int pw = 0, ph = 0; // native size
    int rot = 0;        // 0-3
    // Native rows written since the last take_rows() (for the cache write-back)
    int row0 = 1 << 30, row1 = -1;

    void touch(int a, int b)   // rows [a, b)
    {
        if (a < row0) row0 = a;
        if (b - 1 > row1) row1 = b - 1;
    }
};

// Native point of logical (x, y)
inline void to_native(const Frame& f, int x, int y, int& X, int& Y)
{
    switch (f.rot & 3) {
    case 0: X = x; Y = y; break;
    case 1: X = f.pw - 1 - y; Y = x; break;
    case 2: X = f.pw - 1 - x; Y = f.ph - 1 - y; break;
    default: X = y; Y = f.ph - 1 - x; break;
    }
}

// w x h pixels (row-major, `px`) at logical (x, y); the caller keeps the
// block on the screen (no clipping here).
inline void write(Frame& f, int x, int y, int w, int h, const uint16_t* px)
{
    if (w <= 0 || h <= 0) return;
    switch (f.rot & 3) {
    case 0:
        for (int r = 0; r < h; ++r) std::memcpy(f.fb + (y + r) * f.stride + x, px + r * w, size_t(w) * 2);
        f.touch(y, y + h);
        break;
    case 2: {
        // rows and columns reversed
        for (int r = 0; r < h; ++r) {
            uint16_t* d = f.fb + (f.ph - 1 - (y + r)) * f.stride + (f.pw - 1 - x);
            const uint16_t* s = px + r * w;
            for (int c = 0; c < w; ++c) d[-c] = s[c];
        }
        f.touch(f.ph - y - h, f.ph - y);
        break;
    }
    case 1: {
        // native row x + c holds column c, the block's last row first
        const int X0 = f.pw - y - h;
        for (int c = 0; c < w; ++c) {
            uint16_t* d = f.fb + (x + c) * f.stride + X0;
            const uint16_t* s = px + (h - 1) * w + c;
            for (int k = 0; k < h; ++k, s -= w) d[k] = *s;
        }
        f.touch(x, x + w);
        break;
    }
    default: {
        // native row ph - 1 - (x + c) holds column c, the first row first
        for (int c = 0; c < w; ++c) {
            uint16_t* d = f.fb + (f.ph - 1 - (x + c)) * f.stride + y;
            const uint16_t* s = px + c;
            for (int k = 0; k < h; ++k, s += w) d[k] = *s;
        }
        f.touch(f.ph - x - w, f.ph - x);
        break;
    }
    }
}

}  // namespace fb_push
