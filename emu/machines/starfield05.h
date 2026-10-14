// Namco 05XX starfield generator (Galaga, Bosconian), written for this
// project (MIT). Hardware facts from public reverse-engineering notes and
// MAME's driver (read for facts only).
//
// The chip runs a 16-bit Fibonacci LFSR (taps 16, 13, 11, 6; period 65535)
// one step per pixel over a 256 x 256 field, never reset between frames.
// A state with bits 14-11 set and bits 15, 9, 4, 2 clear is a star: bits
// 10 and 8 give its set (0-3) and the inverted bits 4 1 0 7 6 5 its colour
// (BBGGRR). Two of the four sets show at a time. As the field is one pixel
// longer than the LFSR period, the stars drift one pixel a frame; the
// scroll inputs add or drop steps once a frame to change speed and
// direction. STARCLR low stops the LFSR and reloads its seed (0x7FFF).
//
// Running 65536 steps a frame would cost more than the game, so the 256
// stars of the whole period are found once (their step numbers), and each
// frame only works out where they land.
#pragma once

#include <cstdint>

namespace starfield05 {

constexpr uint16_t kSeed = 0x7fff;
constexpr int      kPeriod = 65535;
constexpr int      kStars = 256;            // per LFSR period
constexpr int      kWidth = 256;            // LFSR steps per line
constexpr int      kVisibleLines = 224;
constexpr int      kMaxShown = 128;         // two sets of 64

// One LFSR step.
inline uint16_t step(uint16_t s)
{
    const uint16_t bit = uint16_t((s ^ (s >> 3) ^ (s >> 5) ^ (s >> 10)) & 1);
    return uint16_t((s >> 1) | (bit << 15));
}
inline bool     is_star(uint16_t s) { return (s & 0xfa14) == 0x7800; }
inline uint8_t  star_set(uint16_t s) { return uint8_t(((s >> 9) & 2) | ((s >> 8) & 1)); }
inline uint8_t  star_colour(uint16_t s)
{
    const uint8_t c = uint8_t(((s >> 5) & 0x07) | ((s << 3) & 0x18) | ((s << 2) & 0x20));
    return uint8_t(~c & 0x3f);
}

// A star of the period: the LFSR step (from the seed) where it is output.
struct Hit {
    uint16_t index;
    uint8_t  set;
    uint8_t  colour;                        // BBGGRR, 0 = black (invisible)
};

// A star on screen, in the field's own coordinates: x 0-255 along the line
// the chip scans, y 0-223 the visible line.
struct Star {
    uint8_t x, y;
    uint8_t colour;
};

class Generator {
public:
    // Finds the period's stars (65535 LFSR steps, once).
    void init();
    void reset() { pos_ = 0; }

    // The 05XX inputs, latched for the coming frame: scroll speed (3 bits,
    // as on SCROLL_X; SCROLL_Y is tied low on Galaga), the two active sets
    // and STARCLR (false = off, LFSR back to the seed).
    void set_controls(uint8_t scroll_x, uint8_t set_a, uint8_t set_b, bool enabled);

    // One frame: writes the visible stars (lines 0-223, in LFSR order) and
    // advances the LFSR by the frame's steps. Returns the count (at most
    // kMaxShown); none while disabled.
    int frame(Star* out);

    const Hit* hits() const { return hits_; }
    int        position() const { return pos_; }   // LFSR steps from the seed, mod the period

private:
    Hit      hits_[kStars] = {};            // sorted by index
    int      pos_ = 0;
    int      pre_ = 22 * kWidth, post_ = 10 * kWidth;
    uint8_t  set_a_ = 0, set_b_ = 2;
    bool     enabled_ = false;
};

} // namespace starfield05
