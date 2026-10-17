// Vector monitor picture - see vector_display.h.
#include "vector_display.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace vector_display {

void Base::init(int width, int height)
{
    width_ = std::min(width, 512);
    height_ = std::min(height, 512);
    xlim_ = width_ * kSub - 1;
    ylim_ = height_ * kSub - 1;
    begin();
    finish();
}

void Base::begin()
{
    count_ = 0;
    dropped_ = 0;
    last_y_ = -2;
}

void Base::add_clipped(int x0, int y0, int x1, int y1, uint16_t color)
{
    const int xlim = xlim_, ylim = ylim_;
    // Clip to the screen (Liang-Barsky on the parameter t of the line).
    const float xmax = float(xlim), ymax = float(ylim);
    const float dx = float(x1 - x0), dy = float(y1 - y0);
    float t0 = 0.0f, t1 = 1.0f;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {float(x0), xmax - float(x0), float(y0), ymax - float(y0)};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return;            // parallel to this edge and outside it
            continue;
        }
        const float r = q[i] / p[i];
        if (p[i] < 0.0f) { if (r > t1) return; if (r > t0) t0 = r; }
        else             { if (r < t0) return; if (r < t1) t1 = r; }
    }
    int ax = x0, ay = y0, bx = x1, by = y1;
    if (t0 > 0.0f) { ax = int(float(x0) + t0 * dx + 0.5f); ay = int(float(y0) + t0 * dy + 0.5f); }
    if (t1 < 1.0f) { bx = int(float(x0) + t1 * dx + 0.5f); by = int(float(y0) + t1 * dy + 0.5f); }
    // Float rounding far off the screen must not leave it (drawing takes
    // every stored coordinate as on the screen).
    store(std::clamp(ax, 0, xlim), std::clamp(ay, 0, ylim), std::clamp(bx, 0, xlim), std::clamp(by, 0, ylim), color);
}

void Base::finish()
{
    // Counting sort of the segments by their first row.
    uint16_t start[513] = {};
    for (int i = 0; i < count_; ++i) ++start[(seg_[i].ay >> 4) + 1];
    for (int r = 1; r <= height_; ++r) start[r] = uint16_t(start[r] + start[r - 1]);
    for (int i = 0; i < count_; ++i) order_[start[seg_[i].ay >> 4]++] = uint16_t(i);
    last_y_ = -2;
}

// Coordinates are clipped to the screen, so they're never negative: / kSub
// is a shift. A sloped line's x between its ends never leaves the range of
// its ends' x (the slope is truncated towards zero), and slope x t, for t
// within the segment's rows, is at most |dx| x 65536 < 2^29: 32 bits hold
// it (the same value the 64-bit product gave).
inline void Base::draw(const Seg& s, int y, uint16_t* out) const
{
    int xa, xb;
    if (s.ay == s.by) {
        xa = s.ax;
        xb = s.bx;
        if (xa == xb) xb += kSub;               // a dot: 2 pixels wide
    } else {
        // Where the line enters and leaves this row.
        const int top = y * kSub;
        const int t0 = top > s.ay ? top - s.ay : 0;
        const int t1 = (s.by < top + kSub - 1 ? s.by : top + kSub - 1) - s.ay;
        const int32_t slope = s.slope();
        xa = s.ax + ((slope * t0) >> 16);
        xb = s.ax + ((slope * t1) >> 16);
    }
    if (xa > xb) std::swap(xa, xb);
    const int p0 = xa >> 4, p1 = std::min(xb >> 4, width_ - 1);
    static_assert(kSub == 16, "the shifts above divide by kSub");
    const uint16_t c = s.color;
    for (uint16_t *p = out + p0, *e = out + p1; p <= e; ++p) *p = c;
}

void Base::render_line(int y, uint16_t* out)
{
    std::fill(out, out + width_, uint16_t(0));
    if (y < 0 || y >= height_) return;
    if (y != last_y_ + 1) {
        // Not the next row: find the active segments from scratch.
        active_n_ = 0;
        next_ = 0;
        while (next_ < count_ && seg_[order_[next_]].ay / kSub < y) {
            const int last = last_row(seg_[order_[next_]]);
            if (last >= y && active_n_ < active_cap_) active_[active_n_++] = uint32_t(order_[next_]) << 16 | uint32_t(last);
            ++next_;
        }
        std::sort(active_, active_ + active_n_);
    } else {
        // Drop the ones that ended above this row.
        int n = 0;
        for (int i = 0; i < active_n_; ++i)
            if (int(active_[i] & 0xffff) >= y) active_[n++] = active_[i];
        active_n_ = n;
    }
    // New ones join in the order the lines were added (later lines on top):
    // both lists are in that order (the sort by first row keeps it), so they
    // merge from the back. Past active_cap_ the rest of this row's new ones
    // are left out.
    int k = 0;
    while (next_ + k < count_ && (seg_[order_[next_ + k]].ay >> 4) == y) ++k;
    const uint16_t* add = order_ + next_;
    next_ += k;
    if (k > active_cap_ - active_n_) k = active_cap_ - active_n_;
    if (k > 0) {
        int i = active_n_ - 1, j = k - 1, w = active_n_ + k - 1;
        while (j >= 0) {
            const uint32_t v = uint32_t(add[j]) << 16;
            if (i >= 0 && active_[i] > v) {
                active_[w--] = active_[i--];
            } else {
                active_[w--] = v | uint32_t(last_row(seg_[add[j]]));
                --j;
            }
        }
        active_n_ += k;
    }
    last_y_ = y;
    for (int i = 0; i < active_n_; ++i) draw(seg_[active_[i] >> 16], y, out);
}

bool Copy::take(const Base& src)
{
    const int n = src.count_;
    if (n > cap_) {
        const int cap = (n + 63) & ~63;
        const int active = std::min(src.active_cap_, cap);
        // active list (4 bytes each), then segments (14), then the sort list (2)
        const int words = active + (cap * int(sizeof(Seg) + 2) + 3) / 4;
        delete[] buf_;
        buf_ = new (std::nothrow) uint32_t[size_t(words)];
        if (!buf_) {
            cap_ = active_cap_ = count_ = 0;
            bytes_ = 0;
            return false;
        }
        bytes_ = words * 4;
        active_ = buf_;
        seg_ = reinterpret_cast<Seg*>(buf_ + active);
        order_ = reinterpret_cast<uint16_t*>(seg_ + cap);
        cap_ = cap;
        active_cap_ = active;
    }
    width_ = src.width_;
    height_ = src.height_;
    xlim_ = src.xlim_;
    ylim_ = src.ylim_;
    count_ = n;
    dropped_ = src.dropped_;
    if (n > 0) {
        std::memcpy(seg_, src.seg_, size_t(n) * sizeof(Seg));
        std::memcpy(order_, src.order_, size_t(n) * sizeof(uint16_t));
    }
    last_y_ = -2;
    return true;
}

} // namespace vector_display
