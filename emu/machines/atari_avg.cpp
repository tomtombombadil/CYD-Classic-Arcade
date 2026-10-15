// Atari AVG (Tempest) - see atari_avg.h.
#include "atari_avg.h"

namespace atari_avg {

void Avg::init(read_fn rd, line_fn line, void* ctx, uint16_t base, int32_t cx, int32_t cy)
{
    rd_ = rd;
    line_ = line;
    ctx_ = ctx;
    base_ = base;
    cx_ = cx;
    cy_ = cy;
    x_ = cx;
    y_ = cy;
    reset();
}

void Avg::reset()
{
    scale_ = bin_scale_ = 0;
    color_ = 0;
    halted_ = true;
}

uint16_t Avg::word()
{
    const uint16_t a = uint16_t(base_ + (pc_ << 1));
    pc_ = (pc_ + 1) & 0x1fff;
    return uint16_t(rd_(ctx_, a) | (rd_(ctx_, uint16_t(a + 1)) << 8));
}

namespace {

// How many left shifts until bit 11 differs from bit 12 (the sign) of a
// 13-bit two's complement value: 16 (the hardware's limit) for 0, 12 for
// -1 (zeros come in from the right).
inline int shifts_to_normalise(int v)
{
    const bool neg = (v & 0x1000) != 0;
    const int s = (v ^ (neg ? 0xfff : 0)) & 0xfff;
    if (!s) return neg ? 12 : 16;
    return 11 - (31 - __builtin_clz(unsigned(s)));
}

} // namespace

int Avg::normalise(int& dvx, int& dvy, int bin_scale, bool svec)
{
    // Shift both left while their top two bits agree (at most 16 times),
    // each shift halving the time; the binary scale halves it again per
    // step. The time counter fills from the top, so after k halvings
    // 2^15 >> k clocks are left (2^8 >> k for SVEC; at least 1).
    int n = shifts_to_normalise(dvx);
    const int ny = shifts_to_normalise(dvy);
    if (ny < n) n = ny;
    dvx = (dvx & 0x1000) | ((dvx << n) & 0x1fff);
    dvy = (dvy & 0x1000) | ((dvy << n) & 0x1fff);
    const int k = n + bin_scale;
    return svec ? 0x100 >> (k < 8 ? k : 8) : 0x8000 >> (k < 15 ? k : 15);
}

// dvx / dvy: 13-bit two's complement. Returns the time in master clocks.
int Avg::vector(int dvx, int dvy, int z, bool svec)
{
    const int clocks = normalise(dvx, dvy, bin_scale_, svec);

    // The DACs take bits 3-12.
    const int vx = (((dvx >> 3) & 0x3ff) ^ 0x200) - 0x200;
    const int vy = (((dvy >> 3) & 0x3ff) ^ 0x200) - 0x200;
    const int64_t rate = int64_t(clocks) * (scale_ ^ 0xff);
    const int32_t x1 = x_ + int32_t((vx * rate) >> 4);
    const int32_t y1 = y_ - int32_t((vy * rate) >> 4);
    const int level = z == 1 ? intensity_ : z * 2;
    if (level) line_(ctx_, x_, y_, x1, y1, color_, level);
    x_ = x1;
    y_ = y1;
    return clocks;
}

int Avg::run(int max_ops)
{
    pc_ = 0;
    sp_ = 0;
    halted_ = false;
    int clocks = 0;                     // master clocks (12 MHz)
    for (ops_ = 0; ops_ < max_ops; ++ops_) {
        const uint16_t w0 = word();
        clocks += 16;                   // ~2 states a word, 8 clocks each
        switch (w0 >> 13) {
        case 0: {                       // VCTR
            const uint16_t w1 = word();
            clocks += 16 + vector(w1 & 0x1fff, w0 & 0x1fff, w1 >> 13, false);
            break;
        }
        case 1:                         // HALT
            halted_ = true;
            ++ops_;
            return clocks / 8;
        case 2:                         // SVEC
            clocks += vector((w0 & 0x1f) << 8, ((w0 >> 8) & 0x1f) << 8, (w0 >> 5) & 7, true);
            break;
        case 3:
            if (w0 & 0x1000) {          // SCAL
                scale_ = w0 & 0xff;
                bin_scale_ = (w0 >> 8) & 7;
            } else if (w0 & 0x800) {    // STAT: colour
                color_ = w0 & 0xf;
            } else {                    // STAT: intensity
                intensity_ = (w0 >> 4) & 0xf;
            }
            break;
        case 4:                         // CNTR
            x_ = cx_;
            y_ = cy_;
            break;
        case 5:                         // JSRL
        case 7: {                       // JMPL
            const uint16_t to = w0 & 0x1fff;
            if (to == 0) {              // back to the start: the picture is done
                ++ops_;
                return clocks / 8;
            }
            if (w0 >> 13 == 5) {
                stack_[sp_ & 3] = pc_;
                sp_ = (sp_ + 1) & 15;
            }
            pc_ = to;
            break;
        }
        case 6:                         // RTSL
            sp_ = (sp_ - 1) & 15;
            pc_ = stack_[sp_ & 3];
            break;
        }
    }
    return clocks / 8;
}

} // namespace atari_avg
