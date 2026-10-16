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

// The picture and its drawing; the segment storage is in Sized<N> below
// (the most segments a picture holds, 16 bytes each with the sort list,
// plus the active list - more are counted and dropped).
class Base {
public:
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;

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
    int capacity() const { return cap_; }

    // 14 bytes (16-bit aligned): the slope is kept in two halves.
    struct Seg {
        int16_t  ax, ay, bx, by;    // ay <= by, 1/16 px
        uint16_t color;
        uint16_t slope_lo;          // dx per 1/16 y, 16.16 (0 for flat lines)
        int16_t  slope_hi;
        int32_t  slope() const { return int32_t(uint32_t(uint16_t(slope_hi)) << 16 | slope_lo); }
    };

protected:
    Base(Seg* seg, uint16_t* order, uint16_t* active, int cap, int active_cap)
        : seg_(seg), order_(order), active_(active), cap_(cap), active_cap_(active_cap) {}

private:
    void store(int ax, int ay, int bx, int by, uint16_t color);
    void draw(const Seg& s, int y, uint16_t* out) const;
    // The last row a segment touches (a dot covers the next one too).
    int  last_row(const Seg& s) const
    {
        const int r = s.by / kSub;
        return (s.ax == s.bx && s.ay == s.by && r + 1 < height_) ? r + 1 : r;
    }

    Seg*      seg_;
    // Segment numbers sorted by first row.
    uint16_t* order_;
    // Rendering state: the segments crossing the row last drawn.
    uint16_t* active_;
    int       cap_;
    int       active_cap_;          // most segments crossing one row (more aren't drawn there)
    int       width_ = 0, height_ = 0;
    int       count_ = 0;
    int       dropped_ = 0;
    int       active_n_ = 0;
    int       next_ = 0;            // next in order_ not yet active
    int       last_y_ = -2;
};

static_assert(sizeof(Base::Seg) == 14, "segments pack into 14 bytes");

// A display holding up to N segments (N x 16 bytes), at most A of them
// crossing one row (A x 2 bytes; A = N can never run short).
template <int N, int A = N>
class Sized : public Base {
public:
    static constexpr int kMaxSegments = N;
    Sized() : Base(seg_store_, order_store_, active_store_, N, A) {}

private:
    Seg      seg_store_[N];
    uint16_t order_store_[N];
    uint16_t active_store_[A];
};

// Asteroids' and Tempest's: 1024 segments, ~18 KB.
using Display = Sized<1024>;

} // namespace vector_display
