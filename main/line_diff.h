// Which parts of a game line changed since it was last sent to the panel.
// Plain C++ (no ESP-IDF), so the PC can check it against real frames.
//
// A line is cut into up to 8 segments (an even number of game pixels each:
// 28 for 224-wide games), each with a 32-bit hash of what the panel shows
// there; a segment is sent when its hash changed. v0.14 hashed whole lines
// and sent every changed line in full: Galaga's scrolling stars change ~140
// of its 288 lines a frame but only ~10 % of the pixels (Tom: Galaga
// dropped many frames in busy waves). 16-bit segment hashes were tried and
// missed a change within 10 frames (arcade_host --panel): game pixels are
// too regular for short hashes - and FNV fed a word at a time only carries
// bits upward, so each word is rotated in.
#pragma once

#include <cstdint>
#include <cstring>

namespace line_diff {

constexpr int kSegs = 8;

struct Layout {
    int width = 0;   // game pixels a line
    int seg_w = 0;   // pixels a segment (even, so hashing goes by 32-bit words)
    int segs = 0;    // segments a line (<= kSegs)
};

inline Layout layout(int width)
{
    Layout l;
    l.width = width;
    l.seg_w = ((width + kSegs - 1) / kSegs + 1) & ~1;
    l.segs = (width + l.seg_w - 1) / l.seg_w;
    return l;
}

inline uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

// One segment's hash: `w` pixels from `p`.
inline uint32_t seg_hash(const uint16_t* p, int w)
{
    uint32_t h = 2166136261u;
    int x = 0;
    for (; x + 1 < w; x += 2) h = rotl(h ^ (uint32_t(p[x]) | uint32_t(p[x + 1]) << 16), 13) * 0x9E3779B1u;
    if (x < w) h = rotl(h ^ p[x], 13) * 0x9E3779B1u;
    return h;
}

inline uint32_t seg_mix(uint32_t h)
{
    h ^= h >> 15;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return h | 1;   // never 0, the "never sent" mark
}

// Hashes `line` (width pixels), compares with the stored hashes (`seg`:
// layout.segs entries; 0 = never sent) and stores the new ones. Returns a
// bit per segment to send; 0 = unchanged.
// Two segments are hashed side by side (each hash is a chain of multiplies,
// so two chains keep the multiplier busy), a pixel pair as one 32-bit load
// when the line is word-aligned (pixel x | pixel x + 1 << 16, little-endian)
// - the same hashes as one pixel at a time (seg_hash).
inline uint32_t diff(const Layout& l, const uint16_t* line, uint32_t* seg)
{
    uint32_t mask = 0;
    int s = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    typedef uint32_t Word __attribute__((may_alias, aligned(4)));
    if (!(reinterpret_cast<uintptr_t>(line) & 3) && !(l.seg_w & 1)) {
        const int pairs = l.seg_w >> 1;
        for (; s + 1 < l.segs && (s + 2) * l.seg_w <= l.width; s += 2) {
            const Word* p = reinterpret_cast<const Word*>(line + s * l.seg_w);
            const Word* q = p + pairs;
            uint32_t h0 = 2166136261u, h1 = 2166136261u;
            for (int i = 0; i < pairs; ++i) {
                h0 = rotl(h0 ^ p[i], 13) * 0x9E3779B1u;
                h1 = rotl(h1 ^ q[i], 13) * 0x9E3779B1u;
            }
            h0 = seg_mix(h0);
            h1 = seg_mix(h1);
            if (h0 != seg[s]) {
                seg[s] = h0;
                mask |= 1u << s;
            }
            if (h1 != seg[s + 1]) {
                seg[s + 1] = h1;
                mask |= 2u << s;
            }
        }
    }
#endif
    for (; s < l.segs; ++s) {
        const int x0 = s * l.seg_w;
        const int x1 = x0 + l.seg_w < l.width ? x0 + l.seg_w : l.width;
        const uint32_t h = seg_mix(seg_hash(line + x0, x1 - x0));
        if (h != seg[s]) {
            seg[s] = h;
            mask |= 1u << s;
        }
    }
    return mask;
}

// The changed segments as runs [first, last + 1), runs one unchanged
// segment apart joined (a window costs about as much as a segment's pixels).
// Returns the number of runs (<= kSegs / 2 + 1).
inline int runs(uint32_t mask, int segs, int (*out)[2])
{
    int n = 0;
    int s = 0;
    while (s < segs) {
        if (!(mask >> s & 1)) {
            ++s;
            continue;
        }
        int e = s + 1;
        while (e < segs && ((mask >> e & 1) || (e + 1 < segs && (mask >> (e + 1) & 1)))) ++e;
        out[n][0] = s;
        out[n][1] = e;
        ++n;
        s = e;
    }
    return n;
}

// One picture's drawing, shared by the firmware (main/game_runner.cpp) and
// the PC check (arcade_host --panel): renders each game line once, sends
// only its changed segments, scaled to the panel, and packs lines whose
// changed part is the same columns into strips of up to `strip_rows` rows.
// Game line y fills output rows first_row(y) .. first_row(y + 1) - 1
// (output row r shows game line r * height / out_h).
struct Drawer {
    Layout segs;
    int height = 0, out_w = 0, out_h = 0;
    bool scaled = false;
    const uint16_t* x_map = nullptr;   // out_w entries: the game pixel each column shows
    int seg_x[kSegs + 1] = {};         // where each segment starts on the panel
    uint16_t* src = nullptr;           // one game line
    uint16_t* strip = nullptr;         // strip_rows * out_w pixels
    int strip_rows = 0;
    uint32_t* seg_hash = nullptr;      // height * segs.segs entries
    int resume_y = 0;                  // where an interrupted picture goes on

    // x_map must be filled before; clears the hashes (0 = never sent)
    void setup()
    {
        for (int sg = 0, x = 0; sg <= segs.segs; ++sg) {
            const int gx = sg * segs.seg_w < segs.width ? sg * segs.seg_w : segs.width;
            while (x < out_w && x_map[x] < gx) ++x;
            seg_x[sg] = x;
        }
        for (int i = 0; i < height * segs.segs; ++i) seg_hash[i] = 0;
        resume_y = 0;
    }

    int first_row(int y) const { return (y * out_h + height - 1) / height; }

    // render(y, line) draws game line y; push(x, y, w, h, pixels) sends a
    // block to the panel (panel coordinates of the picture); stop() is asked
    // before each line but the first - true ends the picture there and the
    // next one goes on from that line (lines not reached keep their old
    // hashes, so they still go). Returns true when the picture was finished.
    template <class Render, class Push, class Stop>
    bool frame(Render render, Push push, Stop stop)
    {
        int rows = 0, start = 0, sx0 = 0, sx1 = 0;
        auto flush = [&]() {
            if (!rows) return;
            push(sx0, start, sx1 - sx0, rows, strip);
            rows = 0;
        };
        auto put_rows = [&](int r0, int r1, int a, int b) {
            const int w = b - a;
            for (int r = r0; r < r1; ++r) {
                if (rows && (a != sx0 || b != sx1 || r != start + rows)) flush();
                if (!rows) {
                    start = r;
                    sx0 = a;
                    sx1 = b;
                }
                uint16_t* row = strip + rows * w;
                if (r > r0 && rows > 0) {
                    // the line's next row (scaled up): the same pixels
                    std::memcpy(row, row - w, size_t(w) * 2);
                } else if (scaled) {
                    const uint16_t* m = x_map + a;
                    int x = 0;
                    for (; x + 4 <= w; x += 4) {
                        const uint16_t p0 = src[m[x]], p1 = src[m[x + 1]], p2 = src[m[x + 2]], p3 = src[m[x + 3]];
                        row[x] = p0;
                        row[x + 1] = p1;
                        row[x + 2] = p2;
                        row[x + 3] = p3;
                    }
                    for (; x < w; ++x) row[x] = src[m[x]];
                } else {
                    std::memcpy(row, src + a, size_t(w) * 2);
                }
                if (++rows == strip_rows) flush();
            }
        };
        bool complete = true;
        for (int k = 0; k < height; ++k) {
            const int y = (resume_y + k) % height;
            if (k > 0 && stop()) {
                resume_y = y;
                complete = false;
                break;
            }
            const int r0 = first_row(y), r1 = first_row(y + 1);
            if (r0 >= r1) continue;
            render(y, src);
            const uint32_t mask = diff(segs, src, seg_hash + y * segs.segs);
            if (!mask) continue;
            int run[kSegs][2];
            const int n = runs(mask, segs.segs, run);
            for (int i = 0; i < n; ++i) put_rows(r0, r1, seg_x[run[i][0]], seg_x[run[i][1]]);
        }
        if (complete) resume_y = 0;
        flush();
        return complete;
    }
};

}  // namespace line_diff
