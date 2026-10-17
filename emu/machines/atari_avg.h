// Atari's Analog Vector Generator as in Tempest, written for this project
// (MIT). The AVG reads a display list of 16-bit words (low byte first) and
// moves the beam by charging integrators: a vector's X / Y values feed DACs
// for a time set by the scale. Its instructions, as documented by Atari
// and MAME's driver (read for facts only) - the state machine PROM isn't
// needed (op = the top 3 bits of the first word):
//   0 VCTR  y (13 bits, two's complement), then z << 13 | x (13 bits);
//           z = intensity: 0 off, 1 the STAT intensity, else z x 2
//   1 HALT
//   2 SVEC  one word: y (5 bits) << 8, z << 5 | x (5 bits); twice the
//           long vector's unit
//   3 STAT  (bit 12 clear) Tempest: bit 11 set = colour (bits 0-3, an
//           index into the colour RAM), else intensity (bits 4-7)
//     SCAL  (bit 12 set) binary scale (bits 8-10), linear scale (bits 0-7)
//   4 CNTR  beam to the centre
//   5 JSRL  word address (13 bits); a 4-deep stack
//   6 RTSL
//   7 JMPL  word address
// Deflection: the hardware shifts X and Y left together until either's
// top two bits differ (halving the drawing time each shift), then draws
// for that time (2^15 clocks less the shifts, 2^8 for SVEC) halved again
// per binary scale step, with bits 3-12 of each on the DACs and the linear
// scale reducing the rate: move = value x time x (255 - linear) / 16 in
// 1/65536 of a screen unit. Done here the same way, so rounding matches.
//
// Star Wars' AVG (Variant::StarWars) differs in three ways: its 6809 is
// big-endian, so a word's high byte comes first; STAT sets the colour (bits
// 8-11: 10 red, 9 green, 8 blue) and an 8-bit intensity (bits 0-7) at once;
// and a vector's brightness is that intensity through a resistor DAC driven
// by its 3-bit z (12k / 24k / 47k for z's bits 2 / 1 / 0) - z = 0 is a move.
#pragma once

#include <cstdint>

namespace atari_avg {

using read_fn = uint8_t (*)(void* ctx, uint16_t addr);
// A line in beam coordinates (16.16 screen units, X right, Y down, the
// centre at (cx, cy)), with the colour index and intensity (Tempest: 1-15;
// Star Wars: 1-255). A dot when the ends meet.
using line_fn = void (*)(void* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int intensity);

enum class Variant : uint8_t { Tempest, StarWars };

class Avg {
public:
    // `base`: CPU address of word 0 (Tempest 2000, Star Wars 0000); the
    // centre in 16.16.
    void init(read_fn rd, line_fn line, void* ctx, uint16_t base, int32_t cx, int32_t cy,
              Variant variant = Variant::Tempest);
    void reset();       // the VG reset strobe: scales and colour cleared
    // Optional direct reads: the 4 KB page (0-3) of the AVG's 16 KB window
    // (from `base`) at `p`; unmapped pages go through the read function.
    void map(int page, const uint8_t* p) { page_[page & 3] = p; }

    // Runs the list from word 0 until HALT, a jump back to word 0 (Tempest
    // keeps the AVG in an endless loop: that ends a picture) or `max_ops`
    // instructions. Returns the drawing time in CPU clocks (1.5 MHz).
    int run(int max_ops = 8192);
    // The same, handing each line to sink(x0, y0, x1, y1, color, intensity)
    // (the line function's arguments) instead of calling the line function,
    // so the machine's own code is inlined into the loop. V must be the
    // variant given to init().
    template <Variant V, class Sink> int run_lines(Sink& sink, int max_ops = 8192);
    // True when the last run ended at HALT (else it loops).
    bool halted() const { return halted_; }

    // A vector's normalisation: shifts dvx / dvy (13-bit two's complement)
    // left together as the hardware does and returns the drawing time in
    // master clocks (12 MHz) at this binary scale.
    static int normalise(int& dvx, int& dvy, int bin_scale, bool svec);

    // State (tests).
    int32_t x() const { return x_; }
    int32_t y() const { return y_; }
    int ops() const { return ops_; }
    int color() const { return color_; }
    int intensity() const { return intensity_; }
    int scale() const { return scale_; }
    int bin_scale() const { return bin_scale_; }

private:
    static int shifts_to_normalise(int v);
    void set_stat(uint16_t w0);

    read_fn  rd_ = nullptr;
    const uint8_t* page_[4] = {};
    line_fn  line_ = nullptr;
    void*    ctx_ = nullptr;
    uint16_t base_ = 0;
    uint16_t pc_ = 0;               // word address
    uint16_t stack_[4] = {};
    uint8_t  sp_ = 0;
    Variant  variant_ = Variant::Tempest;
    int32_t  cx_ = 0, cy_ = 0;
    int32_t  x_ = 0, y_ = 0;        // beam, 16.16
    int      color_ = 0, intensity_ = 0;
    uint8_t  level_[8] = {};        // Star Wars: each z's brightness at this STAT
    int      scale_ = 0, bin_scale_ = 0;
    int      ops_ = 0;
    bool     halted_ = true;
};

// --- the display list, inlined into run_lines() -------------------------------------

// How many left shifts until bit 11 differs from bit 12 (the sign) of a
// 13-bit two's complement value: 16 (the hardware's limit) for 0, 12 for
// -1 (zeros come in from the right): the redundant sign bits of the value
// sign-extended to 32 bits, less the 19 added (one instruction, NSA, on the
// ESP32; checked equal to counting them for all 8192 values).
inline int Avg::shifts_to_normalise(int v)
{
    const int32_t sv = int32_t(uint32_t(v) << 19) >> 19;
    return sv == 0 ? 16 : __builtin_clrsb(sv) - 19;
}

inline int Avg::normalise(int& dvx, int& dvy, int bin_scale, bool svec)
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

template <Variant V, class Sink> int Avg::run_lines(Sink& sink, int max_ops)
{
    // The state the loop changes is kept in locals (the compiler can't tell
    // that the sink's stores leave the members alone) and stored at the end.
    uint16_t pc = 0;
    uint8_t  sp = 0;
    int32_t  x = x_, y = y_;
    int      rate_scale = scale_ ^ 0xff, bin = bin_scale_;
    int      clocks = 0;                // master clocks (12 MHz)
    int      ops = 0;
    bool     halted = false;
    auto word = [&]() -> uint16_t {
        const uint16_t off = uint16_t(pc << 1);
        pc = (pc + 1) & 0x1fff;
        if (const uint8_t* p = page_[off >> 12]) {
            p += off & 0xfff;
            return V == Variant::StarWars ? uint16_t((p[0] << 8) | p[1]) : uint16_t(p[0] | (p[1] << 8));
        }
        const uint16_t a = uint16_t(base_ + off);
        if (V == Variant::StarWars) return uint16_t((rd_(ctx_, a) << 8) | rd_(ctx_, uint16_t(a + 1)));
        return uint16_t(rd_(ctx_, a) | (rd_(ctx_, uint16_t(a + 1)) << 8));
    };
    // dvx / dvy: 13-bit two's complement. Returns the time in master clocks.
    auto vector = [&](int dvx, int dvy, int z, bool svec) __attribute__((always_inline)) {
        const int t = normalise(dvx, dvy, bin, svec);
        // The DACs take bits 3-12.
        const int vx = (((dvx >> 3) & 0x3ff) ^ 0x200) - 0x200;
        const int vy = (((dvy >> 3) & 0x3ff) ^ 0x200) - 0x200;
        const int64_t rate = int64_t(t) * rate_scale;
        const int32_t x1 = x + int32_t((vx * rate) >> 4);
        const int32_t y1 = y - int32_t((vy * rate) >> 4);
        // Star Wars: the brightness for z, worked out at STAT (level_).
        const int level = V == Variant::StarWars ? level_[z & 7] : (z == 1 ? intensity_ : z * 2);
        if (level) sink(x, y, x1, y1, color_, level);
        x = x1;
        y = y1;
        return t;
    };
    for (; ops < max_ops; ++ops) {
        const uint16_t w0 = word();
        clocks += 16;                   // ~2 states a word, 8 clocks each
        const int op = w0 >> 13;
        if (op == 0) {                  // VCTR
            const uint16_t w1 = word();
            clocks += 16 + vector(w1 & 0x1fff, w0 & 0x1fff, w1 >> 13, false);
        } else if (op == 2) {           // SVEC
            clocks += vector((w0 & 0x1f) << 8, ((w0 >> 8) & 0x1f) << 8, (w0 >> 5) & 7, true);
        } else if (op == 3) {
            if (w0 & 0x1000) {          // SCAL
                scale_ = w0 & 0xff;
                rate_scale = scale_ ^ 0xff;
                bin = bin_scale_ = (w0 >> 8) & 7;
            } else {
                set_stat(w0);
            }
        } else if (op == 4) {           // CNTR
            x = cx_;
            y = cy_;
        } else if (op == 6) {           // RTSL
            sp = (sp - 1) & 15;
            pc = stack_[sp & 3];
        } else if (op == 1) {           // HALT
            halted = true;
            ++ops;
            break;
        } else {                        // 5 JSRL, 7 JMPL
            const uint16_t to = w0 & 0x1fff;
            if (to == 0) {              // back to the start: the picture is done
                ++ops;
                break;
            }
            if (op == 5) {
                stack_[sp & 3] = pc;
                sp = (sp + 1) & 15;
            }
            pc = to;
        }
    }
    pc_ = pc;
    sp_ = sp;
    x_ = x;
    y_ = y;
    ops_ = ops;
    halted_ = halted;
    return clocks / 8;
}

} // namespace atari_avg
