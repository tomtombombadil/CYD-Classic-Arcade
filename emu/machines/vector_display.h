// A vector monitor's picture for line-by-line output, written for this
// project (MIT). The vector generators (Atari's DVG and AVG) hand over each
// picture as lines; they're kept as a compact list of segments in output
// pixel coordinates (1/16 pixel units), sorted by their first row, and
// drawn one output line at a time - no frame buffer (a 320 x 240 RGB565
// picture would be 150 KB).
//
// Lines are 1 pixel wide (each row gets the run of pixels the line crosses
// in it, so steep and flat lines both join up); a line whose ends meet is a
// dot, 2 x 2 pixels (a 1-pixel bullet is hard to see). Where lines cross,
// the later one wins.
#pragma once

#include <cstdint>

namespace vector_display {

// Fixed point: coordinates are in 1/16 pixel.
constexpr int kSub = 16;

class Display {
public:
    // The most segments a picture holds (more are counted and dropped).
    // 20 bytes each with the sort and active lists.
    static constexpr int kMaxSegments = 1024;

    // Output size in pixels (at most 512 x 512).
    void init(int width, int height);

    // Starts a new picture: the old one is gone. The machine builds a
    // whole picture (begin, add..., finish) inside one run_frame(), so
    // render_line() never sees half of one.
    void begin();
    // Adds a line from (x0, y0) to (x1, y1) in 1/16 pixels, clipped to the
    // screen; `color` is the pixel value written (RGB565 as the machine
    // wants it).
    void add(int x0, int y0, int x1, int y1, uint16_t color);
    // Ends the picture: sorts it for render_line().
    void finish();

    // Output line y (0 .. height - 1): width pixels.
    void render_line(int y, uint16_t* out);

    int width() const { return width_; }
    int height() const { return height_; }
    int count() const { return count_; }            // segments in the shown picture
    int dropped() const { return dropped_; }        // lost in the last picture (too many)

private:
    struct Seg {
        int16_t  ax, ay, bx, by;    // ay <= by, 1/16 px
        int32_t  slope;             // dx per 1/16 y, 16.16 (0 for flat lines)
        uint16_t color;
        uint16_t last_row;          // the last row it touches
    };

    void store(int ax, int ay, int bx, int by, uint16_t color);
    void draw(const Seg& s, int y, uint16_t* out) const;

    int      width_ = 0, height_ = 0;
    Seg      seg_[kMaxSegments];
    int      count_ = 0;
    int      dropped_ = 0;
    // Segment numbers sorted by first row.
    uint16_t order_[kMaxSegments];
    // Rendering state: the segments crossing the row last drawn.
    uint16_t active_[kMaxSegments];
    int      active_n_ = 0;
    int      next_ = 0;             // next in order_ not yet active
    int      last_y_ = -2;
};

} // namespace vector_display
