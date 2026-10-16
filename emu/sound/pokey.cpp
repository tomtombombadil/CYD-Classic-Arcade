// Atari POKEY - see pokey.h.
#include "pokey.h"

namespace pokey {

namespace {

constexpr uint32_t kN4 = 15, kN5 = 31, kN9 = 511, kN17 = 131071;

// The 4- and 5-bit counters shift left, taking in the inverse of the XOR
// of bits 2 and (n - 1); the noise bit is the one just taken in.
constexpr uint32_t short_poly(int size)
{
    const uint32_t mask = (1u << size) - 1;
    uint32_t s = 0, bits = 0;
    for (uint32_t i = 0; i < mask; ++i) {
        s = ((s << 1) | (~((s >> 2) ^ (s >> (size - 1))) & 1)) & mask;
        bits |= (s & 1) << i;
    }
    return bits;
}
constexpr uint32_t kPoly4 = short_poly(4);
constexpr uint32_t kPoly5 = short_poly(5);

// The 9-bit counter shifts right, taking bit 0 XOR bit 5 into bit 8:
// bit 0 after each step (bit k at step p is bit 0 at step p + k).
struct Poly9 {
    uint8_t b[(kN9 + 7) / 8] = {};
    constexpr Poly9()
    {
        uint32_t s = 0x1ff;
        for (uint32_t p = 0; p < kN9; ++p) {
            s = (s >> 1) | (((s ^ (s >> 5)) & 1) << 8);
            b[p >> 3] = uint8_t(b[p >> 3] | ((s & 1) << (p & 7)));
        }
    }
};
constexpr Poly9 kPoly9{};

// The 17-bit counter shifts right; bit 0 goes round to bit 16 and bit 7
// takes bit 8 XOR bit 13. Kept: bit 8 after each step (16 KB, flash).
// Bits 8-16 only shift, so bit 8 + j at step p is bit 8 at step p + j.
struct Poly17 {
    uint8_t b[(kN17 + 7) / 8] = {};
    constexpr Poly17()
    {
        uint32_t s = 0x1ffff;
        for (uint32_t p = 0; p < kN17; ++p) {
            const uint32_t b7 = ((s >> 8) ^ (s >> 13)) & 1;
            const uint32_t b16 = s & 1;
            s = ((s >> 1) & ~0x80u) | (b7 << 7) | (b16 << 16);
            b[p >> 3] = uint8_t(b[p >> 3] | (((s >> 8) & 1) << (p & 7)));
        }
    }
};
constexpr Poly17 kPoly17{};

inline int bit17(uint32_t p) { return (kPoly17.b[p >> 3] >> (p & 7)) & 1; }
inline int bit9(uint32_t p) { return (kPoly9.b[p >> 3] >> (p & 7)) & 1; }

// AUDC bits.
constexpr uint8_t kNotPoly5 = 0x80, kPoly4Sel = 0x40, kPure = 0x20, kVolOnly = 0x10;
// AUDCTL bits.
constexpr uint8_t kPoly9Sel = 0x80, kCh1Fast = 0x40, kCh3Fast = 0x20, kJoin12 = 0x10, kJoin34 = 0x08,
                  kFilter1 = 0x04, kFilter2 = 0x02, kClock15k = 0x01;

} // namespace

uint32_t poly4_bits() { return kPoly4; }
uint32_t poly5_bits() { return kPoly5; }
int poly17_bit8(uint32_t p) { return bit17(p % kN17); }

uint8_t random17(uint32_t p)
{
    uint8_t v = 0;
    for (int j = 0; j < 8; ++j) v = uint8_t(v | (bit17((p + j) % kN17) << j));
    return v;
}

uint8_t random9(uint32_t p)
{
    uint8_t v = 0;
    for (int j = 0; j < 8; ++j) v = uint8_t(v | (bit9((p + j) % kN9) << j));
    return v;
}

void Pokey::init(uint32_t clock, uint32_t sample_rate)
{
    clock_ = clock;
    step_ = uint32_t((uint64_t(clock) << 16) / sample_rate);
    reset();
}

void Pokey::reset()
{
    for (Channel& c : ch_) c = Channel{};
    for (int i = 0; i < 2; ++i) ch_[i].filter = 1;
    audctl_ = 0;
    skctl_ = 0;
    in_reset_ = true;
    frac_ = 0;
    p4_ = p5_ = p9_ = p17_ = 0;
    t_poly_ = t_;
    update_periods();
    active_ = 0;
}

void Pokey::update_periods()
{
    const uint32_t base = (audctl_ & kClock15k) ? 114 : 28;
    const uint32_t f1 = ch_[0].audf, f2 = ch_[1].audf, f3 = ch_[2].audf, f4 = ch_[3].audf;
    // A joined pair: the low channel's counter wraps (256 counts); the high
    // one underflows when both reach zero. The full clock adds the
    // counters' borrow delay (4 clocks, 7 joined).
    if (audctl_ & kJoin12) {
        const uint32_t v = f2 << 8 | f1;
        ch_[0].period = (audctl_ & kCh1Fast) ? 256 : 256 * base;
        ch_[1].period = (audctl_ & kCh1Fast) ? v + 7 : (v + 1) * base;
    } else {
        ch_[0].period = (audctl_ & kCh1Fast) ? f1 + 4 : (f1 + 1) * base;
        ch_[1].period = (f2 + 1) * base;
    }
    if (audctl_ & kJoin34) {
        const uint32_t v = f4 << 8 | f3;
        ch_[2].period = (audctl_ & kCh3Fast) ? 256 : 256 * base;
        ch_[3].period = (audctl_ & kCh3Fast) ? v + 7 : (v + 1) * base;
    } else {
        ch_[2].period = (audctl_ & kCh3Fast) ? f3 + 4 : (f3 + 1) * base;
        ch_[3].period = (f4 + 1) * base;
    }
}

void Pokey::update_active()
{
    uint8_t a = 0;
    for (int c = 0; c < 4; ++c)
        if ((ch_[c].audc & 15) && !(ch_[c].audc & kVolOnly)) a |= uint8_t(1u << c);
    if ((a & 1) && (audctl_ & kFilter1)) a |= 4;
    if ((a & 2) && (audctl_ & kFilter2)) a |= 8;
    // A channel coming back: its divider goes on from now (the phase lost
    // while it was silent doesn't matter).
    for (int c = 0; c < 4; ++c)
        if ((a & ~active_ & (1u << c)) && int32_t(ch_[c].next - t_) <= 0) ch_[c].next = t_ + ch_[c].period;
    active_ = a;
}

void Pokey::write(uint8_t reg, uint8_t v, uint32_t clock)
{
    switch (reg & 15) {
    case kAudf1: case kAudf2: case kAudf3: case kAudf4:
        // A new divider takes effect at the channel's next underflow.
        ch_[(reg & 15) >> 1].audf = v;
        update_periods();
        break;
    case kAudc1: case kAudc2: case kAudc3: case kAudc4:
        ch_[(reg & 15) >> 1].audc = v;
        update_active();
        break;
    case kAudctl:
        audctl_ = v;
        update_periods();
        if (!(v & kFilter1)) ch_[0].filter = 1;
        if (!(v & kFilter2)) ch_[1].filter = 1;
        update_active();
        break;
    case kStimer:
        // Restarts the dividers.
        for (Channel& c : ch_) c.next = t_ + c.period;
        break;
    case kSkctl: {
        const bool reset = (v & 3) == 0;
        if (reset && !in_reset_) {
            in_reset_ = true;
        } else if (!reset && in_reset_) {
            // Out of reset: the polynomial counters start from their
            // first step, the dividers count again.
            in_reset_ = false;
            random_origin_ = clock;
            t_poly_ = t_;
            p4_ = p5_ = p9_ = p17_ = 0;
            for (Channel& c : ch_) c.next = t_ + c.period;
        }
        skctl_ = v;
        break;
    }
    default:
        break;
    }
}

uint8_t Pokey::read(uint8_t reg, uint32_t clock)
{
    switch (reg & 15) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        return pot_[reg & 7];
    case kAllpot:
        return allpot_;
    case kRandom: {
        // RANDOM shows the counter's bits as they are this clock (held at
        // the first step while in reset).
        const uint32_t p = in_reset_ ? 0 : clock - random_origin_;
        return (audctl_ & kPoly9Sel) ? random9(p % kN9) : random17(p % kN17);
    }
    default:
        return 0xff;    // no keyboard, serial or timer interrupts
    }
}

void Pokey::advance_polys(uint32_t to)
{
    const uint32_t d = to - t_poly_;
    if (!d) return;
    t_poly_ = to;
    p4_ = (p4_ + d) % kN4;
    p5_ = (p5_ + d) % kN5;
    p9_ = (p9_ + d) % kN9;
    p17_ = (p17_ + d) % kN17;
}

void Pokey::step_channel(int c, uint32_t at)
{
    Channel& ch = ch_[c];
    ch.next += ch.period;
    ++ch.count;
    advance_polys(at);
    if ((ch.audc & kNotPoly5) || ((kPoly5 >> p5_) & 1)) {
        if (ch.audc & kPure)           ch.out ^= 1;
        else if (ch.audc & kPoly4Sel)  ch.out = uint8_t((kPoly4 >> p4_) & 1);
        else if (audctl_ & kPoly9Sel)  ch.out = uint8_t(bit9(p9_));
        else                           ch.out = uint8_t(bit17(p17_));
    }
    // Channel 3 clocks channel 1's high-pass flip-flop, 4 clocks 2's.
    if (c == 2 && (audctl_ & kFilter1)) ch_[0].filter = ch_[0].out;
    if (c == 3 && (audctl_ & kFilter2)) ch_[1].filter = ch_[1].out;
}

int Pokey::level() const
{
    int sum = 0;
    for (int c = 0; c < 4; ++c) {
        const Channel& ch = ch_[c];
        const int vol = ch.audc & 15;
        if (!vol) continue;
        // Without the filter, the flip-flop stays 1 (the output inverted).
        const int bit = c < 2 ? (ch.out ^ ch.filter) : (ch.out ^ 1);
        if ((ch.audc & kVolOnly) || bit) sum += vol;
    }
    return sum * kLevelScale;
}

void Pokey::render(int16_t* out, int n, bool add)
{
    if (!active_ || in_reset_) {
        // No underflow matters: the level is the same all along (what the
        // loop below would give, without looking for events).
        const int lv = level();
        for (int i = 0; i < n; ++i) {
            frac_ += step_;
            t_ += frac_ >> 16;
            frac_ &= 0xffff;
            out[i] = int16_t(add ? out[i] + lv : lv);
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        frac_ += step_;
        const uint32_t clocks = frac_ >> 16;
        frac_ &= 0xffff;
        const uint32_t end = t_ + clocks;
        int32_t acc = 0;
        int lv = level();
        if (in_reset_) {
            acc = lv * int32_t(clocks);
            t_ = end;
        } else {
            for (;;) {
                // The next underflow before the end of this sample.
                int next_c = -1;
                uint32_t best = end - t_;
                for (int c = 0; c < 4; ++c) {
                    if (!(active_ >> c & 1)) continue;
                    const uint32_t d = ch_[c].next - t_;
                    if (d < best) { best = d; next_c = c; }
                }
                acc += lv * int32_t(best);
                t_ += best;
                if (next_c < 0) break;
                step_channel(next_c, t_);
                lv = level();
            }
        }
        const int v = clocks ? int(acc / int32_t(clocks)) : lv;
        out[i] = int16_t(add ? out[i] + v : v);
    }
}

} // namespace pokey
