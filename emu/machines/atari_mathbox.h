// Atari's math box (Tempest, Battlezone, Red Baron), written for this
// project (MIT). On the board it is a 2901 bit-slice ALU running a
// microprogram from PROMs; the program's commands are known (from MAME's
// description of them, read for facts only), so each command's arithmetic
// is done directly, with the same 16-bit rounding and the same division
// loop - the PROMs aren't needed. The answer is ready at once (status 0).
//
// Writes to 6080-609F: the command is the address (0-31), the byte its
// data. Commands 00-0A / 0C-10 / 15-16 / 1A-1B load register halves;
// 0B / 11 / 12 / 13 a rotation + perspective divide; 14 a divide; 17-19
// read back; 1C a window search; 1D / 1E a distance estimate.
#pragma once

#include <cstdint>

namespace atari_mathbox {

class Mathbox {
public:
    void reset();
    void go(int command, uint8_t data);
    uint8_t lo() const { return uint8_t(result_); }
    uint8_t hi() const { return uint8_t(uint16_t(result_) >> 8); }
    int16_t result() const { return result_; }
    int16_t reg(int n) const { return r_[n & 15]; }

private:
    void rotate(bool both);         // commands 0B / 11 and on
    void rotate_y(bool both);       // command 12 and on
    void divide(int16_t num_lo, int16_t num_hi);

    int16_t r_[16] = {};
    int16_t result_ = 0;
};

} // namespace atari_mathbox
