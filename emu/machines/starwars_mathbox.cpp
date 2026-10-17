// Star Wars matrix processor, divider, LFSR - see starwars_mathbox.h.
#include "starwars_mathbox.h"

#include <type_traits>

namespace starwars_math {

void MatrixProcessor::init(const uint8_t* proms, uint8_t* ram)
{
    for (int a = 0; a < 1024; ++a)
        micro_[a] = uint16_t((proms[a] & 15) << 12 | (proms[0x400 + a] & 15) << 8 | (proms[0x800 + a] & 15) << 4 |
                             (proms[0xc00 + a] & 15));
    ram_ = ram;
    reset();
}

void MatrixProcessor::reset()
{
    a_ = b_ = c_ = 0;
    acc_ = 0;
    bic_ = 0;
}

namespace {

// One microinstruction's strobes, in the order the hardware's results come
// out (the RAM word is read first, then: clear, load, store, BIC + 1, the
// multiply, LDB, LDA). S is a compile-time constant for the strobe
// combinations the microprogram uses, so each is straight code.
struct Regs {
    int16_t  a, b, c;
    int32_t  acc;
    uint16_t bic;
};

template <class S> inline bool step(S strobe, uint16_t w, Regs& r, uint8_t* ram, int& clocks)
{
    const int ma = (w & 0x80) ? (w & 0x7f) : ((w & 3) | ((r.bic & 0x1ff) << 2));
    uint8_t* p = ram + (ma << 1);
    const int16_t word = int16_t((p[0] << 8) | p[1]);
    if (strobe & kClearAcc) r.acc = 0;
    if (strobe & kLoadAcc) r.acc = int32_t(uint32_t(uint16_t(word)) << 16);
    if (strobe & kStoreAcc) {
        p[0] = uint8_t(uint32_t(r.acc) >> 24);
        p[1] = uint8_t(uint32_t(r.acc) >> 16);
    }
    if (strobe & kIncBic) r.bic = (r.bic + 1) & 0x1ff;
    if (strobe & kLdc) {
        r.c = word;
        // ACC += (A - B) x C x 4, in 32 bits as the serial adder wraps (the
        // low 32 bits of the product: unsigned arithmetic gives them).
        const uint32_t prod = uint32_t(int32_t(r.a) - r.b) * uint32_t(int32_t(r.c)) * 4u;
        r.acc = int32_t(uint32_t(r.acc) + prod);
        r.a = r.a < 0 ? -1 : 0;
        r.b = r.b < 0 ? -1 : 0;
        clocks += 33;
    }
    if (strobe & kLdb) r.b = word;
    if (strobe & kLda) r.a = word;
    return (strobe & kHalt) != 0;
}

template <uint8_t V> using Strobe = std::integral_constant<uint8_t, V>;

} // namespace

int MatrixProcessor::run(uint8_t start)
{
    Regs r{a_, b_, c_, acc_, bic_};
    int mpa = start << 2;
    int clocks = 0;
    int n = 0;
    // A program that never halts would hang the board; stop after a page's
    // worth of loops (the counter wraps inside its page).
    while (n < 4096) {
        const uint16_t w = micro_[mpa];
        clocks += 5;
        ++n;
        bool halt;
        // The strobe combinations Star Wars' microprogram has (the rest
        // the same way, decided as they run).
        switch (uint8_t(w >> 8)) {
        case 0x00: halt = false; break;     // nothing: only the time passes
        case 0x20: halt = step(Strobe<0x20>(), w, r, ram_, clocks); break;
        case 0x80: halt = step(Strobe<0x80>(), w, r, ram_, clocks); break;
        case 0x02: halt = step(Strobe<0x02>(), w, r, ram_, clocks); break;
        case 0x40: halt = step(Strobe<0x40>(), w, r, ram_, clocks); break;
        case 0x90: halt = step(Strobe<0x90>(), w, r, ram_, clocks); break;
        case 0x01: halt = step(Strobe<0x01>(), w, r, ram_, clocks); break;
        case 0x06: halt = step(Strobe<0x06>(), w, r, ram_, clocks); break;
        case 0x50: halt = step(Strobe<0x50>(), w, r, ram_, clocks); break;
        case 0x0a: halt = step(Strobe<0x0a>(), w, r, ram_, clocks); break;
        default:   halt = step(uint8_t(w >> 8), w, r, ram_, clocks); break;
        }
        if (halt) break;
        mpa = (mpa & 0x300) | ((mpa + 1) & 0xff);
    }
    a_ = r.a;
    b_ = r.b;
    c_ = r.c;
    acc_ = r.acc;
    bic_ = r.bic;
    steps_ += uint32_t(n);
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
