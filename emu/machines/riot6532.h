// MOS 6532 RIOT (RAM, I/O, timer), written for this project (MIT): 128
// bytes of RAM, two 8-bit ports with direction registers, the PA7 edge
// detector and the interval timer (1 / 8 / 64 / 1024 clocks a count; after
// it reaches 0 it raises its flag and counts on once a clock). Facts from
// the MOS datasheet and MAME's 6532 (read for facts only). Times are the
// host's clock counts (int64, never going back).
//
// Register selection (A0-A4 of the I/O range): A2 = 0: A1-A0 = port A,
// DDR A, port B, DDR B. A2 = 1: read A0 = 0 the timer (A3 = its interrupt
// enable), A0 = 1 the interrupt flags (bit 7 timer, bit 6 PA7 edge; reading
// clears the edge flag); write A4 = 1 the timer (A1-A0 = prescaler, A3 =
// interrupt enable), A4 = 0 the edge detector (A0 = rising, A1 = enable).
#pragma once

#include <cstdint>

namespace riot6532 {

class Riot {
public:
    void reset();

    uint8_t ram[128] = {};

    uint8_t read(int reg, int64_t now);
    void    write(int reg, uint8_t v, int64_t now);

    // Port inputs (pins not driven by the chip). PA changes go through the
    // edge detector.
    void set_pa_in(uint8_t v);
    void set_pb_in(uint8_t v) { pb_in_ = v; }
    // What the pins show: outputs where DDR = 1, else pulled high.
    uint8_t pa_pins() const { return uint8_t((pa_out_ & pa_ddr_) | ~pa_ddr_); }
    uint8_t pb_pins() const { return uint8_t((pb_out_ & pb_ddr_) | ~pb_ddr_); }

    // The IRQ output (active) as of `now` (the timer's flag raised once due).
    bool irq(int64_t now);
    // When the timer next raises an interrupt (-1: it won't by itself).
    int64_t next_event() const;

    uint8_t timer(int64_t now) const;

private:
    void edge_detect();
    void timer_start(uint8_t v, int64_t now);
    void timer_due(int64_t now);

    uint8_t pa_in_ = 0xff, pa_out_ = 0, pa_ddr_ = 0;
    uint8_t pb_in_ = 0xff, pb_out_ = 0, pb_ddr_ = 0;
    bool    pa7_ = true, edge_rising_ = false, edge_ie_ = false, edge_flag_ = false;
    bool    timer_ie_ = false, timer_flag_ = false;
    bool    counting_ = true;           // false once it passed 0 (counting at 1x)
    int     shift_ = 10;
    int64_t timer_end_ = 0;             // when the count reaches 0 (while counting_)
    int64_t timeout_ = -1000;           // when the flag was last raised
};

} // namespace riot6532
