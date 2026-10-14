// Namco waveform sound generator (WSG), written for this project (MIT).
// The PROM-based design used by Pac-Man (3 voices) and, with the same
// register file, Galaga / Dig Dug; later boards (Mappy's 15XX) drive up to
// 8 voices the same way, so voices are set through a generic per-voice API
// and each board's register layout is a thin front-end (pacman_write).
//
// Hardware facts: the chip is clocked at the CPU clock / 32 (96 kHz on
// Pac-Man). On every clock each voice adds its frequency to a 20-bit
// accumulator; the top 5 bits pick one of the 32 4-bit samples of the
// voice's waveform in the sound PROM (8 waveforms x 32 samples, low nibble
// of each byte). The sample, centred on 8, is scaled by the 4-bit volume
// and the voices are summed.
#pragma once

#include <cstdint>

namespace namco_wsg {

constexpr int kMaxVoices = 8;

class Wsg {
public:
    // `prom`: the 256-byte waveform PROM (kept by pointer, may be in flash).
    // `chip_clock`: accumulator clock in Hz (96000 for Pac-Man).
    // `out_rate`: output sample rate (e.g. 24000).
    void init(const uint8_t* prom, int voices, uint32_t chip_clock, uint32_t out_rate);
    void reset();

    // Generic voice controls. `freq` is the accumulator increment per chip
    // clock (20 bits on the 3-voice chip, 16 on the 8-voice ones).
    void set_frequency(int v, uint32_t freq);
    void set_waveform(int v, uint8_t wave);       // 0-7
    void set_volume(int v, uint8_t vol);          // 0-15
    void set_enabled(bool on) { enabled_ = on; }  // sound enable latch: off = silence
    // Output scale; the default keeps all voices at full volume inside 16 bits.
    void set_gain(int gain) { gain_ = gain; }

    // Pac-Man / Galaga register file: offset 0x00-0x1f (5040-505F on
    // Pac-Man), only the low nibble of `data` counts.
    void pacman_write(uint8_t offset, uint8_t data);

    // Mixes `n` mono samples at the output rate into `out`.
    void render(int16_t* out, int n);

    uint32_t frequency(int v) const { return voice_[v].freq; }
    uint8_t  volume(int v) const { return voice_[v].volume; }

private:
    struct Voice {
        uint32_t phase;     // accumulator << 12: the sample index is phase >> 27
        uint32_t step;      // phase added per sub-sample
        uint32_t freq;
        uint8_t  wave;
        uint8_t  volume;
    };

    void update_step(Voice& v);

    const uint8_t* prom_ = nullptr;
    Voice    voice_[kMaxVoices] = {};
    int      voices_ = 3;
    uint32_t chip_clock_ = 96000, out_rate_ = 24000;
    int      sub_ = 4;              // sub-samples averaged per output sample
    int      gain_ = 85;
    bool     enabled_ = true;
    uint8_t  regs_[0x20] = {};
};

} // namespace namco_wsg
