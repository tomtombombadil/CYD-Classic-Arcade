// Z80 CPU interpreter - see z80.h.
//
// Dispatch: one switch per opcode table. The unprefixed table is a template
// over the index register (0 = HL, 1 = IX, 2 = IY), so DD / FD opcodes reuse
// it: H / L become IXH / IXL, (HL) becomes (IX+d), and the opcodes that use
// (HL) together with H or L keep the real H / L, as the chip does.
#include "z80.h"

namespace z80 {

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "Pair assumes little-endian");

namespace {

// S, Z, Y, X of a byte result, and the same plus even parity (P/V).
struct FlagTables {
    uint8_t sz53[256];
    uint8_t szp53[256];
    constexpr FlagTables() : sz53(), szp53()
    {
        for (int v = 0; v < 256; ++v) {
            uint8_t f = uint8_t(v & (SF | YF | XF));
            if (v == 0) f |= ZF;
            int p = 0;
            for (int b = 0; b < 8; ++b) p ^= (v >> b) & 1;
            sz53[v] = f;
            szp53[v] = uint8_t(f | (p ? 0 : PF));
        }
    }
};
constexpr FlagTables kFlags{};
#define SZ53(v)  kFlags.sz53[uint8_t(v)]
#define SZP53(v) kFlags.szp53[uint8_t(v)]

// T-states of unprefixed opcodes; conditional jumps / calls / returns list
// the not-taken time (the taken extra is added in the code). CB, DD, ED and
// FD are prefixes (handled apart).
constexpr uint8_t kCycMain[256] = {
    4,10, 7, 6, 4, 4, 7, 4,  4,11, 7, 6, 4, 4, 7, 4,   // 00
    8,10, 7, 6, 4, 4, 7, 4,  7,11, 7, 6, 4, 4, 7, 4,   // 10 (JR e: 7 + 5 taken)
    7,10,16, 6, 4, 4, 7, 4,  7,11,16, 6, 4, 4, 7, 4,   // 20
    7,10,13, 6,11,11,10, 4,  7,11,13, 6, 4, 4, 7, 4,   // 30
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // 40
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // 50
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // 60
    7, 7, 7, 7, 7, 7, 4, 7,  4, 4, 4, 4, 4, 4, 7, 4,   // 70
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // 80
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // 90
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // A0
    4, 4, 4, 4, 4, 4, 7, 4,  4, 4, 4, 4, 4, 4, 7, 4,   // B0
    5,10,10,10,10,11, 7,11,  5,10,10, 0,10,17, 7,11,   // C0
    5,10,10,11,10,11, 7,11,  5, 4,10,11,10, 0, 7,11,   // D0
    5,10,10,19,10,11, 7,11,  5, 4,10, 4,10, 0, 7,11,   // E0
    5,10,10, 4,10,11, 7,11,  5, 6,10, 4,10, 0, 7,11,   // F0
};

// T-states of ED opcodes (including the ED fetch); block repeats add 5.
struct EdCycles {
    uint8_t t[256];
    constexpr EdCycles() : t()
    {
        for (int op = 0; op < 256; ++op) t[op] = 8;      // undefined = 8 T NOP
        for (int op = 0x40; op < 0x80; ++op) {
            const int z = op & 7;
            const uint8_t by_z[8] = { 12, 12, 15, 20, 8, 14, 8, 9 };
            t[op] = by_z[z];
        }
        t[0x67] = t[0x6f] = 18;                          // RRD / RLD
        t[0x77] = t[0x7f] = 8;                           // undefined
        for (int op = 0xa0; op < 0xc0; ++op)
            if ((op & 7) < 4) t[op] = 16;
    }
};
constexpr EdCycles kCycEd{};

uint8_t default_read(void*, uint16_t) { return 0xff; }
void default_write(void*, uint16_t, uint8_t) {}

} // namespace

#define rA af.b.h
#define rF af.b.l
#define rB bc.b.h
#define rC bc.b.l
#define rD de.b.h
#define rE de.b.l
#define rH hl.b.h
#define rL hl.b.l

Cpu::Cpu()
{
    for (int p = 0; p < kPages; ++p) {
        read_page[p] = nullptr;
        write_page[p] = nullptr;
    }
    mem_read = default_read;
    mem_write = default_write;
    port_in = default_read;
    port_out = default_write;
    reset();
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
    pc.w = 0;
    sp.w = 0xffff;
    af.w = 0xffff;
    bc.w = de.w = hl.w = 0;
    ix.w = iy.w = 0xffff;
    af2.w = 0xffff;
    bc2.w = de2.w = hl2.w = 0;
    i = r = r7 = 0;
    iff1 = iff2 = 0;
    im = 0;
    halted = false;
    irq_line_ = false;
    nmi_pending_ = false;
    ei_delay_ = false;
}

// --- ALU -------------------------------------------------------------------

Z80_INLINE void Cpu::add8(uint8_t v)
{
    const unsigned res = unsigned(rA) + v;
    rF = uint8_t(SZ53(res) | ((res >> 8) & CF) | ((rA ^ res ^ v) & HF) |
                 ((((v ^ rA ^ 0x80) & (v ^ res)) >> 5) & PF));
    rA = uint8_t(res);
}

Z80_INLINE void Cpu::adc8(uint8_t v)
{
    const unsigned res = unsigned(rA) + v + (rF & CF);
    rF = uint8_t(SZ53(res) | ((res >> 8) & CF) | ((rA ^ res ^ v) & HF) |
                 ((((v ^ rA ^ 0x80) & (v ^ res)) >> 5) & PF));
    rA = uint8_t(res);
}

Z80_INLINE void Cpu::sub8(uint8_t v)
{
    const unsigned res = unsigned(rA) - v;
    rF = uint8_t(SZ53(res) | NF | ((res >> 8) & CF) | ((rA ^ res ^ v) & HF) |
                 ((((v ^ rA) & (rA ^ res)) >> 5) & PF));
    rA = uint8_t(res);
}

Z80_INLINE void Cpu::sbc8(uint8_t v)
{
    const unsigned res = unsigned(rA) - v - (rF & CF);
    rF = uint8_t(SZ53(res) | NF | ((res >> 8) & CF) | ((rA ^ res ^ v) & HF) |
                 ((((v ^ rA) & (rA ^ res)) >> 5) & PF));
    rA = uint8_t(res);
}

// CP: like SUB without storing, but X / Y come from the operand.
Z80_INLINE void Cpu::cp8(uint8_t v)
{
    const unsigned res = unsigned(rA) - v;
    rF = uint8_t((SZ53(res) & (SF | ZF)) | (v & (XF | YF)) | NF | ((res >> 8) & CF) |
                 ((rA ^ res ^ v) & HF) | ((((v ^ rA) & (rA ^ res)) >> 5) & PF));
}

Z80_INLINE void Cpu::and8(uint8_t v) { rA &= v; rF = uint8_t(SZP53(rA) | HF); }
Z80_INLINE void Cpu::xor8(uint8_t v) { rA ^= v; rF = SZP53(rA); }
Z80_INLINE void Cpu::or8(uint8_t v)  { rA |= v; rF = SZP53(rA); }

Z80_INLINE uint8_t Cpu::inc8(uint8_t v)
{
    const uint8_t res = uint8_t(v + 1);
    rF = uint8_t((rF & CF) | SZ53(res) | (res == 0x80 ? PF : 0) | ((res & 0x0f) ? 0 : HF));
    return res;
}

Z80_INLINE uint8_t Cpu::dec8(uint8_t v)
{
    const uint8_t res = uint8_t(v - 1);
    rF = uint8_t((rF & CF) | NF | SZ53(res) | (res == 0x7f ? PF : 0) | ((v & 0x0f) ? 0 : HF));
    return res;
}

// ADD HL/IX/IY,rr: S Z P kept; H from bit 11, C from bit 15, X Y from the
// high byte of the result.
Z80_INLINE void Cpu::add16(Pair& d, uint16_t v)
{
    const uint32_t res = uint32_t(d.w) + v;
    rF = uint8_t((rF & (SF | ZF | PF)) | (((d.w ^ res ^ v) >> 8) & HF) |
                 ((res >> 16) & CF) | ((res >> 8) & (XF | YF)));
    d.w = uint16_t(res);
}

Z80_INLINE void Cpu::adc16(uint16_t v)
{
    const uint32_t res = uint32_t(hl.w) + v + (rF & CF);
    rF = uint8_t(((res >> 8) & (SF | XF | YF)) | ((res & 0xffff) ? 0 : ZF) |
                 (((hl.w ^ res ^ v) >> 8) & HF) | ((res >> 16) & CF) |
                 ((((v ^ hl.w ^ 0x8000) & (v ^ res)) >> 13) & PF));
    hl.w = uint16_t(res);
}

Z80_INLINE void Cpu::sbc16(uint16_t v)
{
    const uint32_t res = uint32_t(hl.w) - v - (rF & CF);
    rF = uint8_t(((res >> 8) & (SF | XF | YF)) | ((res & 0xffff) ? 0 : ZF) | NF |
                 (((hl.w ^ res ^ v) >> 8) & HF) | ((res >> 16) & CF) |
                 ((((v ^ hl.w) & (hl.w ^ res)) >> 13) & PF));
    hl.w = uint16_t(res);
}

// CB rotates / shifts: RLC RRC RL RR SLA SRA SLL SRL.
Z80_INLINE uint8_t Cpu::shift_op(int kind, uint8_t v)
{
    uint8_t res, c;
    switch (kind) {
    case 0:  c = v >> 7; res = uint8_t((v << 1) | c); break;
    case 1:  c = v & 1;  res = uint8_t((v >> 1) | (c << 7)); break;
    case 2:  c = v >> 7; res = uint8_t((v << 1) | (rF & CF)); break;
    case 3:  c = v & 1;  res = uint8_t((v >> 1) | ((rF & CF) << 7)); break;
    case 4:  c = v >> 7; res = uint8_t(v << 1); break;
    case 5:  c = v & 1;  res = uint8_t((v >> 1) | (v & 0x80)); break;
    case 6:  c = v >> 7; res = uint8_t((v << 1) | 1); break;      // SLL (undocumented)
    default: c = v & 1;  res = uint8_t(v >> 1); break;
    }
    rF = uint8_t(SZP53(res) | c);
    return res;
}

// BIT n: Z and P/V = bit clear, S only for bit 7 set, H set, C kept;
// X / Y from `xy_src` (the register, or the address high byte for (IX+d)).
Z80_INLINE void Cpu::bit_op(int n, uint8_t v, uint8_t xy_src)
{
    const uint8_t b = uint8_t(v & (1 << n));
    rF = uint8_t((rF & CF) | HF | (b ? (b & SF) : (ZF | PF)) | (xy_src & (XF | YF)));
}

inline void Cpu::daa()
{
    uint8_t diff = 0, c = rF & CF;
    const uint8_t lo = rA & 0x0f;
    if ((rF & HF) || lo > 9) diff |= 0x06;
    if (c || rA > 0x99) { diff |= 0x60; c = CF; }
    uint8_t h;
    if (rF & NF) {
        h = ((rF & HF) && lo < 6) ? HF : 0;
        rA = uint8_t(rA - diff);
    } else {
        h = lo > 9 ? HF : 0;
        rA = uint8_t(rA + diff);
    }
    rF = uint8_t(SZP53(rA) | (rF & NF) | c | h);
}

Z80_INLINE uint8_t& Cpu::reg8(int z)
{
    switch (z) {
    case 0:  return rB;
    case 1:  return rC;
    case 2:  return rD;
    case 3:  return rE;
    case 4:  return rH;
    case 5:  return rL;
    default: return rA;
    }
}

// --- interrupts and the run loop -------------------------------------------

int Cpu::take_irq()
{
    halted = false;
    iff1 = iff2 = 0;
    ++r;
    push(pc.w);
    switch (im) {
    case 2:
        pc.w = rd16(uint16_t((i << 8) | irq_vector_));
        return 19;
    case 1:
        pc.w = 0x38;
        return 13;
    default:
        // IM 0: the bus byte is executed; only RST n is supported (all the
        // boards we emulate put an RST there, or nothing = RST 38).
        pc.w = (irq_vector_ & 0xc7) == 0xc7 ? uint16_t(irq_vector_ & 0x38) : 0x38;
        return 13;
    }
}

// --- unprefixed / DD / FD --------------------------------------------------

// Register order in opcodes: B C D E H L (HL) A.
#define R8_0 rB
#define R8_1 rC
#define R8_2 rD
#define R8_3 rE
#define R8_4 xh
#define R8_5 xl
#define R8_7 rA
// With a memory operand H / L stay the real ones.
#define M8_0 rB
#define M8_1 rC
#define M8_2 rD
#define M8_3 rE
#define M8_4 rH
#define M8_5 rL
#define M8_7 rA

#define LD_RR(d, s) case 0x40 + (d) * 8 + (s): R8_##d = R8_##s; return t;
#define LD_ROW(d) LD_RR(d, 0) LD_RR(d, 1) LD_RR(d, 2) LD_RR(d, 3) LD_RR(d, 4) LD_RR(d, 5) LD_RR(d, 7) \
    case 0x46 + (d) * 8: M8_##d = rd(maddr(t)); return t;                                           \
    case 0x70 + (d): wr(maddr(t), M8_##d); return t;

#define ALU_OP(k, fn) \
    case 0x80 + (k) * 8 + 0: fn(R8_0); return t; \
    case 0x80 + (k) * 8 + 1: fn(R8_1); return t; \
    case 0x80 + (k) * 8 + 2: fn(R8_2); return t; \
    case 0x80 + (k) * 8 + 3: fn(R8_3); return t; \
    case 0x80 + (k) * 8 + 4: fn(R8_4); return t; \
    case 0x80 + (k) * 8 + 5: fn(R8_5); return t; \
    case 0x80 + (k) * 8 + 6: fn(rd(maddr(t))); return t; \
    case 0x80 + (k) * 8 + 7: fn(R8_7); return t;

#define JR_IF(cond) { const int8_t d = int8_t(fetch()); if (cond) { pc.w = uint16_t(pc.w + d); t += 5; } return t; }
#define JP_IF(cond) { const uint16_t a = fetch16(); if (cond) pc.w = a; return t; }
#define CALL_IF(cond) { const uint16_t a = fetch16(); if (cond) { push(pc.w); pc.w = a; t += 7; } return t; }
#define RET_IF(cond) { if (cond) { pc.w = pop(); t += 6; } return t; }
#define RST(n) push(pc.w); pc.w = (n); return t;

template <int IDX>
inline __attribute__((always_inline)) int Cpu::exec(uint8_t op)
{
    Pair& xy = IDX == 0 ? hl : (IDX == 1 ? ix : iy);
    uint8_t& xh = xy.b.h;
    uint8_t& xl = xy.b.l;
    int t = kCycMain[op] + (IDX ? 4 : 0);

    // Address of the (HL) operand: (IX+d) costs a displacement fetch and 8 T.
    auto maddr = [&](int& tt) -> uint16_t {
        if (IDX == 0) return hl.w;
        tt += 8;
        return uint16_t(xy.w + int8_t(fetch()));
    };

    switch (op) {
    case 0x00: return t;
    case 0x01: bc.w = fetch16(); return t;
    case 0x02: wr(bc.w, rA); return t;
    case 0x03: ++bc.w; return t;
    case 0x04: rB = inc8(rB); return t;
    case 0x05: rB = dec8(rB); return t;
    case 0x06: rB = fetch(); return t;
    case 0x07:   // RLCA
        rA = uint8_t((rA << 1) | (rA >> 7));
        rF = uint8_t((rF & (SF | ZF | PF)) | (rA & (XF | YF | CF)));
        return t;
    case 0x08: { const uint16_t v = af.w; af.w = af2.w; af2.w = v; return t; }
    case 0x09: add16(xy, bc.w); return t;
    case 0x0a: rA = rd(bc.w); return t;
    case 0x0b: --bc.w; return t;
    case 0x0c: rC = inc8(rC); return t;
    case 0x0d: rC = dec8(rC); return t;
    case 0x0e: rC = fetch(); return t;
    case 0x0f: { // RRCA
        const uint8_t c = rA & 1;
        rA = uint8_t((rA >> 1) | (c << 7));
        rF = uint8_t((rF & (SF | ZF | PF)) | (rA & (XF | YF)) | c);
        return t;
    }

    case 0x10: { // DJNZ
        const int8_t d = int8_t(fetch());
        if (--rB) { pc.w = uint16_t(pc.w + d); t += 5; }
        return t;
    }
    case 0x11: de.w = fetch16(); return t;
    case 0x12: wr(de.w, rA); return t;
    case 0x13: ++de.w; return t;
    case 0x14: rD = inc8(rD); return t;
    case 0x15: rD = dec8(rD); return t;
    case 0x16: rD = fetch(); return t;
    case 0x17: { // RLA
        const uint8_t c = rA >> 7;
        rA = uint8_t((rA << 1) | (rF & CF));
        rF = uint8_t((rF & (SF | ZF | PF)) | (rA & (XF | YF)) | c);
        return t;
    }
    case 0x18: JR_IF(true)
    case 0x19: add16(xy, de.w); return t;
    case 0x1a: rA = rd(de.w); return t;
    case 0x1b: --de.w; return t;
    case 0x1c: rE = inc8(rE); return t;
    case 0x1d: rE = dec8(rE); return t;
    case 0x1e: rE = fetch(); return t;
    case 0x1f: { // RRA
        const uint8_t c = rA & 1;
        rA = uint8_t((rA >> 1) | ((rF & CF) << 7));
        rF = uint8_t((rF & (SF | ZF | PF)) | (rA & (XF | YF)) | c);
        return t;
    }

    case 0x20: JR_IF(!(rF & ZF))
    case 0x21: xy.w = fetch16(); return t;
    case 0x22: wr16(fetch16(), xy.w); return t;
    case 0x23: ++xy.w; return t;
    case 0x24: xh = inc8(xh); return t;
    case 0x25: xh = dec8(xh); return t;
    case 0x26: xh = fetch(); return t;
    case 0x27: daa(); return t;
    case 0x28: JR_IF(rF & ZF)
    case 0x29: add16(xy, xy.w); return t;
    case 0x2a: xy.w = rd16(fetch16()); return t;
    case 0x2b: --xy.w; return t;
    case 0x2c: xl = inc8(xl); return t;
    case 0x2d: xl = dec8(xl); return t;
    case 0x2e: xl = fetch(); return t;
    case 0x2f: // CPL
        rA = uint8_t(~rA);
        rF = uint8_t((rF & (SF | ZF | PF | CF)) | HF | NF | (rA & (XF | YF)));
        return t;

    case 0x30: JR_IF(!(rF & CF))
    case 0x31: sp.w = fetch16(); return t;
    case 0x32: wr(fetch16(), rA); return t;
    case 0x33: ++sp.w; return t;
    case 0x34: { const uint16_t a = maddr(t); wr(a, inc8(rd(a))); return t; }
    case 0x35: { const uint16_t a = maddr(t); wr(a, dec8(rd(a))); return t; }
    case 0x36:
        if (IDX == 0) {
            wr(hl.w, fetch());
        } else {
            const uint16_t a = uint16_t(xy.w + int8_t(fetch()));
            wr(a, fetch());
            t += 5;
        }
        return t;
    case 0x37: // SCF
        rF = uint8_t((rF & (SF | ZF | PF)) | CF | (rA & (XF | YF)));
        return t;
    case 0x38: JR_IF(rF & CF)
    case 0x39: add16(xy, sp.w); return t;
    case 0x3a: rA = rd(fetch16()); return t;
    case 0x3b: --sp.w; return t;
    case 0x3c: rA = inc8(rA); return t;
    case 0x3d: rA = dec8(rA); return t;
    case 0x3e: rA = fetch(); return t;
    case 0x3f: // CCF
        rF = uint8_t((rF & (SF | ZF | PF)) | ((rF & CF) ? HF : CF) | (rA & (XF | YF)));
        return t;

    LD_ROW(0) LD_ROW(1) LD_ROW(2) LD_ROW(3) LD_ROW(4) LD_ROW(5) LD_ROW(7)
    case 0x76: halted = true; return t;     // HALT

    ALU_OP(0, add8) ALU_OP(1, adc8) ALU_OP(2, sub8) ALU_OP(3, sbc8)
    ALU_OP(4, and8) ALU_OP(5, xor8) ALU_OP(6, or8)  ALU_OP(7, cp8)

    case 0xc0: RET_IF(!(rF & ZF))
    case 0xc1: bc.w = pop(); return t;
    case 0xc2: JP_IF(!(rF & ZF))
    case 0xc3: pc.w = fetch16(); return t;
    case 0xc4: CALL_IF(!(rF & ZF))
    case 0xc5: push(bc.w); return t;
    case 0xc6: add8(fetch()); return t;
    case 0xc7: RST(0x00)
    case 0xc8: RET_IF(rF & ZF)
    case 0xc9: pc.w = pop(); return t;
    case 0xca: JP_IF(rF & ZF)
    case 0xcb:
        if (IDX == 0) {
            ++r;
            return exec_cb(fetch());
        }
        return exec_index_cb(xy.w);
    case 0xcc: CALL_IF(rF & ZF)
    case 0xcd: { const uint16_t a = fetch16(); push(pc.w); pc.w = a; return t; }
    case 0xce: adc8(fetch()); return t;
    case 0xcf: RST(0x08)

    case 0xd0: RET_IF(!(rF & CF))
    case 0xd1: de.w = pop(); return t;
    case 0xd2: JP_IF(!(rF & CF))
    case 0xd3: { const uint8_t n = fetch(); out(uint16_t(n | (rA << 8)), rA); return t; }
    case 0xd4: CALL_IF(!(rF & CF))
    case 0xd5: push(de.w); return t;
    case 0xd6: sub8(fetch()); return t;
    case 0xd7: RST(0x10)
    case 0xd8: RET_IF(rF & CF)
    case 0xd9: { // EXX
        uint16_t v = bc.w; bc.w = bc2.w; bc2.w = v;
        v = de.w; de.w = de2.w; de2.w = v;
        v = hl.w; hl.w = hl2.w; hl2.w = v;
        return t;
    }
    case 0xda: JP_IF(rF & CF)
    case 0xdb: { const uint8_t n = fetch(); rA = in(uint16_t(n | (rA << 8))); return t; }
    case 0xdc: CALL_IF(rF & CF)
    case 0xdd:
        if (IDX != 0) { --pc.w; --r; return 4; }   // repeated prefix: the first is a NOP
        ++r;
        return exec_ix(fetch());
    case 0xde: sbc8(fetch()); return t;
    case 0xdf: RST(0x18)

    case 0xe0: RET_IF(!(rF & PF))
    case 0xe1: xy.w = pop(); return t;
    case 0xe2: JP_IF(!(rF & PF))
    case 0xe3: { const uint16_t v = rd16(sp.w); wr16(sp.w, xy.w); xy.w = v; return t; }
    case 0xe4: CALL_IF(!(rF & PF))
    case 0xe5: push(xy.w); return t;
    case 0xe6: and8(fetch()); return t;
    case 0xe7: RST(0x20)
    case 0xe8: RET_IF(rF & PF)
    case 0xe9: pc.w = xy.w; return t;
    case 0xea: JP_IF(rF & PF)
    case 0xeb: { const uint16_t v = de.w; de.w = hl.w; hl.w = v; return t; }  // always HL
    case 0xec: CALL_IF(rF & PF)
    case 0xed:
        ++r;
        return (IDX ? 4 : 0) + exec_ed(fetch(), IDX ? 4 : 0);
    case 0xee: xor8(fetch()); return t;
    case 0xef: RST(0x28)

    case 0xf0: RET_IF(!(rF & SF))
    case 0xf1: af.w = pop(); return t;
    case 0xf2: JP_IF(!(rF & SF))
    case 0xf3: iff1 = iff2 = 0; return t;
    case 0xf4: CALL_IF(!(rF & SF))
    case 0xf5: push(af.w); return t;
    case 0xf6: or8(fetch()); return t;
    case 0xf7: RST(0x30)
    case 0xf8: RET_IF(rF & SF)
    case 0xf9: sp.w = xy.w; return t;
    case 0xfa: JP_IF(rF & SF)
    case 0xfb: iff1 = iff2 = 1; ei_delay_ = true; return t;
    case 0xfc: CALL_IF(rF & SF)
    case 0xfd:
        if (IDX != 0) { --pc.w; --r; return 4; }
        ++r;
        return exec_iy(fetch());
    case 0xfe: cp8(fetch()); return t;
    default:   RST(0x38)    // 0xff
    }
}

// Plain functions (not template instances) so EMU_HOT can place them.
EMU_HOT int Cpu::exec_ix(uint8_t op) { return exec<1>(op); }
EMU_HOT int Cpu::exec_iy(uint8_t op) { return exec<2>(op); }

EMU_HOT int Cpu::run(int cycles)
{
    done_ = 0;
    target_ = cycles;
    while (done_ < target_) {
        // One test for the usual case: no NMI, no interrupt that could be
        // taken, not just after EI, not halted.
        if (__builtin_expect(nmi_pending_ | (irq_line_ & (iff1 != 0)) | ei_delay_ | halted, 0)) {
            if (nmi_pending_) {
                nmi_pending_ = false;
                halted = false;
                iff1 = 0;
                ++r;
                push(pc.w);
                pc.w = 0x66;
                done_ += 11;
                continue;
            }
            if (irq_line_ && iff1 && !ei_delay_) {
                done_ += take_irq();
                continue;
            }
            ei_delay_ = false;
            if (halted) {
                // Executes NOPs until an interrupt: burn the rest of the slice.
                const int n = (target_ - done_ + 3) >> 2;
                r = uint8_t(r + n);
                done_ += n * 4;
                break;
            }
        }
        ++r;
        done_ += exec<0>(fetch());   // inlined: no call per instruction
    }
    return done_;
}

// --- CB --------------------------------------------------------------------

EMU_HOT int Cpu::exec_cb(uint8_t op)
{
    const int z = op & 7, y = (op >> 3) & 7;
    const bool mem = z == 6;
    uint8_t v = mem ? rd(hl.w) : reg8(z);
    switch (op >> 6) {
    case 0: v = shift_op(y, v); break;
    case 1:
        // (HL): real chips take X / Y from MEMPTR; H is a stand-in.
        bit_op(y, v, mem ? rH : v);
        return mem ? 12 : 8;
    case 2: v = uint8_t(v & ~(1 << y)); break;
    default: v = uint8_t(v | (1 << y)); break;
    }
    if (mem) { wr(hl.w, v); return 15; }
    reg8(z) = v;
    return 8;
}

// DD CB d op / FD CB d op: the operation works on (IX+d); the undocumented
// register forms also copy the result into the register.
EMU_HOT int Cpu::exec_index_cb(uint16_t base)
{
    const uint16_t a = uint16_t(base + int8_t(fetch()));
    const uint8_t op = fetch();
    const int z = op & 7, y = (op >> 3) & 7;
    uint8_t v = rd(a);
    switch (op >> 6) {
    case 0: v = shift_op(y, v); break;
    case 1: bit_op(y, v, uint8_t(a >> 8)); return 20;
    case 2: v = uint8_t(v & ~(1 << y)); break;
    default: v = uint8_t(v | (1 << y)); break;
    }
    wr(a, v);
    if (z != 6) reg8(z) = v;
    return 23;
}

// --- ED --------------------------------------------------------------------

// LDIR / LDDR repeating: runs the further iterations here, exactly as run()
// would one instruction at a time (2 R increments and 21 T-states each, 16
// for the last), while run() would go on: the slice has time left and no
// interrupt is waiting. Only between directly mapped pages (no callback
// can see the time or stop the run) and never onto the instruction's own
// two bytes; otherwise run() takes the next iteration itself. `spent` =
// the T-states the instruction has taken so far (its prefixes included).
// Returns the T-states added. Galaga's CPUs copy their sprite tables with
// LDIR every frame; one iteration at a time cost ~150 host instructions.
EMU_HOT int Cpu::block_copy(uint8_t op, int spent)
{
    int added = 0;
    const int step = (op & 0x08) ? -1 : 1;
    while (done_ + spent + added < target_ && !nmi_pending_ && !(irq_line_ && iff1)) {
        const uint16_t s = hl.w, d = de.w;
        const uint8_t* rp = read_page[s >> kPageShift];
        uint8_t* wp = write_page[d >> kPageShift];
        if (!rp || !wp || uint16_t(d - pc.w) < 2) break;
        r = uint8_t(r + 2);
        const uint8_t v = rp[s & (kPageSize - 1)];
        wp[d & (kPageSize - 1)] = v;
        hl.w = uint16_t(s + step);
        de.w = uint16_t(d + step);
        --bc.w;
        const uint8_t n = uint8_t(v + rA);
        rF = uint8_t((rF & (SF | ZF | CF)) | (bc.w ? PF : 0) | (n & XF) | ((n << 4) & YF));
        if (!bc.w) {
            pc.w = uint16_t(pc.w + 2);
            return added + 16;
        }
        added += 21;
    }
    return added;
}

EMU_HOT int Cpu::exec_ed(uint8_t op, int pre)
{
    int t = kCycEd.t[op];
    if (op >= 0x40 && op < 0x80) {
        const int z = op & 7, y = (op >> 3) & 7;
        switch (z) {
        case 0: { // IN r,(C); ED 70 only sets flags
            const uint8_t v = in(bc.w);
            if (y != 6) reg8(y) = v;
            rF = uint8_t((rF & CF) | SZP53(v));
            return t;
        }
        case 1: out(bc.w, y == 6 ? 0 : reg8(y)); return t;
        case 2: {
            const uint16_t v = (y >> 1) == 0 ? bc.w : (y >> 1) == 1 ? de.w : (y >> 1) == 2 ? hl.w : sp.w;
            if (y & 1) adc16(v); else sbc16(v);
            return t;
        }
        case 3: {
            const uint16_t a = fetch16();
            Pair& p = (y >> 1) == 0 ? bc : (y >> 1) == 1 ? de : (y >> 1) == 2 ? hl : sp;
            if (y & 1) p.w = rd16(a); else wr16(a, p.w);
            return t;
        }
        case 4: { const uint8_t v = rA; rA = 0; sub8(v); return t; }   // NEG (+ mirrors)
        case 5: iff1 = iff2; pc.w = pop(); return t;                   // RETN / RETI
        case 6: im = uint8_t((y & 3) == 2 ? 1 : (y & 3) == 3 ? 2 : 0); return t;
        default:
            switch (y) {
            case 0: i = rA; return t;
            case 1: r = rA; r7 = rA & 0x80; return t;
            case 2:
                rA = i;
                rF = uint8_t((rF & CF) | SZ53(rA) | (iff2 ? PF : 0));
                return t;
            case 3:
                rA = r_value();
                rF = uint8_t((rF & CF) | SZ53(rA) | (iff2 ? PF : 0));
                return t;
            case 4: { // RRD
                const uint8_t v = rd(hl.w);
                wr(hl.w, uint8_t((rA << 4) | (v >> 4)));
                rA = uint8_t((rA & 0xf0) | (v & 0x0f));
                rF = uint8_t((rF & CF) | SZP53(rA));
                return t;
            }
            case 5: { // RLD
                const uint8_t v = rd(hl.w);
                wr(hl.w, uint8_t((v << 4) | (rA & 0x0f)));
                rA = uint8_t((rA & 0xf0) | (v >> 4));
                rF = uint8_t((rF & CF) | SZP53(rA));
                return t;
            }
            default: return t;
            }
        }
    }

    switch (op) {
    case 0xa0: case 0xa8: case 0xb0: case 0xb8: { // LDI LDD LDIR LDDR
        const uint8_t v = rd(hl.w);
        wr(de.w, v);
        if (op & 0x08) { --hl.w; --de.w; } else { ++hl.w; ++de.w; }
        --bc.w;
        const uint8_t n = uint8_t(v + rA);
        rF = uint8_t((rF & (SF | ZF | CF)) | (bc.w ? PF : 0) | (n & XF) | ((n << 4) & YF));
        if ((op & 0x10) && bc.w) {
            pc.w = uint16_t(pc.w - 2);
            t += 5;
            t += block_copy(op, pre + t);
        }
        return t;
    }
    case 0xa1: case 0xa9: case 0xb1: case 0xb9: { // CPI CPD CPIR CPDR
        const uint8_t v = rd(hl.w);
        const uint8_t res = uint8_t(rA - v);
        if (op & 0x08) --hl.w; else ++hl.w;
        --bc.w;
        uint8_t f = uint8_t((rF & CF) | NF | (SZ53(res) & (SF | ZF)) | ((rA ^ v ^ res) & HF) |
                            (bc.w ? PF : 0));
        const uint8_t n = uint8_t(res - ((f & HF) ? 1 : 0));
        f |= uint8_t((n & XF) | ((n << 4) & YF));
        rF = f;
        if ((op & 0x10) && bc.w && !(f & ZF)) { pc.w = uint16_t(pc.w - 2); t += 5; }
        return t;
    }
    case 0xa2: case 0xaa: case 0xb2: case 0xba: { // INI IND INIR INDR
        const uint8_t v = in(bc.w);
        wr(hl.w, v);
        const bool down = op & 0x08;
        if (down) --hl.w; else ++hl.w;
        --rB;
        const unsigned k = unsigned(v) + uint8_t(down ? rC - 1 : rC + 1);
        rF = uint8_t(SZ53(rB) | ((v & 0x80) ? NF : 0) | (k > 255 ? (HF | CF) : 0) |
                     (SZP53((k & 7) ^ rB) & PF));
        if ((op & 0x10) && rB) { pc.w = uint16_t(pc.w - 2); t += 5; }
        return t;
    }
    case 0xa3: case 0xab: case 0xb3: case 0xbb: { // OUTI OUTD OTIR OTDR
        const uint8_t v = rd(hl.w);
        --rB;
        out(bc.w, v);
        if (op & 0x08) --hl.w; else ++hl.w;
        const unsigned k = unsigned(v) + rL;
        rF = uint8_t(SZ53(rB) | ((v & 0x80) ? NF : 0) | (k > 255 ? (HF | CF) : 0) |
                     (SZP53((k & 7) ^ rB) & PF));
        if ((op & 0x10) && rB) { pc.w = uint16_t(pc.w - 2); t += 5; }
        return t;
    }
    default:
        return t;   // undefined ED opcode: NOP
    }
}

} // namespace z80
