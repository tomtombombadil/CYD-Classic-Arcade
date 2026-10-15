// Motorola 6821 PIA (peripheral interface adapter), written for this
// project (MIT). Two 8-bit ports with data direction registers, the four
// control lines (CA1 / CB1 interrupt inputs, CA2 / CB2 as inputs or set /
// reset outputs) and the two interrupt outputs. The handshake (strobe)
// modes of CA2 / CB2 aren't modelled - no machine here uses them. Facts
// from the Motorola datasheet and MAME's 6821 device (read for facts only).
//
// Register select (reg 0-3): 0 = port A data or DDR A (by CRA bit 2),
// 1 = CRA, 2 = port B data or DDR B (CRB bit 2), 3 = CRB.
#pragma once

#include <cstdint>

class Pia6821 {
public:
    // What the pins read as inputs (set by the owner when they change).
    uint8_t in_a = 0xff;
    uint8_t in_b = 0x00;

    // Called when a port's output changes (data or DDR written) with the
    // port's output value; and when either interrupt output may have
    // changed. Null = not wired.
    void* ctx = nullptr;
    void (*out_a)(void* ctx, uint8_t value) = nullptr;
    void (*out_b)(void* ctx, uint8_t value) = nullptr;
    void (*irq_changed)(void* ctx) = nullptr;

    void reset();

    uint8_t read(int reg);
    void    write(int reg, uint8_t v);

    // Input control lines (edges set the interrupt flags as CRx selects).
    void ca1(bool level);
    void cb1(bool level);
    void ca2(bool level);
    void cb2(bool level);

    bool irq_a() const { return (irq_a1_ && (ctl_a_ & 0x01)) || (irq_a2_ && (ctl_a_ & 0x08)); }
    bool irq_b() const { return (irq_b1_ && (ctl_b_ & 0x01)) || (irq_b2_ && (ctl_b_ & 0x08)); }
    bool irq() const { return irq_a() || irq_b(); }

    // The ports as an outside device sees them (input pins of port A read
    // the input; input pins of port B read 0).
    uint8_t port_a() const { return uint8_t((out_a_reg_ & ddr_a_) | (in_a & ~ddr_a_)); }
    uint8_t port_b() const { return uint8_t(out_b_reg_ & ddr_b_); }
    // CA2 / CB2 as outputs (set mode: CRx bits 5 and 4 set, level = bit 3).
    bool ca2_out() const { return (ctl_a_ & 0x38) == 0x38; }
    bool cb2_out() const { return (ctl_b_ & 0x38) == 0x38; }
    // Port A's data register written by a fast path that skips the
    // callback (the owner handles the output): only when it is selected
    // and all output.
    bool a_is_output_data() const { return (ctl_a_ & 0x04) && ddr_a_ == 0xff; }
    void set_out_a_reg(uint8_t v) { out_a_reg_ = v; }
    uint8_t ctl_a() const { return ctl_a_; }
    uint8_t ctl_b() const { return ctl_b_; }
    uint8_t ddr_a() const { return ddr_a_; }
    uint8_t ddr_b() const { return ddr_b_; }

private:
    uint8_t out_a_reg_ = 0, ddr_a_ = 0, ctl_a_ = 0;
    uint8_t out_b_reg_ = 0, ddr_b_ = 0, ctl_b_ = 0;
    bool irq_a1_ = false, irq_a2_ = false, irq_b1_ = false, irq_b2_ = false;
    bool ca1_ = true, ca2_ = true, cb1_ = false, cb2_ = false;
    bool irq_state_ = false;

    void irq_update();
    void send_a();
    void send_b();
};
