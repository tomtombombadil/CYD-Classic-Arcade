// The Qix sound board, written for this project (MIT): a 6802 (7.3728 MHz
// crystal, E = 921.6 kHz) running ROM U27 (F800-FFFF), its 128 bytes of
// internal RAM, a 6821 PIA at 4000 (mirrored to 7FFF) and an 8-bit DAC on
// the PIA's port B. The data CPU's sound PIA talks to this one: its port A
// drives this port A (the command) and its CA2 drives this CA1 (the
// interrupt); this PIA's interrupts drive the 6802's IRQ. The second PIA
// of the schematic (2000, for a speech chip) is never used. All of the
// sound is this program writing the DAC, so it is exact.
//
// After the DAC: the volume circuit, two 4-bit attenuators (the data
// PIA's port B, high nibble left, low nibble right: each bit switches a
// resistor - 22k, 10k, 5.6k, 3.3k - across the bottom of a divider with
// 10k on top; 0 = no attenuation). Mono here: the two channels averaged.
// Then the output coupling, a 1.5k / 1 uF high-pass (~106 Hz). Facts from
// MAME's driver (read for facts only).
//
// The owner runs it in step with its CPUs: begin_frame() with the frame's
// 6802 cycle count, run_to() before anything that crosses over, then
// end_frame().
#pragma once

#include "../cpu/m6800.h"
#include "../machines/pia6821.h"

#include <cstdint>

namespace qix_sound {

constexpr uint32_t kClock = 921600;             // 7.3728 MHz / 2 into the 6802, / 4

class Board {
public:
    // `rom`: U27, 2 KB.
    void init(const uint8_t* rom, uint32_t audio_rate);
    void reset();

    // A frame of `cycles` 6802 cycles and `samples` output samples into
    // `audio` (null = no output).
    void begin_frame(int16_t* audio, int samples, int cycles);
    void run_to(int cycle);             // runs the 6802 to this cycle of the frame
    void end_frame();                   // runs to the frame's end, fills every sample

    // From the data CPU's sound PIA (call run_to first).
    void set_command(uint8_t pins);     // its port A pins
    void set_ca1(bool level);           // its CA2
    void set_volume(uint8_t v);         // its port B

    // What this board drives back (port A pins, CA2): the owner is called
    // when they change. Qix never drives them, but the wires are there.
    void* owner = nullptr;
    void (*port_a_changed)(void* owner, uint8_t pins) = nullptr;
    void (*ca2_changed)(void* owner, bool level) = nullptr;
    uint8_t port_a_pins() const;
    bool    ca2() const;

    m6800::Cpu& cpu() { return cpu_; }
    Pia6821&    pia() { return pia_; }
    uint8_t     dac() const { return dac_; }
    uint32_t    dac_writes() const { return dac_writes_; }
    float       gain() const { return gain_; }
    int         done() const { return done_; }

private:
    static uint8_t mem_read(void* ctx, uint16_t addr);
    static void    mem_write(void* ctx, uint16_t addr, uint8_t v);
    static void    port_a_out(void* ctx, uint8_t v);
    static void    port_b_out(void* ctx, uint8_t v);
    static void    irq_changed(void* ctx);
    void audio_to(int cycle);

    m6800::Cpu cpu_;
    Pia6821    pia_;
    uint8_t    ram_[0x400] = {};        // one CPU page; the 6802's RAM is 0000-007F
    bool       ca2_ = true;

    uint8_t  dac_ = 0x80;
    uint32_t dac_writes_ = 0;
    float    gain_ = 1.0f;              // the volume circuit (both channels averaged)

    // The frame being run: sample k covers cycles [k, k + 1) x cycles_ /
    // samples_; positions are in cycles x samples_.
    int16_t* audio_ = nullptr;
    int      samples_ = 0, cycles_ = 1, done_ = 0, out_ = 0;
    float    inv_cycles_ = 1.0f;
    int64_t  pos_ = 0, acc_ = 0;
    float    hp_x_ = 0, hp_y_ = 0, hp_a_ = 0.97f;     // the output coupling
};

} // namespace qix_sound
