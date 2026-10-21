// MC68705P3 interpreter - see m68705.h.
#include "m68705.h"

namespace m68705 {

namespace {

// Machine cycles per opcode, the HMOS M6805 table (Motorola's data; 0 =
// undocumented: runs as a 2-cycle no-op).
constexpr uint8_t kCycles[256] = {
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,     // 0x: BRSET / BRCLR
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,                     // 1x: BSET / BCLR
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,                     // 2x: branches
    6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 0, 6,                     // 3x: direct
    4, 0, 0, 4, 4, 0, 4, 4, 4, 4, 4, 0, 4, 4, 0, 4,                     // 4x: A
    4, 0, 0, 4, 4, 0, 4, 4, 4, 4, 4, 0, 4, 4, 0, 4,                     // 5x: X
    7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 0, 7,                     // 6x: indexed + 1 byte
    6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 0, 6,                     // 7x: indexed
    9, 6, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                    // 8x: RTI RTS SWI
    0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 0, 2,                     // 9x: TAX ... TXA
    2, 2, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 0, 8, 2, 0,                     // Ax: immediate, BSR
    4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 3, 7, 4, 5,                     // Bx: direct
    5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 4, 8, 5, 6,                     // Cx: extended
    6, 6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 6, 5, 9, 6, 7,                     // Dx: indexed + 2 bytes
    5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 4, 8, 5, 6,                     // Ex: indexed + 1 byte
    4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 3, 7, 4, 5,                     // Fx: indexed
};

} // namespace

void Cpu::init(const uint8_t* rom)
{
    rom_ = rom;
    tsrc_ = 0;
    tdiv_ = 7;
    reset();
}

void Cpu::reset()
{
    for (int p = 0; p < 3; ++p) {
        ddr[p] = 0;
        if (port_out) port_out(ctx, p, latch[p], 0);
    }
    tdr = 0xff;
    tcr = 0x7f;
    prescale = 0x7f;
    pending_ = 0;
    cc = 0xe0 | CC_I;
    sp = 0x7f;
    pc = vec(kVecReset);
}

uint8_t Cpu::port_read(int p)
{
    const uint8_t in = port_in ? port_in(ctx, p) : 0xff;
    uint8_t v = uint8_t((latch[p] & ddr[p]) | (in & ~ddr[p]));
    if (p == 2) v |= 0xf0;                              // port C: four bits
    return v;
}

uint8_t Cpu::rd(uint16_t ad)
{
    ad &= 0x7ff;
    if (ad >= 0x80) return rom_[ad];
    if (ad >= 0x10) return ram[ad - 0x10];
    switch (ad) {
    case 0: case 1: case 2: return port_read(ad);
    case 8:  return tdr;
    case 9:  return uint8_t(tcr & ~0x08);               // PSC reads 0
    default: return 0xff;
    }
}

void Cpu::wr(uint16_t ad, uint8_t v)
{
    ad &= 0x7ff;
    if (ad >= 0x80) return;                             // EPROM: no programming here
    if (ad >= 0x10) {
        ram[ad - 0x10] = v;
        return;
    }
    switch (ad) {
    case 0: case 1: case 2:
        if (ad == 2) v |= 0xf0;
        latch[ad] = v;
        if (port_out) port_out(ctx, ad, latch[ad], ddr[ad]);
        break;
    case 4: case 5: case 6: {
        const int p = ad - 4;
        if (p == 2) v &= 0x0f;
        ddr[p] = v;
        if (port_out) port_out(ctx, p, latch[p], ddr[p]);
        break;
    }
    case 8: tdr = v; break;
    case 9: tcr_write(v); break;
    default: break;
    }
}

void Cpu::tcr_write(uint8_t v)
{
    tdiv_ = v & 7;
    tsrc_ = (v >> 4) & 3;
    if (v & 0x08) prescale = 0;                         // PSC: clears the prescaler
    // TIR can only be cleared by a write (a 1 keeps it as it is).
    tcr = uint8_t((tcr & v & 0x80) | (v & 0x77));
    if ((tcr & 0x80) && !(tcr & 0x40)) pending_ |= kPendTimer;
    else pending_ &= uint8_t(~kPendTimer);
}

int Cpu::interrupt()
{
    if (!(pending_ & (kPendInt | kPendTimer)) || (cc & CC_I)) return 0;
    push(uint8_t(pc));
    push(uint8_t(pc >> 8));
    push(x);
    push(a);
    push(cc);
    cc |= CC_I;
    if (pending_ & kPendInt) {
        pending_ &= uint8_t(~kPendInt);
        pc = vec(kVecInt);
    } else {
        pc = vec(kVecTimer);                            // level: stays pending until TIR is cleared
    }
    return 11;
}

uint8_t Cpu::rmw(uint8_t op, uint8_t t)
{
    switch (op & 0x0f) {
    case 0x0: {                                         // NEG
        const unsigned r = 0u - t;
        nzc(r);
        return uint8_t(r);
    }
    case 0x3:                                           // COM
        t = uint8_t(~t);
        nz(t);
        cc |= CC_C;
        return t;
    case 0x4:                                           // LSR
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (t & 1));
        t >>= 1;
        if (!t) cc |= CC_Z;
        return t;
    case 0x6: {                                         // ROR
        const uint8_t r = uint8_t((cc & CC_C) << 7 | t >> 1);
        cc = uint8_t((cc & ~CC_C) | (t & 1));
        nz(r);
        return r;
    }
    case 0x7: {                                         // ASR
        const uint8_t r = uint8_t((t & 0x80) | t >> 1);
        cc = uint8_t((cc & ~CC_C) | (t & 1));
        nz(r);
        return r;
    }
    case 0x8: {                                         // LSL
        const unsigned r = unsigned(t) << 1;
        nzc(r);
        return uint8_t(r);
    }
    case 0x9: {                                         // ROL
        const unsigned r = (unsigned(t) << 1) | (cc & CC_C);
        nzc(r);
        return uint8_t(r);
    }
    case 0xa: --t; nz(t); return t;                     // DEC
    case 0xc: ++t; nz(t); return t;                     // INC
    case 0xd: nz(t); return t;                          // TST
    default:  nz(0); return 0;                          // CLR (0xF)
    }
}

int Cpu::exec(uint8_t op)
{
    const int hi = op >> 4;
    switch (hi) {
    case 0x0: {                                         // BRSET n / BRCLR n
        const uint8_t v = rd(rd(pc));
        const int8_t rel = int8_t(rd(uint16_t(pc + 1)));
        pc = uint16_t((pc + 2) & 0x7ff);
        const bool set = (v >> ((op >> 1) & 7)) & 1;
        cc = uint8_t((cc & ~CC_C) | (set ? CC_C : 0));
        if (set != bool(op & 1)) pc = uint16_t((pc + rel) & 0x7ff);
        return 10;
    }
    case 0x1: {                                         // BSET n / BCLR n
        const uint8_t ad = rd(pc);
        pc = uint16_t((pc + 1) & 0x7ff);
        const uint8_t bit = uint8_t(1u << ((op >> 1) & 7));
        const uint8_t v = rd(ad);
        wr(ad, (op & 1) ? uint8_t(v & ~bit) : uint8_t(v | bit));
        return 7;
    }
    case 0x2: {                                         // branches
        const int8_t rel = int8_t(rd(pc));
        pc = uint16_t((pc + 1) & 0x7ff);
        bool t;
        switch (op & 0x0e) {
        case 0x0: t = true; break;                                      // BRA / BRN
        case 0x2: t = !(cc & (CC_C | CC_Z)); break;                     // BHI / BLS
        case 0x4: t = !(cc & CC_C); break;                              // BCC / BCS
        case 0x6: t = !(cc & CC_Z); break;                              // BNE / BEQ
        case 0x8: t = !(cc & CC_H); break;                              // BHCC / BHCS
        case 0xa: t = !(cc & CC_N); break;                              // BPL / BMI
        case 0xc: t = !(cc & CC_I); break;                              // BMC / BMS
        default:  t = int_pin_; break;                                  // BIL / BIH
        }
        if (op & 1) t = !t;
        if (t) pc = uint16_t((pc + rel) & 0x7ff);
        return 4;
    }
    case 0x3: case 0x6: case 0x7: {                     // read-modify-write in memory
        uint16_t ad;
        if (hi == 0x3) {
            ad = rd(pc);
            pc = uint16_t((pc + 1) & 0x7ff);
        } else if (hi == 0x6) {
            ad = uint16_t(x + rd(pc));
            pc = uint16_t((pc + 1) & 0x7ff);
        } else {
            ad = x;
        }
        if ((op & 0x0f) == 0x0f) {                      // CLR: no read
            nz(0);
            wr(ad, 0);
        } else {
            const uint8_t r = rmw(op, rd(ad));
            if ((op & 0x0f) != 0x0d) wr(ad, r);         // TST writes nothing
        }
        return kCycles[op];
    }
    case 0x4: a = rmw(op, a); return 4;
    case 0x5: x = rmw(op, x); return 4;
    case 0x8:
        if (op == 0x80) {                               // RTI
            cc = uint8_t(pull() | 0xe0);
            a = pull();
            x = pull();
            const uint8_t h = pull();
            pc = uint16_t(((h << 8) | pull()) & 0x7ff);
        } else if (op == 0x81) {                        // RTS
            const uint8_t h = pull();
            pc = uint16_t(((h << 8) | pull()) & 0x7ff);
        } else {                                        // SWI
            push(uint8_t(pc));
            push(uint8_t(pc >> 8));
            push(x);
            push(a);
            push(cc);
            cc |= CC_I;
            pc = vec(kVecSwi);
        }
        return kCycles[op];
    case 0x9:
        switch (op) {
        case 0x97: x = a; break;
        case 0x98: cc &= uint8_t(~CC_C); break;
        case 0x99: cc |= CC_C; break;
        case 0x9a: cc &= uint8_t(~CC_I); break;
        case 0x9b: cc |= CC_I; break;
        case 0x9c: sp = 0x7f; break;
        case 0x9f: a = x; break;
        default: break;                                 // NOP
        }
        return 2;
    default:
        break;
    }

    // A0-FF: register / memory, the mode in the high nibble.
    const int lo = op & 0x0f;
    if (op == 0xad) {                                   // BSR
        const int8_t rel = int8_t(rd(pc));
        pc = uint16_t((pc + 1) & 0x7ff);
        push(uint8_t(pc));
        push(uint8_t(pc >> 8));
        pc = uint16_t((pc + rel) & 0x7ff);
        return 8;
    }
    uint16_t ad = 0;
    uint8_t t = 0;
    switch (hi) {
    case 0xa: t = rd(pc); pc = uint16_t((pc + 1) & 0x7ff); break;
    case 0xb: ad = rd(pc); pc = uint16_t((pc + 1) & 0x7ff); break;
    case 0xc: ad = uint16_t(((rd(pc) << 8) | rd(uint16_t(pc + 1))) & 0x7ff); pc = uint16_t((pc + 2) & 0x7ff); break;
    case 0xd: ad = uint16_t((x + ((rd(pc) << 8) | rd(uint16_t(pc + 1)))) & 0x7ff); pc = uint16_t((pc + 2) & 0x7ff); break;
    case 0xe: ad = uint16_t((x + rd(pc)) & 0x7ff); pc = uint16_t((pc + 1) & 0x7ff); break;
    default:  ad = x; break;
    }
    const bool mem = hi != 0xa;
    switch (lo) {
    case 0x7:                                           // STA
        nz(a);
        wr(ad, a);
        return kCycles[op];
    case 0xc: pc = ad; return kCycles[op];              // JMP
    case 0xd:                                           // JSR
        push(uint8_t(pc));
        push(uint8_t(pc >> 8));
        pc = ad;
        return kCycles[op];
    case 0xf:                                           // STX
        nz(x);
        wr(ad, x);
        return kCycles[op];
    default:
        break;
    }
    if (mem) t = rd(ad);
    switch (lo) {
    case 0x0: { const unsigned r = unsigned(a) - t; nzc(r); a = uint8_t(r); break; }                 // SUB
    case 0x1: nzc(unsigned(a) - t); break;                                                            // CMP
    case 0x2: { const unsigned r = unsigned(a) - t - (cc & CC_C); nzc(r); a = uint8_t(r); break; }    // SBC
    case 0x3: nzc(unsigned(x) - t); break;                                                            // CPX
    case 0x4: a &= t; nz(a); break;                                                                   // AND
    case 0x5: nz(uint8_t(a & t)); break;                                                              // BIT
    case 0x6: a = t; nz(a); break;                                                                    // LDA
    case 0x8: a ^= t; nz(a); break;                                                                   // EOR
    case 0x9: case 0xb: {                                                                             // ADC / ADD
        const unsigned r = unsigned(a) + t + (lo == 0x9 ? (cc & CC_C) : 0);
        nzc(r);
        cc = uint8_t((cc & ~CC_H) | ((a ^ t ^ r) & 0x10 ? CC_H : 0));
        a = uint8_t(r);
        break;
    }
    case 0xa: a |= t; nz(a); break;                                                                   // ORA
    case 0xe: x = t; nz(x); break;                                                                    // LDX
    default: break;
    }
    return kCycles[op];
}

int Cpu::run(int cycles)
{
    done_ = 0;
    target_ = cycles;
    while (done_ < target_) {
        int n = interrupt();
        if (!n) {
            const uint8_t op = rd(pc);
            pc = uint16_t((pc + 1) & 0x7ff);
            if (!kCycles[op]) {
                ++illegal_count;
                n = 2;
            } else {
                n = exec(op);
            }
        }
        done_ += n;
        timer(n);
    }
    return done_;
}

} // namespace m68705
