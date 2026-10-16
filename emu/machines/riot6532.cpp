// 6532 RIOT - see riot6532.h.
#include "riot6532.h"

namespace riot6532 {

void Riot::reset()
{
    pa_out_ = pa_ddr_ = pb_out_ = pb_ddr_ = 0;
    edge_rising_ = edge_ie_ = edge_flag_ = false;
    timer_ie_ = timer_flag_ = false;
    counting_ = true;
    shift_ = 10;
    timer_end_ = 0;
    edge_detect();
}

void Riot::edge_detect()
{
    const bool pa7 = (((pa_out_ & pa_ddr_) | (pa_in_ & ~pa_ddr_)) & 0x80) != 0;
    if (pa7 != pa7_ && pa7 == edge_rising_) edge_flag_ = true;
    pa7_ = pa7;
}

void Riot::set_pa_in(uint8_t v)
{
    pa_in_ = v;
    edge_detect();
}

void Riot::timer_start(uint8_t v, int64_t now)
{
    counting_ = true;
    timer_end_ = now + 1 + (int64_t(v) << shift_);
}

// The count reached 0: the flag goes up, and it counts on once a clock.
void Riot::timer_due(int64_t now)
{
    if (counting_ && now >= timer_end_) {
        counting_ = false;
        timer_flag_ = true;
        timeout_ = timer_end_;
    }
}

uint8_t Riot::timer(int64_t now) const
{
    if (counting_ && now < timer_end_) {
        const int64_t remain = timer_end_ - now;
        const uint8_t v = uint8_t(remain >> shift_);
        return (remain & ((1 << shift_) - 1)) ? v : uint8_t(v - 1);
    }
    const int64_t since = now - (counting_ ? timer_end_ : timeout_);
    return uint8_t(0xff - (since & 0xff));
}

bool Riot::irq(int64_t now)
{
    timer_due(now);
    return (timer_ie_ && timer_flag_) || (edge_ie_ && edge_flag_);
}

int64_t Riot::next_event() const
{
    return timer_ie_ && counting_ && !timer_flag_ ? timer_end_ : -1;
}

uint8_t Riot::read(int reg, int64_t now)
{
    reg &= 0x1f;
    if (!(reg & 4)) {
        switch (reg & 3) {
        case 0: return uint8_t((pa_out_ & pa_ddr_) | (pa_in_ & ~pa_ddr_));
        case 1: return pa_ddr_;
        case 2: return uint8_t((pb_out_ & pb_ddr_) | (pb_in_ & ~pb_ddr_));
        default: return pb_ddr_;
        }
    }
    timer_due(now);
    if (reg & 1) {                  // interrupt flags; the edge flag clears
        const uint8_t v = uint8_t((timer_flag_ ? 0x80 : 0) | (edge_flag_ ? 0x40 : 0));
        edge_flag_ = false;
        return v;
    }
    // The timer; reading clears its flag (unless it was raised this very
    // moment) and, past 0, starts it counting again from the value read.
    const uint8_t v = timer(now);
    if (now - timeout_ > 2) {
        timer_flag_ = false;
        if (!counting_) timer_start(v, now);
    }
    timer_ie_ = (reg & 8) != 0;
    return v;
}

void Riot::write(int reg, uint8_t v, int64_t now)
{
    reg &= 0x1f;
    if (!(reg & 4)) {
        switch (reg & 3) {
        case 0: pa_out_ = v; edge_detect(); break;
        case 1: pa_ddr_ = v; edge_detect(); break;
        case 2: pb_out_ = v; break;
        default: pb_ddr_ = v; break;
        }
        return;
    }
    if (reg & 0x10) {
        static const int kShift[4] = {0, 3, 6, 10};
        shift_ = kShift[reg & 3];
        timer_start(v, now);
        timer_flag_ = false;
        timer_ie_ = (reg & 8) != 0;
    } else {
        edge_rising_ = (reg & 1) != 0;
        edge_ie_ = (reg & 2) != 0;
    }
}

} // namespace riot6532
