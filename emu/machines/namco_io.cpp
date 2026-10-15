// Namco 06XX / 51XX / 53XX - see namco_io.h.
#include "namco_io.h"

namespace namco_io {

void Bus06::set_control(uint8_t v, uint64_t now)
{
    control_ = v;
    if ((v & 0xe0) == 0) {
        next_nmi_ = kNever;
        return;
    }
    const uint64_t clock_edge = (now / kCpuCyclesPerClock + 1) * kCpuCyclesPerClock;
    next_nmi_ = clock_edge + (reading() ? uint64_t(period()) : 0);
}

const uint8_t kJoyMap[16] = {
    // active-low LDRU: all pressed ... none pressed
    0xf, 0xe, 0xd, 0x5, 0xc, 0x9, 0x7, 0x6, 0xb, 0x3, 0xa, 0x4, 0x1, 0x2, 0x0, 0x8,
};

void Chip51::reset()
{
    mode_ = 0;
    coinage_left_ = 0;
    coins_[0] = coins_[1] = 0;
    last_buttons_ = 0;
    last_fire_ = 0;
    count_ = 0;
    remap_ = false;
    credits_ = 0;
}

void Chip51::write(uint8_t data)
{
    data &= 0x07;
    if (coinage_left_) {
        switch (coinage_left_--) {
        case 4: coins_per_credit_[0] = data; break;
        case 3: credits_per_coin_[0] = data; break;
        case 2: coins_per_credit_[1] = data; break;
        default: credits_per_coin_[1] = data; break;
        }
        return;
    }
    switch (data) {
    case 1:                     // coinage follows; the credits start over
        coinage_left_ = 4;
        credits_ = 0;
        break;
    case 2: mode_ = 1; count_ = 0; break;
    case 3: remap_ = false; break;
    case 4: remap_ = true; break;
    case 5: mode_ = 0; count_ = 0; break;
    default: break;             // 0, 6, 7: nothing
    }
}

uint8_t Chip51::player(int p)
{
    uint8_t joy = uint8_t((sticks_ >> (4 * p)) & 0x0f);
    if (remap_) joy = kJoyMap[joy];
    const uint8_t bit = uint8_t(1 << p);
    const bool down = !(buttons_ & bit);
    const bool fresh = down && !(last_fire_ & bit);
    last_fire_ = uint8_t(down ? last_fire_ | bit : last_fire_ & ~bit);
    if (!fresh) joy |= 0x10;
    if (!down) joy |= 0x20;
    return joy;
}

uint8_t Chip51::read()
{
    const int slot = count_;
    count_ = uint8_t(count_ == 2 ? 0 : count_ + 1);
    if (mode_ == 0) {
        if (slot == 0) return buttons_;
        if (slot == 1) return sticks_;
        return 0;
    }
    if (slot != 0) return player(slot - 1);

    // The credits byte: count coins and start buttons on their press.
    const uint8_t in = uint8_t(~buttons_);
    const uint8_t pressed = uint8_t(in & ~last_buttons_);
    last_buttons_ = in;
    if (coins_per_credit_[0] == 0) {
        credits_ = 100;                     // free play
    } else if (credits_ < 99) {
        for (int s = 0; s < 2; ++s) {
            if (!(pressed & (0x10 << s))) continue;
            if (++coins_[s] >= coins_per_credit_[s]) {
                credits_ += credits_per_coin_[s];
                coins_[s] = uint8_t(coins_[s] - coins_per_credit_[s]);
            }
        }
        if (pressed & 0x40) ++credits_;     // service credit
    }
    if (mode_ == 1) {
        if ((pressed & 0x04) && credits_ >= 1) {
            credits_ -= 1;
            mode_ = 2;
        } else if ((pressed & 0x08) && credits_ >= 2) {
            credits_ -= 2;
            mode_ = 2;
        }
    }
    if (in & 0x80) return 0xbb;             // test switch on
    return uint8_t((credits_ / 10) * 16 + credits_ % 10);
}

} // namespace namco_io
