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
#pragma once

#include <cstdint>

namespace atari_avg {

using read_fn = uint8_t (*)(void* ctx, uint16_t addr);
// A line in beam coordinates (16.16 screen units, X right, Y down, the
// centre at (cx, cy)), with the colour index and intensity (1-15). A dot
// when the ends meet.
using line_fn = void (*)(void* ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int intensity);

class Avg {
public:
    // `base`: CPU address of word 0 (Tempest 2000); the centre in 16.16.
    void init(read_fn rd, line_fn line, void* ctx, uint16_t base, int32_t cx, int32_t cy);
    void reset();       // the VG reset strobe: scales and colour cleared

    // Runs the list from word 0 until HALT, a jump back to word 0 (Tempest
    // keeps the AVG in an endless loop: that ends a picture) or `max_ops`
    // instructions. Returns the drawing time in CPU clocks (1.5 MHz).
    int run(int max_ops = 8192);
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
    uint16_t word();
    int  vector(int dvx, int dvy, int z, bool svec);

    read_fn  rd_ = nullptr;
    line_fn  line_ = nullptr;
    void*    ctx_ = nullptr;
    uint16_t base_ = 0;
    uint16_t pc_ = 0;               // word address
    uint16_t stack_[4] = {};
    uint8_t  sp_ = 0;
    int32_t  cx_ = 0, cy_ = 0;
    int32_t  x_ = 0, y_ = 0;        // beam, 16.16
    int      color_ = 0, intensity_ = 0;
    int      scale_ = 0, bin_scale_ = 0;
    int      ops_ = 0;
    bool     halted_ = true;
};

} // namespace atari_avg
