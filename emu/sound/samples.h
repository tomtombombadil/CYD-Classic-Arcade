// Sound samples, written for this project (MIT): a WAV parser on a memory
// buffer and a small multi-channel player that resamples to the output rate.
// Used for MAME's optional sample sets (recordings of a cabinet's analog
// sounds, e.g. Space Invaders' "invaders" set): the machine starts and stops
// samples where the original board switched its circuits.
//
// WAV: RIFF / WAVE with PCM data (format 1, or WAVE_FORMAT_EXTENSIBLE with
// the PCM sub-format), 8-bit unsigned or 16-bit signed, mono or stereo
// (stereo is played as the average of both channels), any rate. The data
// stays where it is (flash or RAM); nothing is copied.
#pragma once

#include <cstddef>
#include <cstdint>

namespace samples {

struct Wav {
    const uint8_t* data = nullptr;  // first sample frame
    uint32_t frames = 0;            // sample frames (one per channel set)
    uint32_t rate = 0;              // Hz
    uint8_t  bits = 0;              // 8 or 16
    uint8_t  channels = 0;          // 1 or 2

    // Sample frame i as signed 16-bit (no bounds check).
    int16_t at(uint32_t i) const
    {
        if (bits == 8) {
            const uint8_t* p = data + size_t(i) * channels;
            int v = (p[0] - 128) * 256;
            if (channels == 2) v = (v + (p[1] - 128) * 256) / 2;
            return int16_t(v);
        }
        const uint8_t* p = data + size_t(i) * channels * 2;
        int v = int16_t(uint16_t(p[0] | (p[1] << 8)));
        if (channels == 2) v = (v + int16_t(uint16_t(p[2] | (p[3] << 8)))) / 2;
        return int16_t(v);
    }
};

// Fills `out` from a whole WAV file in memory. False (and `out` left empty)
// for anything that isn't a WAV this player can play.
bool parse_wav(const uint8_t* buf, size_t len, Wav& out);

class Player {
public:
    static constexpr int kMaxChannels = 8;

    void init(uint32_t out_rate);
    void reset();                       // stops everything (volumes stay)

    // Starts `w` on channel `ch` from its beginning (restarting it if it
    // was playing). A looping sample plays until stop().
    void start(int ch, const Wav* w, bool loop = false);
    void stop(int ch);
    bool playing(int ch) const { return ch >= 0 && ch < kMaxChannels && voice_[ch].wav; }
    // Channel volume, 0-256 (256 = as recorded). Default 256.
    void set_volume(int ch, int vol);

    // Adds `n` samples at the output rate to `out` (saturating).
    void mix(int16_t* out, int n);

private:
    struct Voice {
        const Wav* wav;
        uint64_t   pos;     // frame position, 32.32 fixed point
        uint64_t   step;    // frames per output sample, 32.32
        int        volume;
        bool       loop;
    };
    Voice    voice_[kMaxChannels] = {};
    uint32_t out_rate_ = 24000;
};

} // namespace samples
