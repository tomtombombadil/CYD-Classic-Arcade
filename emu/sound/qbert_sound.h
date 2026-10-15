// Q*bert's sounds that aren't in its ROMs, as our own small models (MIT):
//
// Speech: the Votrax SC-01 speech chip turns a 6-bit phoneme code into
// speech from its own internal ROM, which isn't in the game's ROM set.
// This stands in for it with a plain source-filter model: a pitch pulse
// train (or noise for hissing sounds) through two formant resonators set
// per phoneme from ordinary phonetics (English vowel formants; not the
// chip's data), with the chip's documented phoneme lengths. Q*bert's
// famous swearing is random phonemes, so a rough voice suits it. The
// speech clock (a DAC on the board) scales pitch and speed together, as
// on the chip. The A/R (request) line goes high when a phoneme has had its
// time; it drives the sound CPU's NMI.
//
// Knocker: a solenoid in the cabinet hits the side panel when Q*bert falls
// off the pyramid. A strike = a short click and two damped low
// resonances (the panel's boom and thud).
#pragma once

#include <cstdint>

namespace qbert_sound {

class Speech {
public:
    void init(uint32_t sample_rate, uint32_t cpu_clock);
    void reset();

    // A write to the chip at `cycle` (the sound CPU's cycles of all time):
    // phoneme = the low 6 bits, inflection = bits 6-7 (as the chip's pins
    // see them - the board inverts the CPU's data).
    void write(uint8_t phoneme, uint8_t inflection, int64_t cycle);
    // The speech clock DAC (0-255).
    void set_clock(uint8_t dac) { clock_dac_ = dac; }
    // When A/R rises (the current phoneme is done), or -1 when it's high.
    int64_t request_at() const { return busy_ ? done_at_ : -1; }
    // The request line at `cycle` (high = ready for the next phoneme).
    bool ready(int64_t cycle) const { return !busy_ || cycle >= done_at_; }
    // The machine saw A/R rise: the chip is idle until the next write.
    void acknowledge() { busy_ = false; }

    int16_t sample();
    uint32_t phonemes() const { return count_; }

private:
    float rate() const;     // the clock relative to the chip's nominal 720 kHz

    uint32_t fs_ = 24000, cpu_clock_ = 894886;
    uint8_t  clock_dac_ = 0xa0;
    uint8_t  phoneme_ = 0x3f, inflection_ = 0;
    bool     busy_ = false;
    int64_t  done_at_ = 0;
    uint32_t count_ = 0;

    // Synthesis state.
    float    f1_ = 500, f2_ = 1500;             // formants now (glide to the targets)
    float    amp_ = 0, noise_amp_ = 0;          // source levels now
    float    phase_ = 0;                        // pitch phase 0..1
    float    y1a_ = 0, y1b_ = 0, y2a_ = 0, y2b_ = 0, yna_ = 0, ynb_ = 0;
    float    hp_in_ = 0, hp_out_ = 0;
    uint32_t lfsr_ = 0x1234567;
    int      age_ = 0;                          // samples into the phoneme
    // Resonator coefficients (a, b, c) for F1, F2 and the hiss band, and
    // the frequencies they were made for.
    float    k1_[3] = {}, k2_[3] = {}, kn_[3] = {};
    float    kf1_ = -1, kf2_ = -1, knf_ = -1;
};

class Knocker {
public:
    void init(uint32_t sample_rate);
    void reset();
    void strike();
    int16_t sample();
    bool active() const { return left_ > 0; }

private:
    uint32_t fs_ = 24000;
    int      left_ = 0;                         // samples until silent
    // Two damped resonances (boom, thud) and a click.
    float    b1_ = 0, c1_ = 0, y1a_ = 0, y1b_ = 0;
    float    b2_ = 0, c2_ = 0, y2a_ = 0, y2b_ = 0;
    float    click_ = 0, click_decay_ = 0;
    uint32_t lfsr_ = 0xace1;
};

} // namespace qbert_sound
