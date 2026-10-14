// Namco WSG - see namco_wsg.h.
#include "namco_wsg.h"

namespace namco_wsg {

void Wsg::init(const uint8_t* prom, int voices, uint32_t chip_clock, uint32_t out_rate)
{
    prom_ = prom;
    voices_ = voices < 1 ? 1 : voices > kMaxVoices ? kMaxVoices : voices;
    chip_clock_ = chip_clock;
    out_rate_ = out_rate ? out_rate : 1;
    // Run the voices at (about) the chip clock and average down to the
    // output rate: point sampling at 24 kHz would alias the high notes.
    sub_ = int((chip_clock_ + out_rate_ / 2) / out_rate_);
    if (sub_ < 1) sub_ = 1;
    if (sub_ > 16) sub_ = 16;
    gain_ = 32767 / (128 * voices_);
    reset();
}

void Wsg::reset()
{
    for (Voice& v : voice_) v = Voice{};
    for (uint8_t& r : regs_) r = 0;
    enabled_ = true;
}

void Wsg::update_step(Voice& v)
{
    // Accumulator units per output sub-sample, scaled by 2^12; wraps mod
    // 2^32 like the phase it is added to.
    const uint64_t per_sub = uint64_t(out_rate_) * uint64_t(sub_);
    v.step = uint32_t(((uint64_t(v.freq) << 12) * chip_clock_ + per_sub / 2) / per_sub);
}

void Wsg::set_frequency(int v, uint32_t freq)
{
    if (v < 0 || v >= voices_) return;
    voice_[v].freq = freq;
    update_step(voice_[v]);
}

void Wsg::set_waveform(int v, uint8_t wave)
{
    if (v >= 0 && v < voices_) voice_[v].wave = wave & 7;
}

void Wsg::set_volume(int v, uint8_t vol)
{
    if (v >= 0 && v < voices_) voice_[v].volume = vol & 15;
}

// Register file (nibbles): 00-04 / 06-09 / 0B-0E voice accumulators (the
// chip's own, writes ignored), 05 / 0A / 0F waveforms, 10-14 voice 0
// frequency (low nibble first; voice 0 alone has the lowest nibble),
// 15 its volume, 16-19 voice 1 frequency bits 4-19, 1A volume, 1B-1E
// voice 2 frequency, 1F volume.
void Wsg::pacman_write(uint8_t offset, uint8_t data)
{
    offset &= 0x1f;
    data &= 0x0f;
    regs_[offset] = data;
    if (offset < 0x10) {
        if (offset == 0x05 || offset == 0x0a || offset == 0x0f) set_waveform((offset - 5) / 5, data);
        return;
    }
    const int v = offset < 0x15 ? 0 : (offset - 0x11) / 5;
    const int base = 0x11 + v * 5;              // first of the 4 upper nibbles
    if (offset == base + 4) {
        set_volume(v, data);
        return;
    }
    uint32_t f = v == 0 ? regs_[0x10] : 0;
    for (int k = 0; k < 4; ++k) f |= uint32_t(regs_[base + k]) << (4 * (k + 1));
    set_frequency(v, f);
}

void Wsg::render(int16_t* out, int n)
{
    if (!enabled_ || !prom_) {
        for (int i = 0; i < n; ++i) out[i] = 0;
        return;
    }
    for (int i = 0; i < n; ++i) out[i] = 0;
    // One voice at a time over the whole buffer keeps the inner loop tight.
    int32_t mix[64];
    for (int start = 0; start < n; start += 64) {
        const int len = n - start < 64 ? n - start : 64;
        for (int i = 0; i < len; ++i) mix[i] = 0;
        for (int vi = 0; vi < voices_; ++vi) {
            Voice& v = voice_[vi];
            if (!v.volume) continue;              // the chip's counter stops too
            const uint8_t* wave = prom_ + v.wave * 32;
            uint32_t phase = v.phase;
            const uint32_t step = v.step;
            const int vol = v.volume;
            for (int i = 0; i < len; ++i) {
                int32_t acc = 0;
                for (int s = 0; s < sub_; ++s) {
                    acc += (wave[phase >> 27] & 0x0f) - 8;
                    phase += step;
                }
                mix[i] += acc * vol;
            }
            v.phase = phase;
        }
        for (int i = 0; i < len; ++i) {
            int32_t s = mix[i] * gain_ / sub_;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out[start + i] = int16_t(s);
        }
    }
}

} // namespace namco_wsg
