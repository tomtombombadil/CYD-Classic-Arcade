// MOS 6502 CPU interpreter - see m6502.h.
//
// Dispatch: one switch over the opcode. Addressing modes are small inline
// helpers that return the effective address and add the page-cross cycle
// where the chip takes one (reads through abs,X / abs,Y / (zp),Y). Timing
// and the decimal-mode flag rules are from public 6502 documents (the
// MOS programming manual, Bruce Clark's decimal mode tutorial).
#include "m6502.h"

namespace m6502 {

namespace {

// Cycles per opcode (no page-cross / branch extras). Undocumented opcodes
// list their NMOS time though they run as NOPs.
constexpr uint8_t kCycles[256] = {
    7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,   // 00
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 10
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,   // 20
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 30
    6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,   // 40
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 50
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,   // 60
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // 70
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,   // 80
    2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,   // 90
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,   // A0
    2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,   // B0
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,   // C0
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // D0
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,   // E0
    2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,   // F0
};

// Operand bytes of the undocumented opcodes (they run as NOPs). The
// documented ones never look here.
constexpr uint8_t illegal_operands(uint8_t op)
{
    switch (op & 0x1f) {
    case 0x02: return (op == 0x82 || op == 0xc2 || op == 0xe2) ? 1 : 0;   // NOP #i; others jam (KIL)
    case 0x03: case 0x13: case 0x04: case 0x14: case 0x07: case 0x17:
    case 0x00: case 0x09: case 0x0b: return 1;                           // (zp,X) (zp),Y zp zp,X #i
    case 0x0c: case 0x1c: case 0x0f: case 0x1f: case 0x1b: case 0x1e: return 2;   // abs abs,X abs,Y
    default: return 0;                                                   // 1A-style NOPs
    }
}

uint8_t default_read(void*, uint16_t) { return 0xff; }
void default_write(void*, uint16_t, uint8_t) {}

} // namespace

Cpu::Cpu()
{
    for (int p = 0; p < kPages; ++p) {
        read_page[p] = nullptr;
        write_page[p] = nullptr;
    }
    mem_read = default_read;
    mem_write = default_write;
}

void Cpu::map_read(uint16_t start, uint32_t len, const uint8_t* p)
{
    for (uint32_t off = 0; off < len; off += kPageSize)
        read_page[((start + off) >> kPageShift) & (kPages - 1)] = p ? p + off : nullptr;
}

void Cpu::map_write(uint16_t start, uint32_t len, uint8_t* p)
{
    for (uint32_t off = 0; off < len; off += kPageSize)
        write_page[((start + off) >> kPageShift) & (kPages - 1)] = p ? p + off : nullptr;
}

void Cpu::reset()
{
    s = 0xfd;
    i_ = true;
    d_ = false;
    irq_delay_ = false;
    nmi_pending_ = false;
    check_ = irq_line_;
    pc = uint16_t(rd(0xfffc) | (rd(0xfffd) << 8));
}

void Cpu::interrupt(uint16_t vector, bool brk)
{
    push(uint8_t(pc >> 8));
    push(uint8_t(pc));
    push(uint8_t(p() | (brk ? BF : 0)));
    i_ = true;
    pc = uint16_t(rd(vector) | (rd(uint16_t(vector + 1)) << 8));
}

void Cpu::adc(uint8_t m)
{
    if (!d_) {
        const unsigned sum = unsigned(a) + m + c_;
        v_ = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
        c_ = uint8_t(sum >> 8);
        a = uint8_t(sum);
        n_ = z_ = a;
        return;
    }
    // NMOS decimal: Z from the binary sum, N and V from the sum after the
    // low digit's correction only, C from the corrected result.
    z_ = uint8_t(a + m + c_);
    int lo = (a & 0x0f) + (m & 0x0f) + c_;
    if (lo >= 0x0a) lo = ((lo + 0x06) & 0x0f) + 0x10;
    int r = (a & 0xf0) + (m & 0xf0) + lo;
    const int sr = int(int8_t(a & 0xf0)) + int(int8_t(m & 0xf0)) + lo;
    n_ = uint8_t(r);
    v_ = sr < -128 || sr > 127;
    if (r >= 0xa0) r += 0x60;
    c_ = r >= 0x100;
    a = uint8_t(r);
}

void Cpu::sbc(uint8_t m)
{
    // Flags always from the binary difference (NMOS).
    const unsigned diff = unsigned(a) - m - (1 - c_);
    v_ = ((a ^ m) & (a ^ diff) & 0x80) != 0;
    n_ = z_ = uint8_t(diff);
    const uint8_t carry = diff < 0x100;
    if (d_) {
        int lo = (a & 0x0f) - (m & 0x0f) + c_ - 1;
        if (lo < 0) lo = ((lo - 0x06) & 0x0f) - 0x10;
        int r = (a & 0xf0) - (m & 0xf0) + lo;
        if (r < 0) r -= 0x60;
        a = uint8_t(r);
    } else {
        a = uint8_t(diff);
    }
    c_ = carry;
}


// Effective addresses. `cyc` collects the page-cross extra of reads.
#define ZP()    uint16_t(fetch())
#define ZPX()   uint16_t(uint8_t(fetch() + x))
#define ZPY()   uint16_t(uint8_t(fetch() + y))
#define ABS()   fetch16()
#define ABSX()  uint16_t(fetch16() + x)
#define ABSY()  uint16_t(fetch16() + y)
#define INDX()  ([&] { const uint8_t z = uint8_t(fetch() + x); \
                       return uint16_t(rd(z) | (rd(uint8_t(z + 1)) << 8)); }())
#define INDY()  uint16_t(([&] { const uint8_t z = fetch(); \
                       return uint16_t(rd(z) | (rd(uint8_t(z + 1)) << 8)); }()) + y)
// The read forms: + 1 cycle when indexing crosses a page.
#define ABSX_R() ([&] { const uint16_t b = fetch16(); const uint16_t e = uint16_t(b + x); \
                        cyc += ((b ^ e) >> 8) != 0; return e; }())
#define ABSY_R() ([&] { const uint16_t b = fetch16(); const uint16_t e = uint16_t(b + y); \
                        cyc += ((b ^ e) >> 8) != 0; return e; }())
#define INDY_R() ([&] { const uint8_t z = fetch(); \
                        const uint16_t b = uint16_t(rd(z) | (rd(uint8_t(z + 1)) << 8)); \
                        const uint16_t e = uint16_t(b + y); cyc += ((b ^ e) >> 8) != 0; return e; }())

#define NZ(v)   (n_ = z_ = (v))

// The eight read modes of a group-1 opcode column (ORA AND EOR ADC LDA CMP SBC).
#define G1(base, OP) \
    case base + 0x01: { const uint8_t m = rd(INDX());   OP; break; } \
    case base + 0x05: { const uint8_t m = rd(ZP());     OP; break; } \
    case base + 0x09: { const uint8_t m = fetch();      OP; break; } \
    case base + 0x0d: { const uint8_t m = rd(ABS());    OP; break; } \
    case base + 0x11: { const uint8_t m = rd(INDY_R()); OP; break; } \
    case base + 0x15: { const uint8_t m = rd(ZPX());    OP; break; } \
    case base + 0x19: { const uint8_t m = rd(ABSY_R()); OP; break; } \
    case base + 0x1d: { const uint8_t m = rd(ABSX_R()); OP; break; }

// Read-modify-write on memory: zp, abs, zp,X, abs,X (+ the accumulator form).
#define RMW(base, FN) \
    case base + 0x06: { const uint16_t e = ZP();   wr(e, FN(rd(e))); break; } \
    case base + 0x0e: { const uint16_t e = ABS();  wr(e, FN(rd(e))); break; } \
    case base + 0x16: { const uint16_t e = ZPX();  wr(e, FN(rd(e))); break; } \
    case base + 0x1e: { const uint16_t e = ABSX(); wr(e, FN(rd(e))); break; }

#define BRANCH(op, cond) \
    case op: { const int8_t d = int8_t(fetch()); \
               if (cond) { const uint16_t t = uint16_t(pc + d); cyc += 1 + (((t ^ pc) >> 8) != 0); pc = t; } \
               break; }

EMU_HOT int Cpu::run(int cycles)
{
    done_ = 0;
    target_ = cycles;
    while (done_ < target_) {
        if (check_) {
            if (nmi_pending_) {
                nmi_pending_ = false;
                interrupt(0xfffa, false);
                done_ += 7;
                continue;
            }
            if (irq_line_ && !i_ && !irq_delay_) {
                interrupt(0xfffe, false);
                done_ += 7;
                continue;
            }
            irq_delay_ = false;
            check_ = irq_line_;
        }

        uint8_t op = rd(pc);
        if (opcode_hook) {
            op = opcode_hook(ctx, pc, op, wrote_);
            wrote_ = false;
        }
        ++pc;
        int cyc = kCycles[op];
        switch (op) {
        G1(0x00, NZ(a |= m))
        G1(0x20, NZ(a &= m))
        G1(0x40, NZ(a ^= m))
        G1(0x60, adc(m))
        G1(0xa0, NZ(a = m))
        G1(0xc0, cmp(a, m))
        G1(0xe0, sbc(m))

        // STA
        case 0x81: wr(INDX(), a); break;
        case 0x85: wr(ZP(), a); break;
        case 0x8d: wr(ABS(), a); break;
        case 0x91: wr(INDY(), a); break;
        case 0x95: wr(ZPX(), a); break;
        case 0x99: wr(ABSY(), a); break;
        case 0x9d: wr(ABSX(), a); break;
        // STX / STY
        case 0x86: wr(ZP(), x); break;
        case 0x8e: wr(ABS(), x); break;
        case 0x96: wr(ZPY(), x); break;
        case 0x84: wr(ZP(), y); break;
        case 0x8c: wr(ABS(), y); break;
        case 0x94: wr(ZPX(), y); break;
        // LDX
        case 0xa2: NZ(x = fetch()); break;
        case 0xa6: NZ(x = rd(ZP())); break;
        case 0xae: NZ(x = rd(ABS())); break;
        case 0xb6: NZ(x = rd(ZPY())); break;
        case 0xbe: NZ(x = rd(ABSY_R())); break;
        // LDY
        case 0xa0: NZ(y = fetch()); break;
        case 0xa4: NZ(y = rd(ZP())); break;
        case 0xac: NZ(y = rd(ABS())); break;
        case 0xb4: NZ(y = rd(ZPX())); break;
        case 0xbc: NZ(y = rd(ABSX_R())); break;
        // CPX / CPY
        case 0xe0: cmp(x, fetch()); break;
        case 0xe4: cmp(x, rd(ZP())); break;
        case 0xec: cmp(x, rd(ABS())); break;
        case 0xc0: cmp(y, fetch()); break;
        case 0xc4: cmp(y, rd(ZP())); break;
        case 0xcc: cmp(y, rd(ABS())); break;
        // BIT
        case 0x24: { const uint8_t m = rd(ZP());  n_ = m; v_ = (m & 0x40) != 0; z_ = a & m; break; }
        case 0x2c: { const uint8_t m = rd(ABS()); n_ = m; v_ = (m & 0x40) != 0; z_ = a & m; break; }

        // Shifts and rotates
        case 0x0a: a = asl(a); break;
        case 0x2a: a = rol(a); break;
        case 0x4a: a = lsr(a); break;
        case 0x6a: a = ror(a); break;
        RMW(0x00, asl)
        RMW(0x20, rol)
        RMW(0x40, lsr)
        RMW(0x60, ror)
        // INC / DEC
        case 0xe6: { const uint16_t e = ZP();   const uint8_t v = uint8_t(rd(e) + 1); NZ(v); wr(e, v); break; }
        case 0xee: { const uint16_t e = ABS();  const uint8_t v = uint8_t(rd(e) + 1); NZ(v); wr(e, v); break; }
        case 0xf6: { const uint16_t e = ZPX();  const uint8_t v = uint8_t(rd(e) + 1); NZ(v); wr(e, v); break; }
        case 0xfe: { const uint16_t e = ABSX(); const uint8_t v = uint8_t(rd(e) + 1); NZ(v); wr(e, v); break; }
        case 0xc6: { const uint16_t e = ZP();   const uint8_t v = uint8_t(rd(e) - 1); NZ(v); wr(e, v); break; }
        case 0xce: { const uint16_t e = ABS();  const uint8_t v = uint8_t(rd(e) - 1); NZ(v); wr(e, v); break; }
        case 0xd6: { const uint16_t e = ZPX();  const uint8_t v = uint8_t(rd(e) - 1); NZ(v); wr(e, v); break; }
        case 0xde: { const uint16_t e = ABSX(); const uint8_t v = uint8_t(rd(e) - 1); NZ(v); wr(e, v); break; }
        case 0xe8: NZ(++x); break;
        case 0xc8: NZ(++y); break;
        case 0xca: NZ(--x); break;
        case 0x88: NZ(--y); break;

        // Transfers
        case 0xaa: NZ(x = a); break;
        case 0xa8: NZ(y = a); break;
        case 0x8a: NZ(a = x); break;
        case 0x98: NZ(a = y); break;
        case 0xba: NZ(x = s); break;
        case 0x9a: s = x; break;

        // Stack
        case 0x48: push(a); break;
        case 0x68: NZ(a = pull()); break;
        case 0x08: push(uint8_t(p() | BF)); break;
        case 0x28: {
            const bool was = i_;
            set_p(pull());
            if (was && !i_) irq_delay_ = check_ = true;     // CLI / PLP: the next instruction runs first
            break;
        }

        // Flags
        case 0x18: c_ = 0; break;
        case 0x38: c_ = 1; break;
        case 0x58: if (i_) irq_delay_ = check_ = true; i_ = false; break;
        case 0x78: i_ = true; break;
        case 0xb8: v_ = false; break;
        case 0xd8: d_ = false; break;
        case 0xf8: d_ = true; break;

        // Branches
        BRANCH(0x10, !(n_ & 0x80))
        BRANCH(0x30, n_ & 0x80)
        BRANCH(0x50, !v_)
        BRANCH(0x70, v_)
        BRANCH(0x90, !c_)
        BRANCH(0xb0, c_)
        BRANCH(0xd0, z_)
        BRANCH(0xf0, !z_)

        // Jumps, calls, returns
        case 0x4c: pc = fetch16(); break;
        case 0x6c: {
            const uint16_t ptr = fetch16();
            // NMOS: the high byte comes from the same page (JMP ($10FF) reads 10FF, 1000).
            pc = uint16_t(rd(ptr) | (rd(uint16_t((ptr & 0xff00) | uint8_t(ptr + 1))) << 8));
            break;
        }
        case 0x20: {
            const uint16_t t = fetch16();
            const uint16_t ret = uint16_t(pc - 1);
            push(uint8_t(ret >> 8));
            push(uint8_t(ret));
            pc = t;
            break;
        }
        case 0x60: {
            const uint8_t lo = pull();
            pc = uint16_t((lo | (pull() << 8)) + 1);
            break;
        }
        case 0x40: {
            set_p(pull());
            const uint8_t lo = pull();
            pc = uint16_t(lo | (pull() << 8));
            break;
        }
        case 0x00:
            ++pc;                                   // BRK skips a padding byte
            interrupt(0xfffe, true);
            break;

        case 0xea: break;                           // NOP

        default:
            ++illegal_count;
            pc = uint16_t(pc + illegal_operands(op));
            break;
        }
        done_ += cyc;
    }
    return done_;
}

#undef ZP
#undef ZPX
#undef ZPY
#undef ABS
#undef ABSX
#undef ABSY
#undef INDX
#undef INDY
#undef ABSX_R
#undef ABSY_R
#undef INDY_R
#undef NZ
#undef G1
#undef RMW
#undef BRANCH

} // namespace m6502
