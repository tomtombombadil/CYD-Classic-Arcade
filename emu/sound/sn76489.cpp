// SN76489 - see sn76489.h.
#include "sn76489.h"

#include <cmath>

namespace sn76489 {

void Chip::init(uint32_t clock, uint32_t rate, int level)
{
    clock_ = clock;
    step_ = uint32_t((uint64_t(clock / 16) << 16) / rate);
    for (int a = 0; a < 15; ++a) level_[a] = int(std::lround(level * std::pow(10.0, -2.0 * a / 20.0)));
    level_[15] = 0;
    reset();
}

void Chip::reset()
{
    frac_ = 0;
    latched_ = 0;
    for (int c = 0; c < 4; ++c) {
        period_[c] = 0x400;
        count_[c] = 0x400;
        out_[c] = 0;
        atten_[c] = 15;
    }
    noise_ctl_ = 0;
    lfsr_ = 0x4000;
    set_noise_period();
    count_[3] = period_[3];
    out_[3] = lfsr_ & 1;
}

void Chip::set_noise_period()
{
    const int r = noise_ctl_ & 3;
    period_[3] = uint16_t(r == 3 ? period_[2] * 2 : 32 << r);
}

void Chip::write(uint8_t v)
{
    if (v & 0x80) latched_ = uint8_t((v >> 4) & 7);
    const int ch = latched_ >> 1;
    if (latched_ & 1) {                     // attenuation (both byte kinds set the low bits)
        atten_[ch] = v & 0x0f;
        return;
    }
    if (ch == 3) {                          // noise control
        noise_ctl_ = v & 0x07;
        set_noise_period();
        lfsr_ = 0x4000;
        return;
    }
    uint16_t reg = period_[ch] == 0x400 ? 0 : period_[ch];
    if (v & 0x80) reg = uint16_t((reg & 0x3f0) | (v & 0x0f));
    else          reg = uint16_t((reg & 0x00f) | ((v & 0x3f) << 4));
    period_[ch] = reg ? reg : 0x400;
    if (ch == 2 && (noise_ctl_ & 3) == 3) set_noise_period();
}

void Chip::shift_noise()
{
    const int fb = (noise_ctl_ & 4) ? ((lfsr_ ^ (lfsr_ >> 1)) & 1) : (lfsr_ & 1);
    lfsr_ = uint16_t((lfsr_ >> 1) | (fb << 14));
    out_[3] = lfsr_ & 1;
}

void Chip::mix(int32_t* acc, int n)
{
    for (int s = 0; s < n; ++s) {
        frac_ += step_;
        const int t = int(frac_ >> 16);
        frac_ &= 0xffff;
        if (t == 0) continue;
        int32_t sum = 0;                    // level x ticks
        for (int c = 0; c < 4; ++c) {
            if (atten_[c] == 15) {          // silent: only keep the counters going
                count_[c] -= t;
                while (count_[c] <= 0) {
                    if (c < 3) out_[c] ^= 1;
                    else       shift_noise();
                    count_[c] += period_[c];
                }
                continue;
            }
            int left = t, high = 0;
            while (count_[c] <= left) {
                if (out_[c]) high += count_[c];
                left -= count_[c];
                if (c < 3) out_[c] ^= 1;
                else       shift_noise();
                count_[c] = period_[c];
            }
            if (out_[c]) high += left;
            count_[c] -= left;
            sum += level_[atten_[c]] * high;
        }
        if (sum) acc[s] += sum / t;
    }
}

} // namespace sn76489
