// Atari DVG - see atari_dvg.h.
#include "atari_dvg.h"

namespace atari_dvg {

namespace {

// A 12-bit counter as a signed position: 0-1023 on the screen; the rest
// is off it (bit 10 set) - counters past 2047 count as below 0.
inline int signed12(int v) { return v >= 2048 ? v - 4096 : v; }

} // namespace

void Dvg::init(read_fn rd, line_fn line, void* ctx, uint16_t base)
{
    rd_ = rd;
    line_ = line;
    ctx_ = ctx;
    base_ = base;
    reset();
}

void Dvg::reset()
{
    pc_ = 0;
    sp_ = 0;
    x_ = y_ = 0;
    scale_ = 0;
}

uint16_t Dvg::word()
{
    const uint16_t a = uint16_t(base_ + (pc_ << 1));
    pc_ = (pc_ + 1) & 0xfff;
    return uint16_t(rd_(ctx_, a) | (rd_(ctx_, uint16_t(a + 1)) << 8));
}

// dx / dy: 10-bit magnitude with the sign in bit 10. Returns the clocks.
int Dvg::vector(int dx, int dy, int scale, int intensity)
{
    const int clocks = (2 << scale) & 0x7ff;            // 0 from scale 10 on
    int mx = ((dx & 0x3ff) * clocks) >> 10;
    int my = ((dy & 0x3ff) * clocks) >> 10;
    if (dx & 0x400) mx = -mx;
    if (dy & 0x400) my = -my;
    const int x0 = signed12(x_), y0 = signed12(y_);
    const int x1 = x0 + mx, y1 = y0 + my;
    if (intensity) {
        // The part on the screen (the hardware blanks the beam while a
        // counter has bit 10 set).
        if (!((x0 < 0 && x1 < 0) || (x0 > 1023 && x1 > 1023) || (y0 < 0 && y1 < 0) || (y0 > 1023 && y1 > 1023)))
            line_(ctx_, x0, y0, x1, y1, intensity);
    }
    x_ = x1 & 0xfff;
    y_ = y1 & 0xfff;
    return clocks;
}

int Dvg::run(int max_ops)
{
    pc_ = 0;
    int clocks = 0;
    for (ops_ = 0; ops_ < max_ops; ++ops_) {
        const uint16_t w0 = word();
        const int op = w0 >> 12;
        clocks += 2;
        if (op <= 9) {
            const uint16_t w1 = word();
            clocks += 2 + vector(w1 & 0x7ff, w0 & 0x7ff, (scale_ + op) & 15, w1 >> 12);
            continue;
        }
        switch (op) {
        case 0xa: {                                     // LABS
            const uint16_t w1 = word();
            clocks += 2;
            y_ = w0 & 0xfff;
            x_ = w1 & 0xfff;
            scale_ = w1 >> 12;
            break;
        }
        case 0xb:                                       // HALT
            ++ops_;
            return clocks;
        case 0xc:                                       // JSRL
            stack_[sp_ & 3] = pc_;
            sp_ = (sp_ + 1) & 15;
            pc_ = w0 & 0xfff;
            break;
        case 0xd:                                       // RTSL
            sp_ = (sp_ - 1) & 15;
            pc_ = stack_[sp_ & 3];
            break;
        case 0xe:                                       // JMPL
            pc_ = w0 & 0xfff;
            break;
        default: {                                      // SVEC
            const int hi = w0 >> 8, lo = w0 & 0xff;
            const int s = 2 + ((hi >> 3) & 1) + ((lo >> 2) & 2);
            clocks += vector((lo & 7) << 8, (hi & 7) << 8, (scale_ + s) & 15, lo >> 4);
            break;
        }
        }
    }
    return clocks;
}

} // namespace atari_dvg
