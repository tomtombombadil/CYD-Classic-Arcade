// Namco 54XX noise generator, our own sound model (MIT). The 54XX is a
// Fujitsu MB8844 microcontroller whose program (54xx.bin) isn't in the
// usual ROM sets; it makes the explosions in Galaga (and Bosconian /
// Xevious / Pole Position) as noise on three 4-bit outputs, each through a
// resistor DAC and an op-amp band-pass filter on the board (Galaga:
// centres about 170 Hz, 450 Hz and 2.5 kHz, the lowest mixed loudest).
// Hardware facts from public documentation and MAME's driver (read for
// facts only); the sounds themselves are modelled, not emulated.
//
// Commands (one byte each, through the 06XX): 1x play sound A, 2x play
// sound B, 3x / 4x + 4 bytes set sound A / B's parameters, 5x play sound
// C, 6x + 5 bytes set C's parameters, 7x sound C's volume, 0x / 8x-Fx
// nothing. Galaga sets A and B up at power-on and sends "1x 1x 2x 2x"
// when the player's ship explodes.
//
// The model: per sound, a noise source (a sample-and-hold LFSR at a rate
// of its own) whose level decays from the start, into the board's band-
// pass filters. Sound A is a short bright crack (450 Hz + 2.5 kHz), B a
// long low rumble (170 Hz + 450 Hz), C a mid one. The parameter bytes are
// kept (and counted) but don't change the model.
#pragma once

#include <cstdint>

namespace namco54 {

class Chip {
public:
    enum Sound { kA, kB, kC, kSounds };

    void init(uint32_t out_rate);
    void reset();
    void write(uint8_t data);

    // Adds n samples of the chip's sound to `out` (16-bit, saturating).
    void mix(int16_t* out, int n);

    // For tests and the PC tool.
    uint32_t commands() const { return commands_; }     // bytes received
    uint32_t starts(int sound) const { return starts_[sound]; }
    bool     playing() const;
    const uint8_t* params(int sound) const { return voice_[sound].params; }

private:
    // A 2-pole band-pass (state-variable form).
    struct Filter {
        float f, q;                 // 2 sin(pi fc / fs), 1 / Q
        float gain;                 // the board's mixer weight and filter gain
        float low, band;
    };
    struct Voice {
        float    level;             // envelope (0 = silent)
        float    decay;             // per sample
        uint32_t phase, step;       // noise clock, phase in 1/65536 samples
        float    noise;             // the held noise value, +-1
        uint8_t  params[5];
    };

    void start(int sound);

    uint32_t out_rate_ = 24000;
    Voice    voice_[kSounds] = {};
    Filter   filter_[3] = {};
    uint32_t lfsr_ = 1;
    uint8_t  pending_ = 0;          // parameter bytes still to come
    uint8_t  pending_sound_ = 0;
    uint8_t  volume_c_ = 15;
    uint32_t commands_ = 0;
    uint32_t starts_[kSounds] = {};
};

} // namespace namco54
