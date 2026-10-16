// Star Wars matrix processor, divider, LFSR - see starwars_mathbox.h.
#include "starwars_mathbox.h"

namespace starwars_math {

void MatrixProcessor::init(const uint8_t* proms, uint8_t* ram)
{
    proms_ = proms;
    ram_ = ram;
    reset();
}

void MatrixProcessor::reset()
{
    a_ = b_ = c_ = 0;
    acc_ = 0;
    bic_ = 0;
}

int MatrixProcessor::run(uint8_t start)
{
    int mpa = start << 2;
    int clocks = 0;
    // A program that never halts would hang the board; stop after a page's
    // worth of loops (the counter wraps inside its page).
    for (int n = 0; n < 4096; ++n) {
        const uint16_t w = word(mpa);
        const uint8_t strobe = uint8_t(w >> 8);
        const int ma = (w & 0x80) ? (w & 0x7f) : ((w & 3) | ((bic_ & 0x1ff) << 2));
        uint8_t* p = ram_ + (ma << 1);
        const int16_t word = int16_t((p[0] << 8) | p[1]);
        clocks += 5;
        ++steps_;
        if (strobe & kClearAcc) acc_ = 0;
        if (strobe & kLoadAcc) acc_ = int32_t(uint32_t(uint16_t(word)) << 16);
        if (strobe & kStoreAcc) {
            p[0] = uint8_t(uint32_t(acc_) >> 24);
            p[1] = uint8_t(uint32_t(acc_) >> 16);
        }
        if (strobe & kIncBic) bic_ = (bic_ + 1) & 0x1ff;
        if (strobe & kLdc) {
            c_ = word;
            // ACC += (A - B) x C x 4, in 32 bits as the serial adder wraps.
            const int64_t prod = int64_t(int32_t(a_) - b_) * c_ * 4;
            acc_ = int32_t(uint32_t(acc_) + uint32_t(uint64_t(prod)));
            a_ = a_ < 0 ? -1 : 0;
            b_ = b_ < 0 ? -1 : 0;
            clocks += 33;
        }
        if (strobe & kLdb) b_ = word;
        if (strobe & kLda) a_ = word;
        if (strobe & kHalt) break;
        mpa = (mpa & 0x300) | ((mpa + 1) & 0xff);
    }
    return clocks;
}

void Divider::write(int reg, uint8_t v)
{
    switch (reg & 7) {
    case 4:
        divisor_ = uint16_t((divisor_ & 0x00ff) | (v << 8));
        dvd_shift_ = dividend_;
        quotient_ = 0;
        break;
    case 5:
        divisor_ = uint16_t((divisor_ & 0xff00) | v);
        for (int i = 1; i < 16; ++i) {
            quotient_ = uint16_t(quotient_ << 1);
            const uint32_t diff = uint32_t(dvd_shift_) + uint16_t(~divisor_) + 1;
            if (diff & 0x10000) {
                quotient_ |= 1;
                dvd_shift_ = uint16_t(diff << 1);
            } else {
                dvd_shift_ = uint16_t(dvd_shift_ << 1);
            }
        }
        break;
    case 6: dividend_ = uint16_t((dividend_ & 0x00ff) | (v << 8)); break;
    case 7: dividend_ = uint16_t((dividend_ & 0xff00) | v); break;
    default: break;
    }
}

// --- the LFSR ------------------------------------------------------------------

namespace {

constexpr uint32_t kMask = 0x7fffff;
constexpr uint32_t kPeriod = kMask;          // 2^23 - 1

constexpr uint32_t step_c(uint32_t s)
{
    const uint32_t fb = (((s >> 22) ^ (s >> 4)) & 1) ^ 1;
    return ((s << 1) | fb) & kMask;
}

// An affine map on 23 bits as 24 columns: column i = the image of bit i
// (linear part), column 23 = the constant (with bit 23 set, so maps
// compose by the same rule).
struct Map {
    uint32_t col[24];
};

// s: a state with bit 23 set, or (composing) a column as it stands.
constexpr uint32_t apply(const Map& m, uint32_t s)
{
    uint32_t r = 0;
    for (int i = 0; i < 24; ++i)
        if (s & (1u << i)) r ^= m.col[i];
    return r;
}

constexpr Map compose(const Map& a, const Map& b)     // a after b
{
    Map r{};
    for (int i = 0; i < 24; ++i) r.col[i] = apply(a, b.col[i]);
    return r;
}

struct Jumps {
    Map m[23];      // 2^k steps
};

constexpr Jumps make_jumps()
{
    Jumps j{};
    // One step: linear part from step_c(bit) ^ step_c(0); constant step_c(0).
    const uint32_t c = step_c(0);
    for (int i = 0; i < 23; ++i) j.m[0].col[i] = step_c(1u << i) ^ c;
    j.m[0].col[23] = c | (1u << 23);
    for (int k = 1; k < 23; ++k) j.m[k] = compose(j.m[k - 1], j.m[k - 1]);
    return j;
}

constexpr Jumps kJumps = make_jumps();

} // namespace

uint32_t lfsr_step(uint32_t state) { return step_c(state & kMask); }

uint32_t lfsr_advance(uint32_t state, uint64_t ticks)
{
    uint32_t n = uint32_t(ticks % kPeriod);
    state &= kMask;
    for (int k = 0; n; ++k, n >>= 1)
        if (n & 1) state = apply(kJumps.m[k], state | 1u << 23) & kMask;
    return state;
}

} // namespace starwars_math
