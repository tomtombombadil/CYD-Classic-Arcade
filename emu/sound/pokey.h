// Atari POKEY (C012294) sound and I/O chip, written for this project (MIT).
// Four channels, each a divider of the chip clock (base 64 kHz or 15 kHz,
// channels 1 and 3 optionally the full clock, channel pairs joined into
// 16-bit dividers) whose underflows step the channel's output: a square
// wave, or bits of the 4-, 5-, 9- and 17-bit polynomial counters (noise),
// a high-pass flip-flop for channels 1 / 2 clocked by 3 / 4, volume-only
// (DAC) mode, and RANDOM (bits of the 17- or 9-bit polynomial, so a read
// four clocks after another shows its high nibble shifted into the low
// one, as Tempest checks). Hardware facts from public Atari documents and
// MAME's POKEY (read for facts only).
//
// Sound is made event by event: between two underflows every channel's
// output is constant, so each output sample is the exact average of the
// level over its time (a box filter - no aliasing from sampling the 1.5
// MHz square waves). Serial I/O, the keyboard scan and POKEY's timer
// interrupts aren't emulated (no game here uses them); pot / ALLPOT reads
// return what the machine sets.
#pragma once

#include <cstdint>

namespace pokey {

// Registers (write: AUDF1..AUDCTL, SKCTL; read: POT0-7, ALLPOT, RANDOM).
enum Reg : uint8_t {
    kAudf1 = 0x0, kAudc1 = 0x1, kAudf2 = 0x2, kAudc2 = 0x3, kAudf3 = 0x4, kAudc3 = 0x5,
    kAudf4 = 0x6, kAudc4 = 0x7, kAudctl = 0x8, kStimer = 0x9, kSkrest = 0xa, kPotgo = 0xb,
    kSkctl = 0xf,
    kPot0 = 0x0, kAllpot = 0x8, kKbcode = 0x9, kRandom = 0xa, kIrqst = 0xe, kSkstat = 0xf,
};

// Output level of one chip: the sum of its four channels' volumes (0-15)
// times this, so 60 x 256 at most.
constexpr int kLevelScale = 256;

// The polynomial counters' bits, as the chip steps them once a clock
// (exposed for tests): poly4 / poly5 as bit masks of their 15 / 31 steps.
uint32_t poly4_bits();
uint32_t poly5_bits();
// Bit 8 of the 17-bit counter after step p (0 <= p < 131071); RANDOM in
// 17-bit mode reads bits 8-15, i.e. steps p .. p + 7.
int poly17_bit8(uint32_t p);
// RANDOM's value at step p, 17-bit and 9-bit modes.
uint8_t random17(uint32_t p);
uint8_t random9(uint32_t p);

class Pokey {
public:
    // `clock`: the chip's clock (Hz); `sample_rate`: render()'s rate.
    void init(uint32_t clock, uint32_t sample_rate);
    void reset();

    // A register write, taking effect at the current audio position (the
    // machine renders up to its CPU's time first). `clock` = the chip
    // clock count now, on the machine's own count (any origin, the same
    // for every read and write): RANDOM is timed by it.
    void write(uint8_t reg, uint8_t v, uint32_t clock);
    // A register read (`clock` as for write()).
    uint8_t read(uint8_t reg, uint32_t clock);

    // What pot / ALLPOT reads return (the machine wires its inputs here).
    void set_pot(int n, uint8_t v) { pot_[n & 7] = v; }
    void set_allpot(uint8_t v) { allpot_ = v; }

    // Makes n output samples: each the chip's level (0 .. 60 x kLevelScale)
    // averaged over the sample. add = mix into `out` instead of storing.
    void render(int16_t* out, int n, bool add = false);

    // Diagnostics for tests.
    uint8_t audf(int c) const { return ch_[c & 3].audf; }
    uint8_t audc(int c) const { return ch_[c & 3].audc; }
    uint8_t audctl() const { return audctl_; }
    // Channel c's output bit now (before volume).
    int output(int c) const { return ch_[c & 3].out; }
    uint32_t underflows(int c) const { return ch_[c & 3].count; }

private:
    struct Channel {
        uint8_t  audf = 0, audc = 0;
        uint8_t  out = 0;           // the divider's output flip-flop / poly bit
        uint8_t  filter = 0;        // high-pass flip-flop (channels 1 and 2)
        uint32_t period = 28;       // chip clocks between underflows
        uint32_t next = 0;          // chip clock of the next underflow
        uint32_t count = 0;         // underflows so far (tests)
    };

    void update_periods();
    void step_channel(int c, uint32_t at);
    void advance_polys(uint32_t to);
    int  level() const;

    Channel  ch_[4];
    uint8_t  audctl_ = 0;
    uint8_t  skctl_ = 0;
    uint8_t  pot_[8] = {};
    uint8_t  allpot_ = 0xff;
    uint32_t clock_ = 1512000;
    uint32_t t_ = 0;                // chip clock at the current audio position
    uint32_t step_ = 0;             // chip clocks per sample, 16.16
    uint32_t frac_ = 0;
    // Polynomial counter positions at t_poly_.
    uint32_t t_poly_ = 0;
    uint32_t p4_ = 0, p5_ = 0, p9_ = 0, p17_ = 0;
    // RANDOM: the clock count (read()'s) at which SKCTL last left reset.
    uint32_t random_origin_ = 0;
    bool     in_reset_ = true;
};

} // namespace pokey
