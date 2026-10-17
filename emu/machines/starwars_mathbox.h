// Star Wars' matrix processor, divider and random number generator,
// written for this project (MIT). Hardware facts from the schematics as
// described in MAME's driver (read for facts only).
//
// The matrix processor is a microprogrammed unit: 1024 16-bit
// microinstructions in four 1K x 4 PROMs (136021-110 = bits 15-12, -111,
// -112, -113 = bits 3-0), run from the command's start address (the byte
// written x 4) one after the other - no branches; the counter wraps inside
// its 256-word page - until one says halt. A microinstruction is
//   bits 15-8: strobes  80 LDA  40 LDB  20 LDC (multiply-accumulate)
//                       10 clear ACC  08 BIC + 1  04 halt  02 store ACC
//                       01 load ACC
//   bit 7: 1 = address = bits 6-0; 0 = address = BIC x 4 + bits 1-0
// on 16-bit words of the math RAM (5000-5FFF, high byte first). LDC loads C
// and adds (A - B) x C x 4 to the 32-bit accumulator (a serial multiplier),
// leaving A and B their signs; ACC's top 16 bits are what load / store
// see. Each microinstruction takes 5 master clocks (12.096 MHz), a
// multiply 33 more; the main CPU polls MATH_RUN until it's done.
//
// The microinstructions are put together from the four PROMs' nibbles once,
// at init (2 KB of RAM: assembling each from four reads as it ran was a
// third of the processor's time). Old dumps (old MAME sets) have stray high
// nibbles in 14 bytes; the PROMs are 4 bits wide, so only the low nibbles
// count (and match today's dumps).
//
// The divider: a 16-step restoring division of the dividend by the divisor,
// started by the divisor's low byte, 15 quotient bits (the hardware's
// "wrong" results for a divisor < 2 x the dividend included).
//
// The random numbers: a 23-bit LFSR (x^23 + x^5 + 1, inverted feedback)
// clocked at 3 MHz; bits 8-15 are read. It runs on the CPU's clock count,
// so a read gives the hardware's value for the time it happens (jumped
// ahead in closed form).
#pragma once

#include <cstdint>

namespace starwars_math {

// Strobes (bits 15-8 of a microinstruction).
enum : uint8_t {
    kLoadAcc = 0x01, kStoreAcc = 0x02, kHalt = 0x04, kIncBic = 0x08,
    kClearAcc = 0x10, kLdc = 0x20, kLdb = 0x40, kLda = 0x80,
};

class MatrixProcessor {
public:
    // `proms`: the four 1 KB PROMs one after the other (110, 111, 112,
    // 113); `ram`: the 4 KB math RAM.
    void init(const uint8_t* proms, uint8_t* ram);
    void reset();

    // The main CPU's writes: 0 = run from data x 4 (returns the time it
    // takes, in master clocks), 1 = BIC bit 8, 2 = BIC bits 7-0.
    int  run(uint8_t start);
    void set_bic_hi(uint8_t v) { bic_ = uint16_t((bic_ & 0xff) | ((v & 1) << 8)); }
    void set_bic_lo(uint8_t v) { bic_ = uint16_t((bic_ & 0x100) | v); }

    uint16_t word(int addr) const { return micro_[addr & 1023]; }
    int16_t  a() const { return a_; }
    int16_t  b() const { return b_; }
    int16_t  c() const { return c_; }
    int32_t  acc() const { return acc_; }
    uint16_t bic() const { return bic_; }
    uint32_t steps() const { return steps_; }   // microinstructions run (all time)

private:
    uint16_t micro_[1024] = {};     // the microprogram, from the PROMs
    uint8_t* ram_ = nullptr;
    int16_t  a_ = 0, b_ = 0, c_ = 0;
    int32_t  acc_ = 0;
    uint16_t bic_ = 0;
    uint32_t steps_ = 0;
};

class Divider {
public:
    void reset() { quotient_ = dvd_shift_ = divisor_ = dividend_ = 0; }
    // Offsets 4-7 of the math writes: divisor high / low (low starts it),
    // dividend high / low.
    void write(int reg, uint8_t v);
    uint16_t quotient() const { return quotient_; }

private:
    uint16_t quotient_ = 0, dvd_shift_ = 0, divisor_ = 0, dividend_ = 0;
};

// The LFSR state after `ticks` 3 MHz clocks from `state` (23 bits).
uint32_t lfsr_advance(uint32_t state, uint64_t ticks);
// One step (tests).
uint32_t lfsr_step(uint32_t state);

} // namespace starwars_math
