// main/fb_push.h: blocks written into a landscape frame buffer at every
// rotation, checked pixel by pixel against a model of LovyanGFX 1.2.31's
// own mapping (Panel_FrameBufferBase::drawPixelPreclipped: for internal
// rotation r, y = height - 1 - y when r is 1 or 2, x = width - 1 - x when
// r & 2, then x and y swapped when r & 1; width / height the rotated ones),
// and the rows reported for the cache write-back.
#include "fb_push.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int g_fail = 0;

void check(bool ok, const char* what, int a = 0, int b = 0, int c = 0)
{
    if (ok) return;
    if (++g_fail < 20) std::printf("FAIL %s (%d %d %d)\n", what, a, b, c);
}

// LovyanGFX's mapping, written out from its code
void lgfx_native(int r, int pw, int ph, int x, int y, int& X, int& Y)
{
    const int w = (r & 1) ? ph : pw;   // rotated width / height
    const int h = (r & 1) ? pw : ph;
    if ((1u << r) & 0b10010110) y = h - (y + 1);
    if (r & 2) x = w - (x + 1);
    if (r & 1) {
        const int t = x;
        x = y;
        y = t;
    }
    X = x;
    Y = y;
}

uint32_t s_rng = 12345;
int rnd(int n)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return int((s_rng >> 8) % uint32_t(n));
}

}  // namespace

int main()
{
    // A small panel (the real one is 800x480; the mapping is the same) with
    // a stride wider than the panel, to catch stride mix-ups
    const int pw = 40, ph = 24, stride = 44;
    for (int r = 0; r < 4; ++r) {
        for (int t = 0; t < 400; ++t) {
            const int lw = (r & 1) ? ph : pw, lh = (r & 1) ? pw : ph;   // logical size
            const int w = 1 + rnd(lw), h = 1 + rnd(lh);
            const int x = rnd(lw - w + 1), y = rnd(lh - h + 1);
            std::vector<uint16_t> fb(size_t(stride) * ph, 0xDEAD), want = fb;
            std::vector<uint16_t> px(size_t(w) * h);
            for (size_t i = 0; i < px.size(); ++i) px[i] = uint16_t(rnd(65535) + 1);
            int lo = 1 << 30, hi = -1;
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i) {
                    int X, Y;
                    lgfx_native(r, pw, ph, x + i, y + j, X, Y);
                    check(X >= 0 && X < pw && Y >= 0 && Y < ph, "model in range", r, X, Y);
                    want[size_t(Y) * stride + X] = px[size_t(j) * w + i];
                    if (Y < lo) lo = Y;
                    if (Y > hi) hi = Y;
                    int X2, Y2;
                    fb_push::Frame probe;
                    probe.pw = pw;
                    probe.ph = ph;
                    probe.rot = r;
                    fb_push::to_native(probe, x + i, y + j, X2, Y2);
                    check(X2 == X && Y2 == Y, "to_native", r, x + i, y + j);
                }
            fb_push::Frame f;
            f.fb = fb.data();
            f.stride = stride;
            f.pw = pw;
            f.ph = ph;
            f.rot = r;
            fb_push::write(f, x, y, w, h, px.data());
            check(fb == want, "pixels", r, w, h);
            check(f.row0 == lo && f.row1 == hi, "rows", r, f.row0, f.row1);
        }
    }
    std::printf("fb_push: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
