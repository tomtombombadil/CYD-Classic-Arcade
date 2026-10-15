// Motorola 6821 PIA - see pia6821.h.
#include "pia6821.h"

void Pia6821::reset()
{
    out_a_reg_ = ddr_a_ = ctl_a_ = 0;
    out_b_reg_ = ddr_b_ = ctl_b_ = 0;
    irq_a1_ = irq_a2_ = irq_b1_ = irq_b2_ = false;
    irq_update();
}

void Pia6821::irq_update()
{
    const bool now = irq();
    if (now != irq_state_) {
        irq_state_ = now;
        if (irq_changed) irq_changed(ctx);
    }
}

void Pia6821::send_a()
{
    if (out_a) out_a(ctx, ddr_a_ == 0xff ? out_a_reg_ : port_a());
}

void Pia6821::send_b()
{
    if (out_b) out_b(ctx, port_b());
}

// Reading a data register clears that side's interrupt flags.
uint8_t Pia6821::read(int reg)
{
    switch (reg & 3) {
    case 0:
        if (!(ctl_a_ & 0x04)) return ddr_a_;
        irq_a1_ = irq_a2_ = false;
        irq_update();
        return port_a();
    case 1:
        return uint8_t(ctl_a_ | (irq_a1_ ? 0x80 : 0) | (irq_a2_ ? 0x40 : 0));
    case 2:
        if (!(ctl_b_ & 0x04)) return ddr_b_;
        irq_b1_ = irq_b2_ = false;
        irq_update();
        return uint8_t((out_b_reg_ & ddr_b_) | (in_b & ~ddr_b_));
    default:
        return uint8_t(ctl_b_ | (irq_b1_ ? 0x80 : 0) | (irq_b2_ ? 0x40 : 0));
    }
}

void Pia6821::write(int reg, uint8_t v)
{
    switch (reg & 3) {
    case 0:
        if (ctl_a_ & 0x04) out_a_reg_ = v;
        else               ddr_a_ = v;
        send_a();
        break;
    case 1:
        ctl_a_ = v & 0x3f;
        irq_update();
        break;
    case 2:
        if (ctl_b_ & 0x04) out_b_reg_ = v;
        else               ddr_b_ = v;
        send_b();
        break;
    default:
        ctl_b_ = v & 0x3f;
        irq_update();
        break;
    }
}

// CRx bit 1: 1 = the rising edge sets the flag, 0 = the falling edge.
void Pia6821::ca1(bool level)
{
    if (level == ca1_) return;
    ca1_ = level;
    if (level == bool(ctl_a_ & 0x02)) {
        irq_a1_ = true;
        irq_update();
    }
}

void Pia6821::cb1(bool level)
{
    if (level == cb1_) return;
    cb1_ = level;
    if (level == bool(ctl_b_ & 0x02)) {
        irq_b1_ = true;
        irq_update();
    }
}

// C2 as an input (CRx bit 5 clear): bit 4 picks the edge.
void Pia6821::ca2(bool level)
{
    if (level == ca2_) return;
    ca2_ = level;
    if (!(ctl_a_ & 0x20) && level == bool(ctl_a_ & 0x10)) {
        irq_a2_ = true;
        irq_update();
    }
}

void Pia6821::cb2(bool level)
{
    if (level == cb2_) return;
    cb2_ = level;
    if (!(ctl_b_ & 0x20) && level == bool(ctl_b_ & 0x10)) {
        irq_b2_ = true;
        irq_update();
    }
}
