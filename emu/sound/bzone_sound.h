// Battlezone sound board models, written for this project (MIT). Besides
// the POKEY (the radar, the bonus and "motion blocked" tunes), Battlezone's
// sounds are analog circuits switched by one 8-bit latch (1840); these are
// small models of them, with the part values of the board (facts from Al
// Kossow's notes and MAME's discrete description, read for facts only):
//
//   bit 0  explosion on      noise (bits 11-14 of the noise register NANDed,
//   bit 1  explosion loud    halved) chopping a 10 uF cap's charge
//   bit 2  shell on          noise (the register's output, halved) chopping
//   bit 3  shell loud        a 4.7 uF cap's charge
//   bit 4  engine rev        the engine's 555 runs faster (its control
//                            voltage slides through 100k / 10 uF)
//   bit 5  sound on          every sound, the POKEY's too
//   bit 6  start lamp
//   bit 7  engine on         two counters dividing the 555 by 12 and 10,
//                            their outputs summed and smoothed (a rumble)
//
// Each "on" bit charges its cap at once through a CD4066 switch; while the
// bit is off, the noise bit discharges it through 23k, so the sound fades
// (shell ~0.2 s, explosion ~0.5 s). The noise-chopped charge is amplified
// (x 33 loud, x 8.7 soft) by an op-amp with 330k / 4.7 nF feedback (a
// 103 Hz low-pass) - a dull thud, as on the cabinet.
#pragma once

#include <cstdint>

namespace bzone_sound {

enum Bit : uint8_t {
    kExplode = 0x01, kExplodeLoud = 0x02, kShell = 0x04, kShellLoud = 0x08,
    kEngineRev = 0x10, kSoundOn = 0x20, kStartLamp = 0x40, kEngineOn = 0x80,
};

class Board {
public:
    void init(uint32_t rate);
    void reset();

    void write(uint8_t v) { latch_ = v; }       // 1840

    // Writes `n` samples at the output rate. `io` holds the POKEY's level
    // (0 .. 60 x pokey::kLevelScale) for those samples on the way in and
    // the board's output on the way out.
    void render(int16_t* io, int n);

    // State (tests).
    uint8_t latch() const { return latch_; }
    float engine_cv() const { return eng_cv_; }
    float engine_hz() const;                    // the 555's frequency now
    float shell_cap() const { return sh_cap_; }
    float explode_cap() const { return ex_cap_; }
    uint16_t noise() const { return lfsr_; }

private:
    float    rate_ = 24000;
    uint8_t  latch_ = 0;

    // Noise: 16-bit shift register at 6 kHz (12 kHz halved), XNOR of bits 3
    // and 14 in at bit 0, bit 15 out; two flip-flops halve the output and
    // the NAND of bits 11-14.
    uint16_t lfsr_ = 0;
    uint32_t noise_phase_ = 0, noise_step_ = 0;
    uint8_t  shell_ff_ = 0, explode_ff_ = 0;
    uint8_t  last_out_ = 0, last_nand_ = 0;

    // The caps (volts) and the op-amps' outputs (volts).
    float    sh_cap_ = 0, ex_cap_ = 0;
    float    sh_out_ = 0, ex_out_ = 0;
    float    sh_decay_ = 0, ex_decay_ = 0;      // per sample while the noise bit is high
    float    sh_charge_ = 0, ex_charge_ = 0;    // per sample while the bit is on (270 ohm)
    float    amp_lp_ = 0;                       // the op-amps' 330k / 4.7 nF

    // Engine: control voltage (volts), the 555's phase, the two counters,
    // the mixer node.
    float    eng_cv_ = 0;
    float    eng_fall_ = 0, eng_rise_ = 0;      // 1 s down, 0.18 s up (through the diode)
    float    eng_phase_ = 0;                    // 0..1 of the 555's period
    uint8_t  cnt12_ = 4, cnt10_ = 6;
    float    eng_node_ = 0, eng_lp_ = 0;

    // POKEY: its 10k / 0.015 uF output filter.
    float    pk_out_ = 0, pk_lp_ = 0;

    // The output coupling capacitor (0.1 uF into 22k).
    float    dc_x_ = 0, dc_y_ = 0, dc_r_ = 0;
};

} // namespace bzone_sound
