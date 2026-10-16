// Motorola 6800 interpreter - see m6800.h.
#include "m6800.h"

namespace m6800 {

namespace {

// Cycles per opcode (datasheet). 0 = undocumented: run as a no-op.
constexpr uint8_t kCycles[256] = {
    //0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    0, 2, 0, 0, 0, 0, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2,      // 0
    2, 2, 0, 0, 0, 0, 2, 2, 0, 2, 0, 2, 0, 0, 0, 0,      // 1
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,      // 2
    4, 4, 4, 4, 4, 4, 4, 4, 0, 5, 0, 10, 0, 0, 9, 12,    // 3
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,      // 4
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,      // 5
    7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 4, 7,      // 6
    6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,      // 7
    2, 2, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 3, 8, 3, 0,      // 8
    3, 3, 3, 0, 3, 3, 3, 4, 3, 3, 3, 3, 4, 0, 4, 5,      // 9
    5, 5, 5, 0, 5, 5, 5, 6, 5, 5, 5, 5, 6, 8, 6, 7,      // A
    4, 4, 4, 0, 4, 4, 4, 5, 4, 4, 4, 4, 5, 9, 5, 6,      // B
    2, 2, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 0, 0, 3, 0,      // C
    3, 3, 3, 0, 3, 3, 3, 4, 3, 3, 3, 3, 0, 0, 4, 5,      // D
    5, 5, 5, 0, 5, 5, 5, 6, 5, 5, 5, 5, 0, 0, 6, 7,      // E
    4, 4, 4, 0, 4, 4, 4, 5, 4, 4, 4, 4, 0, 0, 5, 6,      // F
};

// Operand bytes of the undocumented opcodes (so they're skipped whole).
inline int operand_bytes(uint8_t op)
{
    if (op < 0x60) return 0;
    if (op < 0x80) return op < 0x70 ? 1 : 2;
    switch ((op >> 4) & 3) {
    case 0:  return (op & 0x0f) >= 0x0c ? 2 : 1;       // immediate: 16-bit for C-F
    case 3:  return 2;                                  // extended
    default: return 1;                                  // direct, indexed
    }
}


// Branch conditions: bit k of kBranch[cc & 15] = branch 0x20 + k taken
// (C = 1, V = 2, Z = 4, N = 8 in the flags' low nibble).
struct BranchTable {
    uint16_t m[16];
    constexpr BranchTable() : m()
    {
        for (int f = 0; f < 16; ++f) {
            const bool c = f & 1, v = f & 2, z = f & 4, n = f & 8;
            const bool t[8] = {true, !(c || z), !c, !z, !v, !n, n == v, !z && n == v};
            uint16_t bits = 0;
            for (int k = 0; k < 16; ++k)
                if ((k & 1) ? !t[k >> 1] : t[k >> 1]) bits = uint16_t(bits | (1 << k));
            m[f] = bits;
        }
    }
};
constexpr BranchTable kBranch;

inline bool taken(uint8_t op, uint8_t cc) { return (kBranch.m[cc & 15] >> (op & 15)) & 1; }

} // namespace

Cpu::Cpu()
{
    for (int i = 0; i < kPages; ++i) {
        read_page[i] = nullptr;
        write_page[i] = nullptr;
    }
}

void Cpu::map_read(uint16_t start, uint32_t len, const uint8_t* p)
{
    for (uint32_t off = 0; off < len; off += kPageSize)
        read_page[(start + off) >> kPageShift] = p ? p + off : nullptr;
}

void Cpu::map_write(uint16_t start, uint32_t len, uint8_t* p)
{
    for (uint32_t off = 0; off < len; off += kPageSize)
        write_page[(start + off) >> kPageShift] = p ? p + off : nullptr;
}

void Cpu::reset()
{
    cc = uint8_t(cc | 0xc0 | CC_I);
    wai = false;
    nmi_pending_ = false;
    irq_hold_ = false;
    pc = rd16(kVecReset);
}

void Cpu::push_state()
{
    push16(pc);
    push16(x);
    push8(a);
    push8(b);
    push8(cc);
}

// IRQ / NMI: 12 cycles to stack the state and fetch the vector; 4 when WAI
// had already stacked it.
void Cpu::interrupt(uint16_t vec)
{
    if (wai) {
        wai = false;
        done_ += 4;
    } else {
        push_state();
        done_ += 12;
    }
    cc |= CC_I;
    pc = rd16(vec);
}

int Cpu::run(int cycles)
{
    done_ = 0;
    target_ = cycles;
    while (done_ < target_) {
        if (nmi_pending_) {
            nmi_pending_ = false;
            interrupt(kVecNmi);
            continue;
        }
        if (irq_line_ && !(cc & CC_I) && !irq_hold_) {
            interrupt(kVecIrq);
            continue;
        }
        irq_hold_ = false;
        if (wai) {
            if (done_ < target_) done_ = target_;
            break;
        }
        exec(fetch());
    }
    return done_;
}

uint8_t Cpu::op_sub(uint8_t x1, uint8_t x2, int carry)
{
    const unsigned res = unsigned(x1) - x2 - unsigned(carry);
    const uint8_t r = uint8_t(res);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z)
                 | (((x1 ^ x2) & (x1 ^ r) & 0x80) ? CC_V : 0) | (res & 0x100 ? CC_C : 0));
    return r;
}

uint8_t Cpu::op_add(uint8_t x1, uint8_t x2, int carry)
{
    const unsigned res = unsigned(x1) + x2 + unsigned(carry);
    const uint8_t r = uint8_t(res);
    cc = uint8_t((cc & ~(CC_H | CC_N | CC_Z | CC_V | CC_C)) | ((x1 ^ x2 ^ res) & 0x10 ? CC_H : 0)
                 | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | ((~(x1 ^ x2) & (x1 ^ r) & 0x80) ? CC_V : 0)
                 | (res & 0x100 ? CC_C : 0));
    return r;
}

// NEG COM LSR ROR ASR ASL ROL DEC INC CLR (TST and JMP are done apart).
// The shifts and rotates set V = N xor C.
uint8_t Cpu::rmw(uint8_t op, uint8_t v)
{
    uint8_t r;
    switch (op & 0x0f) {
    case 0x0:
        r = uint8_t(-v);
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z)
                     | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0));
        return r;
    case 0x3:
        r = uint8_t(~v);
        nz(r);
        cc |= CC_C;
        return r;
    case 0x4:
        r = uint8_t(v >> 1);
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (r ? 0 : CC_Z) | (v & 1 ? CC_C : 0));
        shift_v();
        return r;
    case 0x6:
        r = uint8_t((v >> 1) | (cc & CC_C ? 0x80 : 0));
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v & 1 ? CC_C : 0));
        shift_v();
        return r;
    case 0x7:
        r = uint8_t((v >> 1) | (v & 0x80));
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v & 1 ? CC_C : 0));
        shift_v();
        return r;
    case 0x8:
        r = uint8_t(v << 1);
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v & 0x80 ? CC_C : 0));
        shift_v();
        return r;
    case 0x9:
        r = uint8_t((v << 1) | (cc & CC_C ? 1 : 0));
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v & 0x80 ? CC_C : 0));
        shift_v();
        return r;
    case 0xa:
        r = uint8_t(v - 1);
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v == 0x80 ? CC_V : 0));
        return r;
    case 0xc:
        r = uint8_t(v + 1);
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (r & 0x80 ? CC_N : 0) | (r ? 0 : CC_Z) | (v == 0x7f ? CC_V : 0));
        return r;
    default:                                    // F: CLR
        cc = uint8_t((cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
        return 0;
    }
}

// "DECA (or DECB) ; BNE back to it" - the sound programs' delay loop -
// just taken back to the DEC: runs the whole iterations that the slice
// would run (both instructions starting before its end) at once. Exact:
// nothing else changes inside the loop, and the interrupt lines only
// change between runs.
void Cpu::delay_loop()
{
    const uint8_t dec = rd(pc);
    if (dec != 0x4a && dec != 0x5a) return;
    const int room = target_ - done_ - 2;       // an iteration needs its BNE to start in the slice
    if (room <= 0) return;
    uint8_t& acc = dec == 0x4a ? a : b;
    const int left = acc;                       // the branch was taken: not 0
    int k = (room - 1) / 6 + 1;
    if (k > left) k = left;
    done_ += 6 * k;
    acc = uint8_t(left - k);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (acc & 0x80 ? CC_N : 0) | (acc ? 0 : CC_Z)
                 | (acc == 0x7f ? CC_V : 0));   // DEC from 0x80
    if (!acc) pc = uint16_t(pc + 3);            // the last BNE falls through
}

inline void Cpu::exec(uint8_t op)
{
    const int cyc = kCycles[op];
    if (!cyc) {                                 // undocumented: skip it
        pc = uint16_t(pc + operand_bytes(op));
        done_ += 2;
        return;
    }
    done_ += cyc;
    switch (op) {
    case 0x01: break;                                                   // NOP
    case 0x06: cc = uint8_t(a | 0xc0); irq_hold_ = true; break;         // TAP
    case 0x07: a = uint8_t(cc | 0xc0); break;                           // TPA
    case 0x08: ++x; cc = uint8_t((cc & ~CC_Z) | (x ? 0 : CC_Z)); break; // INX
    case 0x09: --x; cc = uint8_t((cc & ~CC_Z) | (x ? 0 : CC_Z)); break; // DEX
    case 0x0a: cc &= uint8_t(~CC_V); break;
    case 0x0b: cc |= CC_V; break;
    case 0x0c: cc &= uint8_t(~CC_C); break;
    case 0x0d: cc |= CC_C; break;
    case 0x0e:                                                          // CLI
        if (cc & CC_I) irq_hold_ = true;
        cc &= uint8_t(~CC_I);
        break;
    case 0x0f: cc |= CC_I; break;
    case 0x10: a = op_sub(a, b, 0); break;                              // SBA
    case 0x11: op_sub(a, b, 0); break;                                  // CBA
    case 0x16: b = a; nz(b); break;                                     // TAB
    case 0x17: a = b; nz(a); break;                                     // TBA
    case 0x19: {                                                        // DAA
        const uint8_t msn = a & 0xf0, lsn = a & 0x0f;
        unsigned cf = 0;
        if (lsn > 0x09 || (cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 0x09)  cf |= 0x60;
        if (msn > 0x90 || (cc & CC_C)) cf |= 0x60;
        const unsigned t = a + cf;
        a = uint8_t(t);
        nz(a);
        if (t & 0x100) cc |= CC_C;
        break;
    }
    case 0x1b: a = op_add(a, b, 0); break;                              // ABA

    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
        const int8_t o = int8_t(fetch());
        if (taken(op, cc)) {
            // A branch to itself: idle until an interrupt (not when one is
            // already due - after CLI / TAP it waits for this one instruction).
            if (o == -2 && target_ > done_ && !nmi_pending_ && !(irq_line_ && !(cc & CC_I)))
                done_ += (target_ - done_ + 3) / 4 * 4;
            pc = uint16_t(pc + o);
            if (op == 0x26 && o == -3) delay_loop();
        }
        break;
    }

    case 0x30: x = uint16_t(s + 1); break;                              // TSX
    case 0x31: ++s; break;                                              // INS
    case 0x32: a = pull8(); break;                                      // PULA
    case 0x33: b = pull8(); break;                                      // PULB
    case 0x34: --s; break;                                              // DES
    case 0x35: s = uint16_t(x - 1); break;                              // TXS
    case 0x36: push8(a); break;                                         // PSHA
    case 0x37: push8(b); break;                                         // PSHB
    case 0x39: pc = pull16(); break;                                    // RTS
    case 0x3b:                                                          // RTI
        cc = uint8_t(pull8() | 0xc0);
        b = pull8();
        a = pull8();
        x = pull16();
        pc = pull16();
        break;
    case 0x3e: push_state(); wai = true; break;                         // WAI
    case 0x3f: push_state(); cc |= CC_I; pc = rd16(kVecSwi); break;     // SWI

    case 0x40: case 0x43: case 0x44: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
    case 0x4c: case 0x4f:
        a = rmw(op, a);
        break;
    case 0x4d: nz(a); cc &= uint8_t(~CC_C); break;                      // TSTA
    case 0x50: case 0x53: case 0x54: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
    case 0x5c: case 0x5f:
        b = rmw(op, b);
        break;
    case 0x5d: nz(b); cc &= uint8_t(~CC_C); break;
    case 0x60: case 0x63: case 0x64: case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a:
    case 0x6c: case 0x6f: {
        const uint16_t ea = uint16_t(x + fetch());
        wr(ea, rmw(op, rd(ea)));
        break;
    }
    case 0x6d: nz(rd(uint16_t(x + fetch()))); cc &= uint8_t(~CC_C); break;
    case 0x6e: pc = uint16_t(x + fetch()); break;
    case 0x70: case 0x73: case 0x74: case 0x76: case 0x77: case 0x78: case 0x79: case 0x7a:
    case 0x7c: case 0x7f: {
        const uint16_t ea = fetch16();
        wr(ea, rmw(op, rd(ea)));
        break;
    }
    case 0x7d: nz(rd(fetch16())); cc &= uint8_t(~CC_C); break;
    case 0x7e: pc = fetch16(); break;

    case 0x8d: {                                                        // BSR
        const int8_t o = int8_t(fetch());
        push16(pc);
        pc = uint16_t(pc + o);
        break;
    }
    case 0xad: { const uint16_t ea = uint16_t(x + fetch()); push16(pc); pc = ea; break; }   // JSR ,X
    case 0xbd: { const uint16_t ea = fetch16(); push16(pc); pc = ea; break; }              // JSR ext

    default: {
        // 0x80-0xFF: A group (8x-Bx), B group (Cx-Fx); mode by bits 4-5.
        const int mode = (op >> 4) & 3;
        const int low = op & 0x0f;
        const bool wide = low >= 0x0c;          // CPX / LDS / STS, LDX / STX
        uint16_t ea;
        switch (mode) {
        case 0: ea = pc; pc = uint16_t(pc + (wide ? 2 : 1)); break;
        case 1: ea = fetch(); break;
        case 2: ea = uint16_t(x + fetch()); break;
        default: ea = fetch16(); break;
        }
        uint8_t& acc = (op & 0x40) ? b : a;
        switch (low) {
        case 0x0: acc = op_sub(acc, rd(ea), 0); break;
        case 0x1: op_sub(acc, rd(ea), 0); break;
        case 0x2: acc = op_sub(acc, rd(ea), cc & CC_C ? 1 : 0); break;
        case 0x4: acc &= rd(ea); nz(acc); break;
        case 0x5: nz(uint8_t(acc & rd(ea))); break;
        case 0x6: acc = rd(ea); nz(acc); break;
        case 0x7: nz(acc); wr(ea, acc); break;
        case 0x8: acc ^= rd(ea); nz(acc); break;
        case 0x9: acc = op_add(acc, rd(ea), cc & CC_C ? 1 : 0); break;
        case 0xa: acc |= rd(ea); nz(acc); break;
        case 0xb: acc = op_add(acc, rd(ea), 0); break;
        case 0xc: {                             // CPX: N V from the high bytes, Z from all 16 bits
            const uint16_t m = rd16(ea);
            const uint8_t xh = uint8_t(x >> 8), mh = uint8_t(m >> 8);
            const uint8_t rh = uint8_t(xh - mh);
            cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (rh & 0x80 ? CC_N : 0) | (x == m ? CC_Z : 0)
                         | (((xh ^ mh) & (xh ^ rh) & 0x80) ? CC_V : 0));
            break;
        }
        case 0xe:                               // LDS / LDX
            if (op & 0x40) { x = rd16(ea); nz16(x); }
            else           { s = rd16(ea); nz16(s); }
            break;
        case 0xf:                               // STS / STX
            if (op & 0x40) { nz16(x); wr16(ea, x); }
            else           { nz16(s); wr16(ea, s); }
            break;
        default: break;
        }
        break;
    }
    }
}

} // namespace m6800
