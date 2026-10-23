// MCS-48 CPU interpreter - see i8035.h.
//
// Dispatch: one switch over the opcode. Each instruction's machine cycles
// are counted before it runs, so callbacks (a DAC on P1) see it finished,
// and the timer / counter advance first, as on the chip.
#include "i8035.h"

namespace i8035 {

namespace {

// Machine cycles of each opcode: 2 for immediates, jumps, calls, returns,
// MOVX / MOVP, port and expander I/O, DJNZ; 1 for the rest (undefined
// opcodes run as 1-cycle NOPs).
struct Cycles {
    uint8_t c[256];
    constexpr Cycles() : c()
    {
        for (int op = 0; op < 256; ++op) c[op] = 1;
        const uint8_t two[] = {
            0x02, 0x03, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x0e, 0x0f, 0x13, 0x16, 0x23, 0x26, 0x36,
            0x39, 0x3a, 0x3c, 0x3d, 0x3e, 0x3f, 0x43, 0x46, 0x53, 0x56, 0x76, 0x80, 0x81, 0x83,
            0x86, 0x88, 0x89, 0x8a, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x93, 0x96, 0x98, 0x99,
            0x9a, 0x9c, 0x9d, 0x9e, 0x9f, 0xa3, 0xb0, 0xb1, 0xb3, 0xb6, 0xc6, 0xd3, 0xe3, 0xe6,
            0xf6,
        };
        for (uint8_t op : two) c[op] = 2;
        for (int p = 0; p < 8; ++p) {
            c[(p << 5) | 0x04] = 2;      // JMP
            c[(p << 5) | 0x14] = 2;      // CALL
            c[(p << 5) | 0x12] = 2;      // JBb
        }
        for (int r = 0; r < 8; ++r) {
            c[0xb8 + r] = 2;             // MOV Rr,#data
            c[0xe8 + r] = 2;             // DJNZ Rr
        }
    }
};
constexpr Cycles kCycles{};

uint8_t default_read(void*, uint8_t) { return 0xff; }
uint8_t default_test(void*, uint8_t) { return 0; }
void default_write(void*, uint8_t, uint8_t) {}

} // namespace

Cpu::Cpu()
{
    ext_read = default_read;
    ext_write = default_write;
    port_in = default_read;
    port_out = default_write;
    test_in = default_test;
    reset();
}

// The chip's reset: PC, PSW (bank 0, SP 0), A11, both interrupts, the timer
// and the flags cleared; ports high. RAM and A keep their contents.
void Cpu::reset()
{
    pc = 0;
    psw = 0;
    a11 = 0;
    p1 = p2 = 0xff;
    bus = 0xff;
    f1 = false;
    xirq_enabled = tirq_enabled = false;
    in_irq = false;
    timer_flag = timer_overflow = false;
    count_mode = 0;
    prescaler = 0;
    t1_last_ = false;
    update_irq();
}

// Stack entry n (n = SP) at RAM 8 + 2n: PC bits 0-7, then PC bits 8-11 and
// the PSW's top four bits (CY AC F0 BS).
void Cpu::push_pc()
{
    const int sp = psw & 7;
    ram[8 + 2 * sp] = uint8_t(pc);
    ram[9 + 2 * sp] = uint8_t(((pc >> 8) & 0x0f) | (psw & 0xf0));
    psw = uint8_t((psw & 0xf8) | ((sp + 1) & 7));
}

void Cpu::pop_pc(bool restore_psw)
{
    const int sp = (psw - 1) & 7;
    const uint8_t hi = ram[9 + 2 * sp];
    pc = uint16_t((ram[8 + 2 * sp] | (hi << 8)) & (in_irq ? 0x7ff : 0xfff));
    psw = restore_psw ? uint8_t((hi & 0xf0) | sp) : uint8_t((psw & 0xf8) | sp);
}

inline void Cpu::add(uint8_t v, uint8_t carry)
{
    const unsigned res = unsigned(a) + v + carry;
    const unsigned lo = unsigned(a & 0x0f) + (v & 0x0f) + carry;
    psw = uint8_t((psw & ~(CY | AC)) | (res > 0xff ? CY : 0) | (lo > 0x0f ? AC : 0));
    a = uint8_t(res);
}

// Decimal adjust: +6 when the low digit is over 9 or AC is set (a carry out
// sets CY), then +0x60 when the high digit is over 9 or CY is set.
inline void Cpu::da()
{
    if ((a & 0x0f) > 9 || (psw & AC)) {
        if (a > 0xf9) psw |= CY;
        a = uint8_t(a + 0x06);
    }
    if ((a & 0xf0) > 0x90 || (psw & CY)) {
        a = uint8_t(a + 0x60);
        psw |= CY;
    }
}

// Timer: +1 every 32 cycles (the run loop adds to the prescaler and calls
// this when it reaches 32). Counter: +1 on each falling edge of T1
// (sampled once an instruction). An overflow sets the JTF flag, and a
// pending interrupt only while the timer interrupt is enabled.
void Cpu::count()
{
    bool over = false;
    if (count_mode == 1) {
        const uint8_t old = timer;
        timer = uint8_t(timer + (prescaler >> 5));
        prescaler &= 31;
        over = timer < old;
    } else {
        const bool t1 = test(1);
        if (t1_last_ && !t1) over = ++timer == 0;
        t1_last_ = t1;
    }
    if (over) {
        timer_flag = true;
        if (tirq_enabled) timer_overflow = true;
        update_irq();
    }
}

// A CALL to 3 (external, first) or 7 (timer); 2 cycles. Nothing else is
// taken until RETR.
int Cpu::take_irq()
{
    push_pc();
    in_irq = true;
    if (irq_line_ && xirq_enabled) {
        pc = 0x003;
    } else {
        pc = 0x007;
        timer_overflow = false;
    }
    update_irq();
    return 2;
}

// Registers R0-R7 of the selected bank; @R0 / @R1 address the RAM (64 or
// 128 bytes, ram_mask).
#define R(n) ram[((psw & BS) ? 24 : 0) + (n)]
#define AT(n) ram[R(n) & ram_mask]

EMU_HOT int Cpu::run(int cycles)
{
    done_ = 0;
    target_ = cycles;
    update_irq();                           // (tests may set the flags between runs)
    while (done_ < target_) {
        int c;
        uint8_t op;
        if (__builtin_expect(irq_ready_, 0)) {
            c = take_irq();
            op = 0;                         // runs as a NOP below
        } else {
            op = fetch();
            c = kCycles.c[op];
        }
        done_ += c;
        if (count_mode) {
            if (count_mode == 1) {
                prescaler = uint8_t(prescaler + c);
                if (prescaler >= 32) count();
            } else {
                count();
            }
        }

        switch (op) {
        // --- jumps and calls (bits 7-5 = the page) ----------------------------
        case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xa4: case 0xc4: case 0xe4:
            jump(uint16_t(((op & 0xe0) << 3) | fetch()));
            break;
        case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xb4: case 0xd4: case 0xf4: {
            const uint8_t lo = fetch();
            call(uint16_t(((op & 0xe0) << 3) | lo));
            break;
        }
        case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xb2: case 0xd2: case 0xf2:
            branch((a >> (op >> 5)) & 1);       // JBb
            break;
        case 0x16: { const bool t = timer_flag; timer_flag = false; branch(t); break; }   // JTF
        case 0x26: branch(!test(0)); break;     // JNT0
        case 0x36: branch(test(0)); break;      // JT0
        case 0x46: branch(!test(1)); break;     // JNT1
        case 0x56: branch(test(1)); break;      // JT1
        case 0x76: branch(f1); break;           // JF1
        case 0x86: branch(irq_line_); break;    // JNI: the INT pin is low
        case 0x96: branch(a != 0); break;       // JNZ
        case 0xb6: branch(psw & F0); break;     // JF0
        case 0xc6: branch(a == 0); break;       // JZ
        case 0xe6: branch(!(psw & CY)); break;  // JNC
        case 0xf6: branch(psw & CY); break;     // JC
        case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
            branch(--R(op & 7) != 0);           // DJNZ
            break;
        case 0xb3: {                            // JMPP @A
            const uint16_t page = pc & 0xf00;
            pc = uint16_t(page | rom[(page | a) & rom_mask]);
            break;
        }
        case 0x83: pop_pc(false); break;        // RET
        case 0x93: in_irq = false; pop_pc(true); update_irq(); break;   // RETR

        // --- accumulator ----------------------------------------------------------
        case 0x03: add(fetch(), 0); break;
        case 0x13: add(fetch(), (psw & CY) ? 1 : 0); break;
        case 0x60: case 0x61: add(AT(op & 1), 0); break;
        case 0x70: case 0x71: add(AT(op & 1), (psw & CY) ? 1 : 0); break;
        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
            add(R(op & 7), 0);
            break;
        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            add(R(op & 7), (psw & CY) ? 1 : 0);
            break;
        case 0x43: a |= fetch(); break;
        case 0x40: case 0x41: a |= AT(op & 1); break;
        case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            a |= R(op & 7);
            break;
        case 0x53: a &= fetch(); break;
        case 0x50: case 0x51: a &= AT(op & 1); break;
        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            a &= R(op & 7);
            break;
        case 0xd3: a ^= fetch(); break;
        case 0xd0: case 0xd1: a ^= AT(op & 1); break;
        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
            a ^= R(op & 7);
            break;
        case 0x07: --a; break;
        case 0x17: ++a; break;
        case 0x27: a = 0; break;
        case 0x37: a = uint8_t(~a); break;
        case 0x47: a = uint8_t((a << 4) | (a >> 4)); break;                 // SWAP
        case 0x57: da(); break;
        case 0x77: a = uint8_t((a >> 1) | (a << 7)); break;                  // RR
        case 0xe7: a = uint8_t((a << 1) | (a >> 7)); break;                  // RL
        case 0x67: {                                                          // RRC
            const uint8_t c = uint8_t(a << 7);
            a = uint8_t((a >> 1) | (psw & CY));
            psw = uint8_t((psw & ~CY) | c);
            break;
        }
        case 0xf7: {                                                          // RLC
            const uint8_t c = a & 0x80;
            a = uint8_t((a << 1) | (psw >> 7));
            psw = uint8_t((psw & ~CY) | c);
            break;
        }

        // --- moves ------------------------------------------------------------------
        case 0x23: a = fetch(); break;
        case 0xf0: case 0xf1: a = AT(op & 1); break;
        case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
            a = R(op & 7);
            break;
        case 0xa0: case 0xa1: AT(op & 1) = a; break;
        case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
            R(op & 7) = a;
            break;
        case 0xb0: case 0xb1: { const uint8_t v = fetch(); AT(op & 1) = v; break; }
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf: {
            const uint8_t v = fetch();
            R(op & 7) = v;
            break;
        }
        case 0x20: case 0x21: { uint8_t& m = AT(op & 1); const uint8_t t = m; m = a; a = t; break; }
        case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
            uint8_t& m = R(op & 7);
            const uint8_t t = m;
            m = a;
            a = t;
            break;
        }
        case 0x30: case 0x31: {                                               // XCHD
            uint8_t& m = AT(op & 1);
            const uint8_t t = m;
            m = uint8_t((m & 0xf0) | (a & 0x0f));
            a = uint8_t((a & 0xf0) | (t & 0x0f));
            break;
        }
        case 0x10: case 0x11: ++AT(op & 1); break;
        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
            ++R(op & 7);
            break;
        case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
            --R(op & 7);
            break;
        case 0xc7: a = uint8_t(psw | 0x08); break;                           // MOV A,PSW
        case 0xd7: psw = uint8_t(a & ~0x08); break;                          // MOV PSW,A
        case 0x42: a = timer; break;
        case 0x62: timer = a; break;
        case 0xa3: a = rom[((pc & 0xf00) | a) & rom_mask]; break;           // MOVP A,@A
        case 0xe3: a = rom[(0x300 | a) & rom_mask]; break;                   // MOVP3 A,@A
        case 0x80: case 0x81: a = ext_read(ctx, R(op & 1)); break;           // MOVX A,@Rr
        case 0x90: case 0x91: ext_write(ctx, R(op & 1), a); break;           // MOVX @Rr,A

        // --- ports --------------------------------------------------------------------
        case 0x08: a = in(kBus); break;                                      // INS A,BUS
        case 0x09: a = uint8_t(in(kP1) & p1); break;
        case 0x0a: a = uint8_t(in(kP2) & p2); break;
        case 0x02: bus = a; out(kBus, a); break;                             // OUTL BUS,A
        case 0x39: p1 = a; out(kP1, p1); break;
        case 0x3a: p2 = a; out(kP2, p2); break;
        case 0x88: bus |= fetch(); out(kBus, bus); break;
        case 0x89: p1 |= fetch(); out(kP1, p1); break;
        case 0x8a: p2 |= fetch(); out(kP2, p2); break;
        case 0x98: bus &= fetch(); out(kBus, bus); break;
        case 0x99: p1 &= fetch(); out(kP1, p1); break;
        case 0x9a: p2 &= fetch(); out(kP2, p2); break;
        case 0x0c: case 0x0d: case 0x0e: case 0x0f: a = 0; break;           // MOVD A,Pp: no 8243
        case 0x3c: case 0x3d: case 0x3e: case 0x3f:                          // MOVD / ANLD / ORLD
        case 0x8c: case 0x8d: case 0x8e: case 0x8f:
        case 0x9c: case 0x9d: case 0x9e: case 0x9f:
            break;

        // --- flags and control -------------------------------------------------------
        case 0x97: psw &= uint8_t(~CY); break;
        case 0xa7: psw ^= CY; break;
        case 0x85: psw &= uint8_t(~F0); break;
        case 0x95: psw ^= F0; break;
        case 0xa5: f1 = false; break;
        case 0xb5: f1 = !f1; break;
        case 0xc5: psw &= uint8_t(~BS); break;
        case 0xd5: psw |= BS; break;
        case 0xe5: a11 = 0; break;
        case 0xf5: a11 = 0x800; break;
        case 0x05: xirq_enabled = true; update_irq(); break;
        case 0x15: xirq_enabled = false; update_irq(); break;
        case 0x25: tirq_enabled = true; update_irq(); break;
        case 0x35: tirq_enabled = false; timer_overflow = false; update_irq(); break;
        case 0x45: if (count_mode != 2) t1_last_ = test(1); count_mode = 2; break;   // STRT CNT
        case 0x55: count_mode = 1; prescaler = 0; break;                     // STRT T
        case 0x65: count_mode = 0; break;                                    // STOP TCNT
        case 0x75: break;                                                    // ENT0 CLK

        default: break;                                                      // NOP, undefined
        }
    }
    return done_;
}

#undef R
#undef AT

} // namespace i8035
