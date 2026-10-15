// The Williams "special chip" blitter (SC1, VL2001), written for this
// project (MIT). Eight write-only registers at CA00-CA07:
//   0 control (writing it starts the blit)   1 solid colour ("mask")
//   2-3 source address   4-5 destination   6 width   7 height
// Control bits: 0 source in screen layout (256 bytes from one pixel pair
// to the next), 1 destination in screen layout, 2 slow (2 us a byte),
// 3 foreground only (a source pixel 0 leaves the destination), 4 solid
// colour, 5 shift one pixel right, 6 skip odd pixels, 7 skip even pixels.
// The SC1 has a known bug: width and height are taken XOR 4 (the SC2 fixed
// it; the games written for the SC1 store them pre-XORed).
//
// The blit happens at once; the CPU is halted for its length: about one
// cycle a byte (two when slow) at 1 MHz (Sean Riddle measured ~910K bytes
// a second; MAME counts 2 bus accesses of the 4 MHz chip a byte, 2 clocks
// each, 4 when slow). Source reads go
// through the CPU's memory map (so ROM when it's banked over the video
// RAM); the destination's old byte always comes from RAM below C000.
// Facts from Sean Riddle's notes and MAME's driver (read for facts only).
#pragma once

#include "../cpu/m6809.h"

#include <cstdint>

class WilliamsBlitter {
public:
    uint8_t size_xor = 4;               // 4 = SC1, 0 = SC2

    void reset();
    // A write to register `reg` (0-7). Writing the control register runs
    // the blit on `cpu`'s memory map with `ram` (0000-BFFF) and returns the
    // cycles the CPU is held; other registers return 0.
    int write(int reg, uint8_t v, m6809::Cpu& cpu, uint8_t* ram);

    uint32_t blits() const { return blits_; }
    uint32_t bytes() const { return bytes_; }

private:
    uint8_t  control_ = 0, solid_ = 0, width_ = 0, height_ = 0;
    uint16_t src_ = 0, dst_ = 0;
    uint32_t blits_ = 0, bytes_ = 0;

    int blit(m6809::Cpu& cpu, uint8_t* ram);
};
