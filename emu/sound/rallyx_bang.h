// Rally-X crash ("BANG") sound model, written for this project (MIT).
// On the real sound board the crash is an analog circuit, not the wavetable
// chip: an 18-stage shift register with XOR feedback (CD4006 + CD4070)
// makes noise, clocked by the video's 4V line (2 kHz). The program's BANG
// latch bit charges a 1 uF capacitor to +5 V through an analog switch
// (CD4066); a second switch, opened and closed by the noise bit, lets the
// capacitor out through 150k + 22k to ground, so the noise is chopped from
// a voltage that dies away (~0.17 s while the switch is closed, about twice
// that on average). The 22k end of that divider feeds a uA741 stage
// (10 nF in, 470k || 10 nF feedback): a high-pass at ~34 Hz, and with the
// divider's ~19k source resistance a low-pass at ~840 Hz - a dull rumble.
// It is mixed with the wavetable sound through 4.7k / 10k into a 47 nF
// node (~4.4 kHz) before the power amplifier.
//
// Part values are the board's (facts read from MAME's netlist of the sound
// board, CC0); this is a small model of it, cheap enough to run at 24 kHz
// on the ESP32, and only while it sounds.
#pragma once

#include <cstdint>

namespace rallyx_bang {

class Bang {
public:
    void init(uint32_t rate);
    void reset();

    // The BANG latch bit (on = the capacitor is charged / held up).
    void set_trigger(bool on) { trigger_ = on; }
    bool trigger() const { return trigger_; }
    // Still making sound (the capacitor or the filters not yet quiet).
    bool active() const;

    // Adds `n` samples at the output rate to `out` (saturating).
    void mix(int16_t* out, int n);
    // Writes `n` samples (overwrites `out`).
    void render(int16_t* out, int n);

    // For tests: the capacitor's charge (0-1) and the noise register.
    float    charge() const { return cap_; }
    uint32_t noise_register() const { return lfsr_; }
    // One step of the noise register (also used by the tests).
    static uint32_t lfsr_next(uint32_t r);

private:
    float    rate_ = 24000;
    bool     trigger_ = false;
    uint32_t lfsr_ = 1, clk_phase_ = 0, clk_step_ = 0;
    float    cap_ = 0, charge_ = 0, discharge_ = 0;
    float    lp_ = 0, lp_c_ = 0;                  // divider source R into C5
    float    hp_x_ = 0, hp_y_ = 0, hp_r_ = 0;     // the uA741 stage
    float    out_lp_ = 0, out_c_ = 0;             // the mixing node
};

} // namespace rallyx_bang
