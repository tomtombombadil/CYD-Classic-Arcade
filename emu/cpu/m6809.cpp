// Motorola 6809 interpreter - see m6809.h.
#include "m6809.h"

namespace m6809 {

namespace {

// The register union assumes a little-endian host (x86, ESP32 Xtensa).
constexpr bool little_endian()
{
    return static_cast<uint8_t>(0x0102) == 0x02;
}
static_assert(little_endian(), "m6809::Cpu's D register assumes a little-endian host");

// Page 1 base cycles (datasheet), without the indexed modes' extra cycles.
// 0 = handled specially (prefixes, stack ops whose count depends on the
// operand, waits).
constexpr uint8_t kCycles[256] = {
    // 0x00 direct read-modify-write; 0E JMP
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6,
    // 0x10: prefixes, NOP, SYNC, HCF x2, LBRA, LBSR, X18, DAA, ORCC, NOP, ANDCC, SEX, EXG, TFR
    0, 0, 2, 0, 0, 0, 5, 9, 3, 2, 3, 2, 3, 2, 8, 6,
    // 0x20 branches
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    // 0x30 LEA x4, PSH/PUL x4, ANDCC alias, RTS, ABX, RTI, CWAI, MUL, reset, SWI
    4, 4, 4, 4, 5, 5, 5, 5, 4, 5, 3, 0, 0, 11, 19, 19,
    // 0x40 / 0x50 inherent A / B
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    // 0x60 indexed RMW (+ extra); 6E JMP
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6,
    // 0x70 extended RMW; 7E JMP
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 7,
    // 0x80 immediate A: SUBD 4, CMPX 4, BSR 7, LDX 3, 8F store-immediate alias 3
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 7, 3, 3,
    // 0x90 direct A
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,
    // 0xA0 indexed A (+ extra)
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5,
    // 0xB0 extended A
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6,
    // 0xC0 immediate B: ADDD 4, LDD 3, CD HCF, LDU 3, CF alias 3
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 0, 3, 3,
    // 0xD0 direct B
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    // 0xE0 indexed B (+ extra)
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    // 0xF0 extended B
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,
};

} // namespace

Cpu::Cpu()
{
    d = 0;
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
    dp = 0;
    cc = CC_I | CC_F;
    cwai = sync = halted = false;
    nmi_pending_ = nmi_armed_ = false;
    pc = rd16(kVecReset);
}

int Cpu::run(int cycles)
{
    done_ = 0;
    target_ = cycles;
    while (done_ < target_) {
        if ((nmi_pending_ || firq_line_ || irq_line_) && interrupt()) continue;
        if (cwai || sync || halted) {           // waiting: the rest of the slice passes
            if (done_ < target_) done_ = target_;
            break;
        }
        step();
    }
    return done_;
}

// Interrupts are taken between instructions: NMI first, then FIRQ, then
// IRQ. In CWAI the state is already on the stack (the datasheet's 20 cycles
// of CWAI = 16 before the wait + 4 to fetch the vector). SYNC ends on any
// interrupt line, masked or not; a masked one just carries on.
bool Cpu::interrupt()
{
    uint16_t vec = 0;
    if (nmi_pending_ && nmi_armed_) {
        nmi_pending_ = false;
        vec = kVecNmi;
    } else if (firq_line_ && !(cc & CC_F)) {
        vec = kVecFirq;
    } else if (irq_line_ && !(cc & CC_I)) {
        vec = kVecIrq;
    }
    if (!vec) {
        if (sync && (firq_line_ || irq_line_ || nmi_pending_)) {
            sync = false;
            done_ += 2;
        }
        if (!nmi_armed_) nmi_pending_ = false;
        return false;
    }
    if (sync) {
        sync = false;
        done_ += 2;
    }
    if (cwai) {
        cwai = false;
        cc |= uint8_t(vec == kVecIrq ? CC_I : CC_I | CC_F);
        done_ += 4;
    } else if (vec == kVecFirq) {
        cc &= uint8_t(~CC_E);
        push16s(pc);
        push8s(cc);
        cc |= CC_I | CC_F;
        done_ += 10;
    } else {
        cc |= CC_E;
        push_entire();
        cc |= uint8_t(vec == kVecIrq ? CC_I : CC_I | CC_F);
        done_ += 19;
    }
    pc = rd16(vec);
    return true;
}

void Cpu::push_entire()
{
    push16s(pc);
    push16s(u);
    push16s(y);
    push16s(x);
    push8s(dp);
    push8s(r.b);
    push8s(r.a);
    push8s(cc);
}

// PSHS / PSHU: PC, U or S, Y, X, DP, B, A, CC (high bits first); one cycle
// a byte.
void Cpu::push_regs(uint16_t& sp, uint8_t m, bool to_s)
{
    auto p8 = [&](uint8_t v) { wr(--sp, v); ++done_; };
    auto p16 = [&](uint16_t v) { p8(uint8_t(v)); p8(uint8_t(v >> 8)); };
    if (m & 0x80) p16(pc);
    if (m & 0x40) p16(to_s ? u : s);
    if (m & 0x20) p16(y);
    if (m & 0x10) p16(x);
    if (m & 0x08) p8(dp);
    if (m & 0x04) p8(r.b);
    if (m & 0x02) p8(r.a);
    if (m & 0x01) p8(cc);
}

void Cpu::pull_regs(uint16_t& sp, uint8_t m, bool from_s)
{
    auto l8 = [&]() { ++done_; return rd(sp++); };
    auto l16 = [&]() { const uint8_t h = l8(); return uint16_t((h << 8) | l8()); };
    if (m & 0x01) cc = l8();
    if (m & 0x02) r.a = l8();
    if (m & 0x04) r.b = l8();
    if (m & 0x08) dp = l8();
    if (m & 0x10) x = l16();
    if (m & 0x20) y = l16();
    if (m & 0x40) {
        const uint16_t v = l16();
        if (from_s) u = v; else s = v;
    }
    if (m & 0x80) pc = l16();
}

// Register codes of TFR / EXG: 0 D, 1 X, 2 Y, 3 U, 4 S, 5 PC, 8 A, 9 B,
// A CC, B DP; others read FFFF and ignore writes. An 8-bit register read
// into a 16-bit one gives FF in the high byte (CC and DP give themselves
// twice when the first register of an EXG is 8-bit, or in a TFR).
uint16_t Cpu::tfr_read(int reg, bool cc_doubled) const
{
    switch (reg & 0x0f) {
    case 0:  return d;
    case 1:  return x;
    case 2:  return y;
    case 3:  return u;
    case 4:  return s;
    case 5:  return pc;
    case 8:  return uint16_t(0xff00 | r.a);
    case 9:  return uint16_t(0xff00 | r.b);
    case 10: return cc_doubled ? uint16_t((cc << 8) | cc) : uint16_t(0xff00 | cc);
    case 11: return cc_doubled ? uint16_t((dp << 8) | dp) : uint16_t(0xff00 | dp);
    default: return 0xffff;
    }
}

void Cpu::tfr_write(int reg, uint16_t v)
{
    switch (reg & 0x0f) {
    case 0:  d = v; break;
    case 1:  x = v; break;
    case 2:  y = v; break;
    case 3:  u = v; break;
    case 4:  s = v; nmi_armed_ = true; break;
    case 5:  pc = v; break;
    case 8:  r.a = uint8_t(v); break;
    case 9:  r.b = uint8_t(v); break;
    case 10: cc = uint8_t(v); break;
    case 11: dp = uint8_t(v); break;
    default: break;
    }
}

// The indexed postbyte. Extra cycles over the instruction's base count:
//   ,R 0   ,R+ 2   ,R++ 3   ,-R 2   ,--R 3   5-bit 1   A,R / B,R 1
//   n8,R 1   n16,R 4   D,R 4   n8,PCR 1   n16,PCR 5   indirect +3
//   [n16] 5
EMU_HOT uint16_t Cpu::ea_indexed()
{
    const uint8_t pb = fetch();
    uint16_t* reg;
    switch (pb & 0x60) {
    case 0x00: reg = &x; break;
    case 0x20: reg = &y; break;
    case 0x40: reg = &u; break;
    default:   reg = &s; break;
    }
    if (!(pb & 0x80)) {                         // 5-bit signed offset
        done_ += 1;
        const int off = (pb & 0x10) ? int(pb & 0x0f) - 16 : int(pb & 0x0f);
        return uint16_t(*reg + off);
    }
    uint16_t ea;
    switch (pb & 0x0f) {
    case 0x0: ea = *reg; *reg = uint16_t(*reg + 1); done_ += 2; break;
    case 0x1: ea = *reg; *reg = uint16_t(*reg + 2); done_ += 3; break;
    case 0x2: *reg = uint16_t(*reg - 1); ea = *reg; done_ += 2; break;
    case 0x3: *reg = uint16_t(*reg - 2); ea = *reg; done_ += 3; break;
    case 0x4: ea = *reg; break;
    case 0x5: ea = uint16_t(*reg + int8_t(r.b)); done_ += 1; break;
    case 0x6: ea = uint16_t(*reg + int8_t(r.a)); done_ += 1; break;
    case 0x8: ea = uint16_t(*reg + int8_t(fetch())); done_ += 1; break;
    case 0x9: ea = uint16_t(*reg + fetch16()); done_ += 4; break;
    case 0xb: ea = uint16_t(*reg + d); done_ += 4; break;
    case 0xc: { const int8_t o = int8_t(fetch()); ea = uint16_t(pc + o); done_ += 1; break; }
    case 0xd: { const uint16_t o = fetch16(); ea = uint16_t(pc + o); done_ += 5; break; }
    case 0xf: ea = fetch16(); done_ += 2; break;     // only meaningful indirect: [n16]
    default:  ea = 0; break;                         // 7, A, E: no such mode
    }
    if (pb & 0x10) {                            // indirect
        ea = rd16(ea);
        done_ += 3;
    }
    return ea;
}

// --- arithmetic ---------------------------------------------------------------------

uint8_t Cpu::op_neg(uint8_t v)
{
    const uint8_t res = uint8_t(-v);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z)
                 | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0));
    return res;
}

uint8_t Cpu::op_com(uint8_t v)
{
    const uint8_t res = uint8_t(~v);
    nz8(res);
    cc |= CC_C;
    return res;
}

uint8_t Cpu::op_lsr(uint8_t v)
{
    const uint8_t res = uint8_t(v >> 1);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (res ? 0 : CC_Z) | (v & 1 ? CC_C : 0));
    return res;
}

uint8_t Cpu::op_ror(uint8_t v)
{
    const uint8_t res = uint8_t((v >> 1) | (cc & CC_C ? 0x80 : 0));
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z) | (v & 1 ? CC_C : 0));
    return res;
}

uint8_t Cpu::op_asr(uint8_t v)
{
    const uint8_t res = uint8_t((v >> 1) | (v & 0x80));
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z) | (v & 1 ? CC_C : 0));
    return res;
}

uint8_t Cpu::op_asl(uint8_t v)
{
    const uint8_t res = uint8_t(v << 1);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z)
                 | (((v ^ (v << 1)) & 0x80) ? CC_V : 0) | (v & 0x80 ? CC_C : 0));
    return res;
}

uint8_t Cpu::op_rol(uint8_t v)
{
    const uint8_t res = uint8_t((v << 1) | (cc & CC_C ? 1 : 0));
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z)
                 | (((v ^ (v << 1)) & 0x80) ? CC_V : 0) | (v & 0x80 ? CC_C : 0));
    return res;
}

uint8_t Cpu::op_dec(uint8_t v)
{
    const uint8_t res = uint8_t(v - 1);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z) | (v == 0x80 ? CC_V : 0));
    return res;
}

uint8_t Cpu::op_inc(uint8_t v)
{
    const uint8_t res = uint8_t(v + 1);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (res & 0x80 ? CC_N : 0) | (res ? 0 : CC_Z) | (v == 0x7f ? CC_V : 0));
    return res;
}

void Cpu::op_tst(uint8_t v) { nz8(v); }

// a - b - carry: N Z V C (H undefined on the 6809, left alone)
uint8_t Cpu::op_sub(uint8_t a, uint8_t b, int carry)
{
    const unsigned res = unsigned(a) - b - unsigned(carry);
    const uint8_t r8 = uint8_t(res);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (r8 & 0x80 ? CC_N : 0) | (r8 ? 0 : CC_Z)
                 | (((a ^ b) & (a ^ r8) & 0x80) ? CC_V : 0) | (res & 0x100 ? CC_C : 0));
    return r8;
}

// a + b + carry: H N Z V C
uint8_t Cpu::op_add(uint8_t a, uint8_t b, int carry)
{
    const unsigned res = unsigned(a) + b + unsigned(carry);
    const uint8_t r8 = uint8_t(res);
    cc = uint8_t((cc & ~(CC_H | CC_N | CC_Z | CC_V | CC_C)) | ((a ^ b ^ res) & 0x10 ? CC_H : 0)
                 | (r8 & 0x80 ? CC_N : 0) | (r8 ? 0 : CC_Z) | ((~(a ^ b) & (a ^ r8) & 0x80) ? CC_V : 0)
                 | (res & 0x100 ? CC_C : 0));
    return r8;
}

uint16_t Cpu::op_sub16(uint16_t a, uint16_t b)
{
    const uint32_t res = uint32_t(a) - b;
    const uint16_t r16 = uint16_t(res);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (r16 & 0x8000 ? CC_N : 0) | (r16 ? 0 : CC_Z)
                 | (((a ^ b) & (a ^ r16) & 0x8000) ? CC_V : 0) | (res & 0x10000 ? CC_C : 0));
    return r16;
}

uint16_t Cpu::op_add16(uint16_t a, uint16_t b)
{
    const uint32_t res = uint32_t(a) + b;
    const uint16_t r16 = uint16_t(res);
    cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V | CC_C)) | (r16 & 0x8000 ? CC_N : 0) | (r16 ? 0 : CC_Z)
                 | ((~(a ^ b) & (a ^ r16) & 0x8000) ? CC_V : 0) | (res & 0x10000 ? CC_C : 0));
    return r16;
}

// The read-modify-write group (low nibble of 0x00-0x7F). D (TST) and E
// (JMP) never get here.
uint8_t Cpu::rmw(uint8_t op, uint8_t v)
{
    switch (op & 0x0f) {
    case 0x0: case 0x1: return op_neg(v);
    case 0x2:           return (cc & CC_C) ? op_com(v) : op_neg(v);
    case 0x3:           return op_com(v);
    case 0x4: case 0x5: return op_lsr(v);
    case 0x6:           return op_ror(v);
    case 0x7:           return op_asr(v);
    case 0x8:           return op_asl(v);
    case 0x9:           return op_rol(v);
    case 0xa:           return op_dec(v);
    case 0xb: {                                 // DEC that also sets C (C = operand != 0)
        const bool c = v != 0;
        const uint8_t res = op_dec(v);
        cc = uint8_t((cc & ~CC_C) | (c ? CC_C : 0));
        return res;
    }
    case 0xc:           return op_inc(v);
    case 0xe:                                   // 4E / 5E: CLR that keeps C
        cc = uint8_t((cc & ~(CC_N | CC_V)) | CC_Z);
        return 0;
    default:                                    // F: CLR
        cc = uint8_t((cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
        return 0;
    }
}

bool Cpu::cond(uint8_t op) const
{
    const bool c = cc & CC_C, z = cc & CC_Z, n = cc & CC_N, v = cc & CC_V;
    bool t;
    switch ((op >> 1) & 7) {
    case 0: t = true; break;                    // BRA / BRN
    case 1: t = !(c || z); break;               // BHI / BLS
    case 2: t = !c; break;                      // BCC / BCS
    case 3: t = !z; break;                      // BNE / BEQ
    case 4: t = !v; break;                      // BVC / BVS
    case 5: t = !n; break;                      // BPL / BMI
    case 6: t = n == v; break;                  // BGE / BLT
    default: t = !z && n == v; break;           // BGT / BLE
    }
    return (op & 1) ? !t : t;
}

// --- execution -------------------------------------------------------------------------

EMU_HOT void Cpu::step()
{
    exec(fetch());
}

// One page 1 instruction (the opcode is fetched).
EMU_HOT void Cpu::exec(uint8_t op)
{
    done_ += kCycles[op];
    switch (op) {
    // --- read-modify-write: direct, A, B, indexed, extended ---
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0f: {
        const uint16_t ea = ea_direct();
        wr(ea, rmw(op, rd(ea)));
        break;
    }
    case 0x0d: op_tst(rd(ea_direct())); break;
    case 0x0e: pc = ea_direct(); break;

    case 0x10: page2(); break;
    case 0x11: page3(); break;
    case 0x12: case 0x1b: break;                // NOP (1B: an alias)
    case 0x13: sync = true; break;              // SYNC
    case 0x14: case 0x15: case 0xcd:            // halt and catch fire
        halted = true;
        break;
    case 0x16: { const uint16_t o = fetch16(); pc = uint16_t(pc + o); break; }    // LBRA
    case 0x17: {                                // LBSR
        const uint16_t o = fetch16();
        push16s(pc);
        pc = uint16_t(pc + o);
        break;
    }
    case 0x18: {                                // undocumented: CC from (CC & n) << 1
        const uint8_t n = rd(pc);
        cc = uint8_t(((cc & n) << 1) | ((cc & CC_Z) >> 1));
        break;
    }
    case 0x19: {                                // DAA
        const uint8_t a = r.a, msn = a & 0xf0, lsn = a & 0x0f;
        unsigned cf = 0;
        if (lsn > 0x09 || (cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 0x09)  cf |= 0x60;
        if (msn > 0x90 || (cc & CC_C)) cf |= 0x60;
        const unsigned t = a + cf;
        r.a = uint8_t(t);
        nz8(r.a);                               // also clears V
        if (t & 0x100) cc |= CC_C;
        break;
    }
    case 0x1a: cc |= fetch(); break;            // ORCC
    case 0x1c: case 0x38: cc &= fetch(); break; // ANDCC (38: alias, one cycle more)
    case 0x1d:                                  // SEX
        r.a = (r.b & 0x80) ? 0xff : 0x00;
        cc = uint8_t((cc & ~(CC_N | CC_Z)) | (d & 0x8000 ? CC_N : 0) | (d ? 0 : CC_Z));
        break;
    case 0x1e: {                                // EXG
        const uint8_t p = fetch();
        const bool doubled = p & 0x80;
        const uint16_t r1 = tfr_read(p >> 4, doubled), r2 = tfr_read(p, doubled);
        tfr_write(p, r1);
        tfr_write(p >> 4, r2);
        break;
    }
    case 0x1f: {                                // TFR
        const uint8_t p = fetch();
        tfr_write(p, tfr_read(p >> 4, true));
        break;
    }

    // --- short branches ---
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
        const int8_t o = int8_t(fetch());
        if (cond(op)) pc = uint16_t(pc + o);
        break;
    }

    // --- 0x30 ---
    case 0x30: x = ea_indexed(); cc = uint8_t((cc & ~CC_Z) | (x ? 0 : CC_Z)); break;
    case 0x31: y = ea_indexed(); cc = uint8_t((cc & ~CC_Z) | (y ? 0 : CC_Z)); break;
    case 0x32: s = ea_indexed(); nmi_armed_ = true; break;
    case 0x33: u = ea_indexed(); break;
    case 0x34: push_regs(s, fetch(), true); break;
    case 0x35: pull_regs(s, fetch(), true); break;
    case 0x36: push_regs(u, fetch(), false); break;
    case 0x37: pull_regs(u, fetch(), false); break;
    case 0x39: pc = uint16_t((rd(s) << 8) | rd(uint16_t(s + 1))); s = uint16_t(s + 2); break;   // RTS
    case 0x3a: x = uint16_t(x + r.b); break;    // ABX
    case 0x3b:                                  // RTI
        cc = rd(s++);
        if (cc & CC_E) {
            r.a = rd(s++);
            r.b = rd(s++);
            dp = rd(s++);
            x = uint16_t((rd(s) << 8) | rd(uint16_t(s + 1))); s = uint16_t(s + 2);
            y = uint16_t((rd(s) << 8) | rd(uint16_t(s + 1))); s = uint16_t(s + 2);
            u = uint16_t((rd(s) << 8) | rd(uint16_t(s + 1))); s = uint16_t(s + 2);
            done_ += 15;
        } else {
            done_ += 6;
        }
        pc = uint16_t((rd(s) << 8) | rd(uint16_t(s + 1)));
        s = uint16_t(s + 2);
        break;
    case 0x3c:                                  // CWAI: mask, push everything, wait
        cc &= fetch();
        cc |= CC_E;
        push_entire();
        cwai = true;
        done_ += 16;
        break;
    case 0x3d: {                                // MUL
        d = uint16_t(r.a * r.b);
        cc = uint8_t((cc & ~(CC_Z | CC_C)) | (d ? 0 : CC_Z) | (d & 0x80 ? CC_C : 0));
        break;
    }
    case 0x3e:                                  // undocumented: reset through FFFE, state pushed
        cc |= CC_E;
        push_entire();
        cc |= CC_I | CC_F;
        pc = rd16(kVecReset);
        break;
    case 0x3f:                                  // SWI
        cc |= CC_E;
        push_entire();
        cc |= CC_I | CC_F;
        pc = rd16(kVecSwi);
        break;

    // --- inherent A / B ---
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4e: case 0x4f:
        r.a = rmw(op, r.a);
        break;
    case 0x4d: op_tst(r.a); break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5e: case 0x5f:
        r.b = rmw(op, r.b);
        break;
    case 0x5d: op_tst(r.b); break;

    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6f: {
        const uint16_t ea = ea_indexed();
        wr(ea, rmw(op, rd(ea)));
        break;
    }
    case 0x6d: op_tst(rd(ea_indexed())); break;
    case 0x6e: pc = ea_indexed(); break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7f: {
        const uint16_t ea = fetch16();
        wr(ea, rmw(op, rd(ea)));
        break;
    }
    case 0x7d: op_tst(rd(fetch16())); break;
    case 0x7e: pc = fetch16(); break;

    // --- the A / B / D / X / U groups ---
    case 0x8d: {                                // BSR
        const int8_t o = int8_t(fetch());
        push16s(pc);
        pc = uint16_t(pc + o);
        break;
    }
    case 0x9d: case 0xad: case 0xbd: {          // JSR
        const uint16_t ea = op == 0x9d ? ea_direct() : op == 0xad ? ea_indexed() : fetch16();
        push16s(pc);
        pc = ea;
        break;
    }
    case 0x87: case 0xc7:                       // undocumented store-immediate: flags only
        nz8(op == 0x87 ? r.a : r.b);
        ++pc;
        break;
    case 0x8f: case 0xcf:                       // undocumented 16-bit store-immediate
        nz16(op == 0x8f ? x : u);
        pc = uint16_t(pc + 2);
        break;

    default: {
        if (op < 0x80) break;                   // nothing else below 0x80
        // Operand address by mode (bits 4-5): immediate, direct, indexed, extended.
        const int mode = (op >> 4) & 3;
        const int low = op & 0x0f;
        const bool is_b = op & 0x40;
        // 16-bit operand: SUBD / ADDD (3), CMPX / LDD (C), LDX / LDU (E); stores
        // (D, F except the X-group's 8D / 9D... handled above).
        const bool wide = low == 0x3 || low == 0xc || low == 0xe || (low == 0xd && is_b) || low == 0xf;
        uint16_t ea = 0;
        switch (mode) {
        case 0: ea = pc; pc = uint16_t(pc + (wide ? 2 : 1)); break;
        case 1: ea = ea_direct(); break;
        case 2: ea = ea_indexed(); break;
        default: ea = fetch16(); break;
        }
        uint8_t& acc = is_b ? r.b : r.a;
        switch (low) {
        case 0x0: acc = op_sub(acc, rd(ea), 0); break;                          // SUB
        case 0x1: op_sub(acc, rd(ea), 0); break;                                // CMP
        case 0x2: acc = op_sub(acc, rd(ea), cc & CC_C ? 1 : 0); break;          // SBC
        case 0x3:                                                               // SUBD / ADDD
            d = is_b ? op_add16(d, rd16(ea)) : op_sub16(d, rd16(ea));
            break;
        case 0x4: acc &= rd(ea); nz8(acc); break;                               // AND
        case 0x5: nz8(uint8_t(acc & rd(ea))); break;                            // BIT
        case 0x6: acc = rd(ea); nz8(acc); break;                                // LD
        case 0x7: nz8(acc); wr(ea, acc); break;                                 // ST
        case 0x8: acc ^= rd(ea); nz8(acc); break;                               // EOR
        case 0x9: acc = op_add(acc, rd(ea), cc & CC_C ? 1 : 0); break;          // ADC
        case 0xa: acc |= rd(ea); nz8(acc); break;                               // OR
        case 0xb: acc = op_add(acc, rd(ea), 0); break;                          // ADD
        case 0xc:                                                               // CMPX / LDD
            if (is_b) { d = rd16(ea); nz16(d); }
            else      op_sub16(x, rd16(ea));
            break;
        case 0xd: nz16(d); wr16(ea, d); break;                                  // STD (B group)
        case 0xe:                                                               // LDX / LDU
            if (is_b) { u = rd16(ea); nz16(u); }
            else      { x = rd16(ea); nz16(x); }
            break;
        case 0xf:                                                               // STX / STU
            if (is_b) { nz16(u); wr16(ea, u); }
            else      { nz16(x); wr16(ea, x); }
            break;
        }
        break;
    }
    }
}

// Page 2 (prefix 10): long branches, SWI2, CMPD / CMPY, LDY / STY, LDS / STS.
// Counts include the prefix's cycle. Unknown codes run as page 1 codes
// with the prefix's cycle added.
void Cpu::page2()
{
    const uint8_t op = fetch();
    if (op >= 0x20 && op <= 0x2f) {             // LBcc: 5, 6 when taken
        const uint16_t o = fetch16();
        done_ += 5;
        if (cond(op)) {
            pc = uint16_t(pc + o);
            done_ += 1;
        }
        return;
    }
    const int mode = (op >> 4) & 3;
    auto addr = [&](int imm_bytes) -> uint16_t {
        uint16_t ea;
        switch (mode) {
        case 0: ea = pc; pc = uint16_t(pc + imm_bytes); break;
        case 1: ea = ea_direct(); break;
        case 2: ea = ea_indexed(); break;
        default: ea = fetch16(); break;
        }
        return ea;
    };
    // Base cycles by mode for compares (5 7 7 8) and loads / stores (4 6 6 7).
    static constexpr uint8_t kCmp[4] = {5, 7, 7, 8}, kLd[4] = {4, 6, 6, 7};
    switch (op) {
    case 0x10: case 0x11: done_ += 1; page2(); return;  // repeated prefixes
    case 0x3f:                                  // SWI2
        done_ += 20;
        cc |= CC_E;
        push_entire();
        pc = rd16(kVecSwi2);
        return;
    case 0x83: case 0x93: case 0xa3: case 0xb3:  // CMPD
        done_ += kCmp[mode];
        op_sub16(d, rd16(addr(2)));
        return;
    case 0x8c: case 0x9c: case 0xac: case 0xbc:  // CMPY
        done_ += kCmp[mode];
        op_sub16(y, rd16(addr(2)));
        return;
    case 0x8e: case 0x9e: case 0xae: case 0xbe:  // LDY
        done_ += kLd[mode];
        y = rd16(addr(2));
        nz16(y);
        return;
    case 0x9f: case 0xaf: case 0xbf: {          // STY
        done_ += kLd[mode];
        const uint16_t ea = addr(2);
        nz16(y);
        wr16(ea, y);
        return;
    }
    case 0xce: case 0xde: case 0xee: case 0xfe:  // LDS
        done_ += kLd[mode];
        s = rd16(addr(2));
        nz16(s);
        nmi_armed_ = true;
        return;
    case 0xdf: case 0xef: case 0xff: {          // STS
        done_ += kLd[mode];
        const uint16_t ea = addr(2);
        nz16(s);
        wr16(ea, s);
        return;
    }
    default:
        done_ += 1;
        exec(op);
        return;
    }
}

// Page 3 (prefix 11): SWI3, CMPU, CMPS.
void Cpu::page3()
{
    const uint8_t op = fetch();
    const int mode = (op >> 4) & 3;
    static constexpr uint8_t kCmp[4] = {5, 7, 7, 8};
    uint16_t* reg = nullptr;
    switch (op) {
    case 0x10: case 0x11: done_ += 1; page3(); return;
    case 0x3f:                                  // SWI3
        done_ += 20;
        cc |= CC_E;
        push_entire();
        pc = rd16(kVecSwi3);
        return;
    case 0x83: case 0x93: case 0xa3: case 0xb3: reg = &u; break;
    case 0x8c: case 0x9c: case 0xac: case 0xbc: reg = &s; break;
    default:
        done_ += 1;
        exec(op);
        return;
    }
    done_ += kCmp[mode];
    uint16_t ea;
    switch (mode) {
    case 0: ea = pc; pc = uint16_t(pc + 2); break;
    case 1: ea = ea_direct(); break;
    case 2: ea = ea_indexed(); break;
    default: ea = fetch16(); break;
    }
    op_sub16(*reg, rd16(ea));
}

} // namespace m6809
