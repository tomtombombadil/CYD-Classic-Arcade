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
        case 1: credits_per_coin_[1] = data; break;
        default: break;         // Xevious' first two
        }
        return;
    }
    switch (data) {
    case 1:                     // coinage follows; the credits start over
        coinage_left_ = xevious_ ? 6 : 4;
        credits_ = 0;
        if (xevious_) remap_ = true;
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


// --- 56XX / 58XX ---------------------------------------------------------------

void Chip5x::clear()
{
    for (uint8_t& n : ram_) n = 0;
    last_coins_ = last_buttons_ = 0;
    pin13_ = 0;
    reset_ = true;
    set_reset(true);
}

void Chip5x::set_reset(bool held)
{
    reset_ = held;
    if (!held) return;
    credits_ = 0;
    coins_[0] = coins_[1] = 0;
    coins_per_credit_[0] = coins_per_credit_[1] = 1;
    credits_per_coin_[0] = credits_per_coin_[1] = 1;
}

void Chip5x::coins(int swap)
{
    int add = 0, take = 0;
    const uint8_t c = port(pin13_, 0);
    const uint8_t c_new = uint8_t(c & ~last_coins_);
    last_coins_ = c;
    for (int s = 0; s < 2; ++s) {
        if (!(c_new & (1 << s))) continue;
        const int per = coins_per_credit_[s] & 7;
        if (++coins_[s] >= per) {
            add = credits_per_coin_[s] - (coins_per_credit_[s] >> 3);
            coins_[s] = uint8_t(coins_[s] - per);
        } else if (coins_per_credit_[s] & 8) {
            add = 1;
        }
    }
    if (c_new & 0x08) add = 1;                      // service credit

    const uint8_t b = port(pin13_, 3);
    const uint8_t b_new = uint8_t(b & ~last_buttons_);
    last_buttons_ = b;
    if (ram_[9] == 0) {                             // the game takes starts now
        if (b_new & 0x04) { if (credits_ >= 1) take = 1; }
        else if (b_new & 0x08) { if (credits_ >= 2) take = 2; }
    }
    credits_ += add - take;

    ram_[0 ^ swap] = uint8_t((credits_ / 10) & 15);
    ram_[1 ^ swap] = uint8_t(credits_ % 10);
    if (add) ram_[2 ^ swap] = uint8_t(add & 15);    // the game clears these two
    if (take) ram_[3 ^ swap] = uint8_t(take);
    ram_[4] = port(pin13_, 1);
    ram_[5] = uint8_t(((b & 5) << 1) | (b_new & 5));
    ram_[6] = port(pin13_, 2);
    ram_[7] = uint8_t((b & 0x0a) | ((b_new & 0x0a) >> 1));
}

void Chip5x::run()
{
    if (reset_) return;
    const bool c58 = type_ == Type::k58xx;
    switch (ram_[8]) {
    case 1: {                                       // the ports
        const int at = c58 ? 4 : 0;
        for (int p = 0; p < 4; ++p) ram_[at + p] = port(pin13_, p);
        pin13_ = ram_[9] & 1;                       // output port A = nibble 9
        break;
    }
    case 2:                                         // coinage
        coins_per_credit_[0] = ram_[9];
        credits_per_coin_[0] = ram_[10];
        coins_per_credit_[1] = ram_[11];
        credits_per_coin_[1] = ram_[12];
        break;
    case 3:
        if (c58) coins(2);
        break;
    case 4:
        if (!c58) {
            coins(0);
        } else {
            for (int p = 0; p < 4; ++p) {
                ram_[2 * p] = port(0, p);
                ram_[2 * p + 1] = port(1, p);
            }
            pin13_ = 1;
        }
        break;
    case 5:
        if (c58) {
            // Start-up check: each result nibble is the XOR of the inverted
            // argument nibbles whose LFSR bit is set (taps 0x90, seed 0x22
            // stepped by the first two arguments).
            auto next = [](int n) { return ((n & 1) ? n ^ 0x90 : n) >> 1; };
            static const uint8_t kOrder[7] = {11, 10, 9, 15, 14, 13, 12};
            int seed = 0x22;
            for (int i = (ram_[9] * 16 + ram_[10]) & 0x7f; i > 0; --i) seed = next(seed);
            for (int i = 1; i < 8; ++i) {
                int n = 0, r = seed;
                for (int k = 0; k < 7; ++k) {
                    if (r & 1) n ^= ~ram_[kOrder[k]];
                    r = next(r);
                    if (k == 0) seed = r;
                }
                ram_[i] = uint8_t(~n & 15);
            }
            ram_[0] = ram_[9] == 15 ? 15 : 0;
        }
        break;
    case 7:
        if (!c58) {
            ram_[2] = 0x0e;
            ram_[7] = 0x06;
        }
        break;
    case 8:
        if (!c58) {
            int sum = 0;
            for (int i = 9; i < 16; ++i) sum += ram_[i];
            ram_[0] = uint8_t((sum >> 4) & 15);
            ram_[1] = uint8_t(sum & 15);
        }
        break;
    case 9:
        if (!c58) {
            for (int p = 0; p < 4; ++p) {
                ram_[2 * p] = port(0, p);
                ram_[2 * p + 1] = port(1, p);
            }
            pin13_ = 1;
        }
        break;
    default:                                        // 0: nothing
        break;
    }
}

// --- 50XX --------------------------------------------------------------------

namespace {

const uint16_t kAmount8[16] = {5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 500};
const uint16_t kAmountB[16] = {10, 20, 30, 40, 50, 60, 80, 100, 120, 140, 160, 180, 200, 400, 600, 1000};
const uint16_t kAmountE[16] = {15, 30, 45, 60, 75, 90, 120, 150, 180, 210, 240, 270, 300, 600, 900, 1500};

uint32_t from_bcd(uint32_t v)
{
    uint32_t r = 0, m = 1;
    for (; v; v >>= 4, m *= 10) r += (v & 15) * m;
    return r;
}

constexpr uint32_t kMaxScore = 9999999;

} // namespace

void Chip50::reset()
{
    score_[0] = score_[1] = 0;
    first_ = interval_ = high_ = 0;
    high_set_ = false;
    next_bonus_[0] = next_bonus_[1] = 0;
    flags_[0] = flags_[1] = 0;
    player_ = 0;
    subtract_ = false;
    params_left_ = 0;
    count_ = 0;
}

void Chip50::add(uint32_t amount)
{
    uint32_t& s = score_[player_];
    const uint32_t old = s;
    if (subtract_) {
        s = amount > s ? 0 : s - amount;
        return;
    }
    s = s + amount > kMaxScore ? kMaxScore : s + amount;
    uint8_t& f = flags_[player_];
    if (first_ && old < first_ && s >= first_) f |= 0x40;
    if (interval_) {
        uint32_t& next = next_bonus_[player_];
        if (next == 0) next = first_ + interval_;
        if (s >= next) {
            f |= 0x20;
            while (next <= s) next += interval_;
        }
    }
    if (high_set_ && s > high_) {
        high_ = s;
        f |= 0x80;
    }
}

void Chip50::write(uint8_t data)
{
    if (params_left_) {
        param_ = (param_ << 8) | data;
        if (--params_left_ == 0) {
            const uint32_t v = from_bcd(param_);
            switch (param_cmd_) {
            case 2: first_ = v; break;
            case 3: interval_ = v; break;
            default: high_ = v; high_set_ = true; break;
            }
        }
        return;
    }
    const int hi = data >> 4, lo = data & 15;
    switch (hi) {
    case 0x0: break;
    case 0x1:
        score_[0] = score_[1] = 0;
        flags_[0] = flags_[1] = 0;
        next_bonus_[0] = next_bonus_[1] = 0;
        break;
    case 0x2: case 0x3: case 0x5:
        param_cmd_ = uint8_t(hi);
        params_left_ = 3;
        param_ = 0;
        break;
    case 0x4: break;                            // unknown
    case 0x6: player_ = (data & 0x08) ? 1 : 0; break;
    case 0x7: subtract_ = lo != 0; break;
    default: {
        const int group = (hi - 8) / 3, scale = (hi - 8) % 3;     // 8 9 A / B C D / E F
        const uint16_t* table = group == 0 ? kAmount8 : group == 1 ? kAmountB : kAmountE;
        add(uint32_t(table[lo]) * (scale == 0 ? 1 : scale == 1 ? 10 : 100));
        break;
    }
    }
}

uint8_t Chip50::read()
{
    const uint32_t s = score_[player_];
    const int slot = count_;
    count_ = uint8_t((count_ + 1) & 3);
    // Digits of the 7-digit score, most significant first.
    uint32_t bcd = 0;
    for (uint32_t v = s, sh = 0; sh < 28; v /= 10, sh += 4) bcd |= (v % 10) << sh;
    switch (slot) {
    case 0: {
        const uint8_t f = flags_[player_];
        flags_[player_] = 0;
        return uint8_t(f | ((bcd >> 24) & 0x0f));
    }
    case 1: return uint8_t(bcd >> 16);
    case 2: return uint8_t(bcd >> 8);
    default: return uint8_t(bcd);
    }
}

} // namespace namco_io
