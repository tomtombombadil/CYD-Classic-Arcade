// Vector monitor picture - see vector_display.h.
#include "vector_display.h"

#include <algorithm>

namespace vector_display {

void Display::init(int width, int height)
{
    width_ = std::min(width, 512);
    height_ = std::min(height, 512);
    begin();
    finish();
}

void Display::begin()
{
    count_ = 0;
    dropped_ = 0;
    last_y_ = -2;
}

void Display::add(int x0, int y0, int x1, int y1, uint16_t color)
{
    const int xlim = width_ * kSub - 1, ylim = height_ * kSub - 1;
    if (x0 >= 0 && x1 >= 0 && y0 >= 0 && y1 >= 0 && x0 <= xlim && x1 <= xlim && y0 <= ylim && y1 <= ylim) {
        store(x0, y0, x1, y1, color);
        return;
    }
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
    store(ax, ay, bx, by, color);
}

void Display::store(int ax, int ay, int bx, int by, uint16_t color)
{
    if (count_ >= kMaxSegments) {
        ++dropped_;
        return;
    }
    if (ay > by) { std::swap(ax, bx); std::swap(ay, by); }
    Seg& s = seg_[count_++];
    s.ax = int16_t(ax);
    s.ay = int16_t(ay);
    s.bx = int16_t(bx);
    s.by = int16_t(by);
    // dx << 16 fits 32 bits: |dx| < 512 x 16.
    s.slope = by > ay ? int32_t(((bx - ax) * 65536) / (by - ay)) : 0;
    s.color = color;
    s.last_row = uint16_t(by / kSub);
    // A dot covers the next row too (if there is one).
    if (ax == bx && ay == by && s.last_row + 1 < height_) ++s.last_row;
}

void Display::finish()
{
    // Counting sort of the segments by their first row.
    uint16_t start[513] = {};
    for (int i = 0; i < count_; ++i) ++start[seg_[i].ay / kSub + 1];
    for (int r = 1; r <= height_; ++r) start[r] = uint16_t(start[r] + start[r - 1]);
    for (int i = 0; i < count_; ++i) order_[start[seg_[i].ay / kSub]++] = uint16_t(i);
    last_y_ = -2;
}

void Display::draw(const Seg& s, int y, uint16_t* out) const
{
    int xa, xb;
    if (s.ay == s.by) {
        xa = s.ax;
        xb = s.bx;
        if (xa == xb) xb += kSub;               // a dot: 2 pixels wide
    } else {
        // Where the line enters and leaves this row.
        const int y0 = std::max<int>(s.ay, y * kSub);
        const int y1 = std::min<int>(s.by, y * kSub + kSub - 1);
        xa = s.ax + int((int64_t(s.slope) * (y0 - s.ay)) >> 16);
        xb = s.ax + int((int64_t(s.slope) * (y1 - s.ay)) >> 16);
    }
    if (xa > xb) std::swap(xa, xb);
    const int p0 = std::max(xa / kSub, 0), p1 = std::min(xb / kSub, width_ - 1);
    for (int x = p0; x <= p1; ++x) out[x] = s.color;
}

void Display::render_line(int y, uint16_t* out)
{
    for (int x = 0; x < width_; ++x) out[x] = 0;
    if (y < 0 || y >= height_) return;
    if (y != last_y_ + 1) {
        // Not the next row: find the active segments from scratch.
        active_n_ = 0;
        next_ = 0;
        while (next_ < count_ && seg_[order_[next_]].ay / kSub < y) {
            if (seg_[order_[next_]].last_row >= y) active_[active_n_++] = order_[next_];
            ++next_;
        }
        std::sort(active_, active_ + active_n_);
    } else {
        // Drop the ones that ended above this row.
        int n = 0;
        for (int i = 0; i < active_n_; ++i)
            if (seg_[active_[i]].last_row >= y) active_[n++] = active_[i];
        active_n_ = n;
    }
    // New ones join in the order the lines were added (later lines on top).
    while (next_ < count_ && seg_[order_[next_]].ay / kSub == y) {
        const uint16_t v = order_[next_++];
        int i = active_n_++;
        for (; i > 0 && active_[i - 1] > v; --i) active_[i] = active_[i - 1];
        active_[i] = v;
    }
    last_y_ = y;
    for (int i = 0; i < active_n_; ++i) draw(seg_[active_[i]], y, out);
}

} // namespace vector_display
