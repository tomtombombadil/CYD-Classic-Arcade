// Atari's Digital Vector Generator (Asteroids, Lunar Lander), written for
// this project (MIT). The DVG reads a display list of 16-bit words (low
// byte first) from the CPU's memory and moves the beam with 12-bit X / Y
// counters; a counter with bit 10 set is off the screen (the visible
// square is 0-1023 both ways, Y up). Its instructions, as documented by
// Atari and MAME's driver (read for facts only) - the state machine PROM
// isn't needed:
//   op 0-9  VCTR  y (10 bits + sign bit 10), then intensity << 12 | x;
//                 scale = global scale + op
//   op A    LABS  y (12 bits), then global scale << 12 | x (12 bits)
//   op B    HALT
//   op C    JSRL  word address (12 bits); a 4-deep stack
//   op D    RTSL
//   op E    JMPL  word address
//   op F    SVEC  one word: y (2 bits << 8, sign, scale bit 0) in the high
//                 byte, intensity << 4 | scale bit 1, x sign, x (2 bits)
//                 in the low byte; scale = global scale + 2 + its 2 bits
// A vector of scale s moves (magnitude x 2^(s+1)) >> 10 counts: the bit
// rate multipliers give exactly that many steps over 2^(s+1) clocks (none
// from scale 10 on). Intensity 0 = beam off (a move).
#pragma once

#include <cstdint>

namespace atari_dvg {

using read_fn = uint8_t (*)(void* ctx, uint16_t addr);
// A visible line (or a dot when the ends meet) in DVG coordinates.
using line_fn = void (*)(void* ctx, int x0, int y0, int x1, int y1, int intensity);

class Dvg {
public:
    // `base`: CPU address of word 0 (Asteroids 4000).
    void init(read_fn rd, line_fn line, void* ctx, uint16_t base);
    void reset();

    // VGGO: runs the list from word 0 to its HALT (at most `max_ops`
    // instructions), sending each visible line, and returns the time it
    // takes the DVG, in its 1.5 MHz clocks: one a word fetched, plus
    // 2^(s+1) a vector (its drawing time).
    int run(int max_ops = 4096);

    // State (tests).
    int x() const { return x_; }
    int y() const { return y_; }
    int scale() const { return scale_; }
    int ops() const { return ops_; }

private:
    uint16_t word();
    int  vector(int dx, int dy, int scale, int intensity);

    read_fn  rd_ = nullptr;
    line_fn  line_ = nullptr;
    void*    ctx_ = nullptr;
    uint16_t base_ = 0;
    uint16_t pc_ = 0;               // word address
    uint16_t stack_[4] = {};
    uint8_t  sp_ = 0;
    int      x_ = 0, y_ = 0;        // 12-bit beam counters
    int      scale_ = 0;            // global scale (LABS)
    int      ops_ = 0;              // instructions in the last run
};

} // namespace atari_dvg
