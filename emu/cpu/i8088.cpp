// Intel 8088 CPU interpreter - see i8088.h.
//
// Dispatch: one switch over the opcode after the prefixes. ModRM operands
// are decoded once per instruction into a register number or a segment +
// offset (modrm()), which also adds the effective-address cycles. Timing
// and flag rules are from Intel's public 8086 / 8088 documents (the iAPX
// 86/88 user's manual); the undefined-flag and undocumented-opcode
// behaviour from the public SingleStepTests 8088 suite, which the host
// tests run (tools/host_tests/i8088_test.cpp).
#include "i8088.h"

namespace i8088 {

namespace {

struct Parity {
    uint8_t even[256];
    constexpr Parity() : even()
    {
        for (int i = 0; i < 256; ++i) {
            int n = 0;
            for (int b = 0; b < 8; ++b) n += (i >> b) & 1;
            even[i] = (n & 1) ? 0 : 1;
        }
    }
};
constexpr Parity kParity;

uint8_t default_read(void*, uint32_t) { return 0xff; }
void    default_write(void*, uint32_t, uint8_t) {}
uint8_t default_in(void*, uint16_t) { return 0xff; }
void    default_out(void*, uint16_t, uint8_t) {}
uint8_t default_ack(void*) { return 0xff; }

enum AluOp { kAdd, kOr, kAdc, kSbb, kAnd, kSub, kXor, kCmp };

} // namespace

Cpu::Cpu()
{
    for (int p = 0; p < kPages; ++p) {
        read_page[p] = nullptr;
        write_page[p] = nullptr;
    }
    mem_read = default_read;
    mem_write = default_write;
    io_read = default_in;
    io_write = default_out;
    int_ack = default_ack;
}

void Cpu::map_read(uint32_t start, uint32_t len, const uint8_t* p)
{
    for (uint32_t off = 0; off < len; off += kPageSize) {
        const uint32_t a = start + off;
        if (a < 0x10000) read_page[a >> kPageShift] = p ? p + off : nullptr;
    }
}

void Cpu::map_write(uint32_t start, uint32_t len, uint8_t* p)
{
    for (uint32_t off = 0; off < len; off += kPageSize) {
        const uint32_t a = start + off;
        if (a < 0x10000) write_page[a >> kPageShift] = p ? p + off : nullptr;
    }
}

void Cpu::reset()
{
    sreg[ES] = sreg[SS] = sreg[DS] = 0;
    sreg[CS] = 0xffff;
    ip = 0;
    set_flags(0);
    nmi_pending_ = halted_ = inhibit_ = false;
}

uint16_t Cpu::flags() const
{
    return uint16_t(0xf002 | cf_ | pf_ << 2 | af_ << 4 | zf_ << 6 | sf_ << 7 | tf_ << 8 | if_ << 9 |
                    df_ << 10 | of_ << 11);
}

void Cpu::set_flags(uint16_t f)
{
    cf_ = f & 1;
    pf_ = (f >> 2) & 1;
    af_ = (f >> 4) & 1;
    zf_ = (f >> 6) & 1;
    sf_ = (f >> 7) & 1;
    tf_ = (f >> 8) & 1;
    if_ = (f >> 9) & 1;
    df_ = (f >> 10) & 1;
    of_ = (f >> 11) & 1;
}

inline void Cpu::szp8(uint8_t v)
{
    zf_ = v == 0;
    sf_ = v >> 7;
    pf_ = kParity.even[v];
}

inline void Cpu::szp16(uint16_t v)
{
    zf_ = v == 0;
    sf_ = v >> 15;
    pf_ = kParity.even[v & 0xff];
}

// Reads the ModRM byte and any displacement; for a memory operand sets
// ea_seg_ / ea_off_ and adds the effective-address cycles.
inline void Cpu::modrm()
{
    modrm_ = fetch8();
    if (modrm_ >= 0xc0) return;
    const int mod = modrm_ >> 6;
    uint16_t off;
    int seg = DS, c;
    switch (modrm_ & 7) {
    case 0: off = uint16_t(r[BX] + r[SI]); c = 7; break;
    case 1: off = uint16_t(r[BX] + r[DI]); c = 8; break;
    case 2: off = uint16_t(r[BP] + r[SI]); c = 8; seg = SS; break;
    case 3: off = uint16_t(r[BP] + r[DI]); c = 7; seg = SS; break;
    case 4: off = r[SI]; c = 5; break;
    case 5: off = r[DI]; c = 5; break;
    case 6:
        if (mod == 0) { off = fetch16(); c = 6; }
        else          { off = r[BP]; c = 5; seg = SS; }
        break;
    default: off = r[BX]; c = 5; break;
    }
    if (mod == 1)      { off = uint16_t(off + int8_t(fetch8())); c += 4; }
    else if (mod == 2) { off = uint16_t(off + fetch16()); c += 4; }
    ea_off_ = off;
    ea_seg_ = sreg[seg_ovr_ >= 0 ? seg_ovr_ : seg];
    cyc_ += c;
}

template <int W>
inline uint32_t Cpu::alu(int op, uint32_t a, uint32_t b)
{
    constexpr uint32_t M = W == 8 ? 0xffu : 0xffffu;
    constexpr uint32_t S = W == 8 ? 0x80u : 0x8000u;
    uint32_t res;
    switch (op) {
    case kAdd:
    case kAdc:
        res = a + b + (op == kAdc ? cf_ : 0);
        cf_ = res > M;
        of_ = ((a ^ res) & (b ^ res) & S) != 0;
        af_ = ((a ^ b ^ res) >> 4) & 1;
        break;
    case kSub:
    case kSbb:
    case kCmp:
        res = a - b - (op == kSbb ? cf_ : 0);
        cf_ = res > M;      // borrow: the difference wrapped below zero
        of_ = ((a ^ b) & (a ^ res) & S) != 0;
        af_ = ((a ^ b ^ res) >> 4) & 1;
        break;
    case kOr:  res = a | b; cf_ = of_ = af_ = 0; break;
    case kAnd: res = a & b; cf_ = of_ = af_ = 0; break;
    default:   res = a ^ b; cf_ = of_ = af_ = 0; break;
    }
    res &= M;
    if (W == 8) szp8(uint8_t(res));
    else        szp16(uint16_t(res));
    return res;
}

template <int W>
inline uint32_t Cpu::inc(uint32_t a)
{
    constexpr uint32_t M = W == 8 ? 0xffu : 0xffffu;
    constexpr uint32_t S = W == 8 ? 0x80u : 0x8000u;
    const uint32_t res = (a + 1) & M;
    of_ = res == S;
    af_ = (res & 0xf) == 0;
    if (W == 8) szp8(uint8_t(res));
    else        szp16(uint16_t(res));
    return res;
}

template <int W>
inline uint32_t Cpu::dec(uint32_t a)
{
    constexpr uint32_t M = W == 8 ? 0xffu : 0xffffu;
    constexpr uint32_t S = W == 8 ? 0x80u : 0x8000u;
    const uint32_t res = (a - 1) & M;
    of_ = res == S - 1;
    af_ = (res & 0xf) == 0xf;
    if (W == 8) szp8(uint8_t(res));
    else        szp16(uint16_t(res));
    return res;
}

// D0-D3: rotates, shifts and SETMO, one bit at a time as the chip does (so
// OF is the last step's). A count of 0 changes nothing.
template <int W>
uint32_t Cpu::shift(int op, uint32_t v, int count)
{
    constexpr uint32_t M = W == 8 ? 0xffu : 0xffffu;
    constexpr int T = W - 1;
    if (count == 0) return v;
    for (int i = 0; i < count; ++i) {
        uint32_t c;
        switch (op) {
        case 0:     // ROL
            cf_ = (v >> T) & 1;
            v = ((v << 1) | cf_) & M;
            of_ = ((v >> T) & 1) ^ cf_;
            break;
        case 1:     // ROR
            cf_ = v & 1;
            v = (v >> 1) | (uint32_t(cf_) << T);
            of_ = ((v >> T) ^ (v >> (T - 1))) & 1;
            break;
        case 2:     // RCL
            c = (v >> T) & 1;
            v = ((v << 1) | cf_) & M;
            cf_ = uint8_t(c);
            of_ = ((v >> T) & 1) ^ cf_;
            break;
        case 3:     // RCR
            c = v & 1;
            v = (v >> 1) | (uint32_t(cf_) << T);
            cf_ = uint8_t(c);
            of_ = ((v >> T) ^ (v >> (T - 1))) & 1;
            break;
        case 4:     // SHL
            cf_ = (v >> T) & 1;
            v = (v << 1) & M;
            of_ = ((v >> T) & 1) ^ cf_;
            break;
        case 5:     // SHR
            cf_ = v & 1;
            of_ = (v >> T) & 1;
            v >>= 1;
            break;
        case 6:     // SETMO (undocumented): all ones
            v = M;
            cf_ = of_ = 0;
            break;
        default:    // SAR
            cf_ = v & 1;
            v = (v >> 1) | (v & (1u << T));
            of_ = 0;
            break;
        }
    }
    if (op >= 4) {
        af_ = op == 6 ? 0 : af_;
        if (W == 8) szp8(uint8_t(v));
        else        szp16(uint16_t(v));
    }
    return v;
}

// F6 / F7: TEST, NOT, NEG, MUL, IMUL, DIV, IDIV.
template <int W>
void Cpu::group3(uint8_t /*op*/)
{
    constexpr uint32_t M = W == 8 ? 0xffu : 0xffffu;
    const bool mem = !is_reg();
    const uint32_t v = W == 8 ? get_rm8() : get_rm16();
    const int wx = (W == 16 && mem) ? 4 : 0;     // the 8088's extra word transfer
    switch (reg_field()) {
    case 0:
    case 1: {   // TEST (1 is an alias)
        const uint32_t imm = W == 8 ? fetch8() : fetch16();
        alu<W>(kAnd, v, imm);
        cyc_ += mem ? 11 + wx : 5;
        return;
    }
    case 2:     // NOT
        if (W == 8) set_rm8(uint8_t(~v));
        else        set_rm16(uint16_t(~v));
        cyc_ += mem ? 16 + 2 * wx : 3;
        return;
    case 3: {   // NEG
        const uint32_t res = alu<W>(kSub, 0, v);
        if (W == 8) set_rm8(uint8_t(res));
        else        set_rm16(uint16_t(res));
        cyc_ += mem ? 16 + 2 * wx : 3;
        return;
    }
    case 4:     // MUL
        if (W == 8) {
            const uint16_t p = uint16_t(uint8_t(r[AX]) * v);
            r[AX] = p;
            cf_ = of_ = (p >> 8) != 0;
            szp8(uint8_t(p >> 8));
            cyc_ += mem ? 76 : 70;
        } else {
            const uint32_t p = uint32_t(r[AX]) * v;
            r[AX] = uint16_t(p);
            r[DX] = uint16_t(p >> 16);
            cf_ = of_ = r[DX] != 0;
            szp16(r[DX]);
            cyc_ += mem ? 124 + wx : 118;
        }
        return;
    case 5:     // IMUL
        if (W == 8) {
            const int16_t p = int16_t(int8_t(uint8_t(r[AX])) * int8_t(uint8_t(v)));
            r[AX] = uint16_t(p);
            cf_ = of_ = p != int8_t(uint8_t(p));
            szp8(uint8_t(uint16_t(p) >> 8));
            cyc_ += mem ? 86 : 80;
        } else {
            const int32_t p = int32_t(int16_t(r[AX])) * int16_t(uint16_t(v));
            r[AX] = uint16_t(p);
            r[DX] = uint16_t(uint32_t(p) >> 16);
            cf_ = of_ = p != int16_t(uint16_t(p));
            szp16(r[DX]);
            cyc_ += mem ? 134 + wx : 128;
        }
        return;
    case 6:     // DIV
        if (W == 8) {
            cyc_ += mem ? 86 : 80;
            if (v == 0 || (r[AX] / v) > M) { interrupt(0); return; }
            const uint16_t n = r[AX];
            r[AX] = uint16_t((n / v) | (n % v) << 8);
        } else {
            cyc_ += mem ? 150 + wx : 144;
            const uint32_t n = uint32_t(r[DX]) << 16 | r[AX];
            if (v == 0 || n / v > M) { interrupt(0); return; }
            r[AX] = uint16_t(n / v);
            r[DX] = uint16_t(n % v);
        }
        return;
    default: {  // IDIV
        constexpr int64_t kMax = W == 8 ? 127 : 32767;
        int64_t n, d;
        if (W == 8) { n = int16_t(r[AX]); d = int8_t(uint8_t(v)); cyc_ += mem ? 107 : 101; }
        else { n = int32_t(uint32_t(r[DX]) << 16 | r[AX]); d = int16_t(uint16_t(v)); cyc_ += mem ? 171 + wx : 165; }
        if (d == 0) { interrupt(0); return; }
        int64_t q = n / d;
        const int64_t rem = n % d;
        if (rep_) q = -q;       // the 8088's REP quirk: the quotient's sign flips
        if (q > kMax || q < -kMax) { interrupt(0); return; }
        if (W == 8) r[AX] = uint16_t((uint8_t(q)) | (uint8_t(rem) << 8));
        else { r[AX] = uint16_t(q); r[DX] = uint16_t(rem); }
        return;
    }
    }
}

inline bool Cpu::cond(int cc) const
{
    bool t;
    switch (cc >> 1) {
    case 0:  t = of_; break;
    case 1:  t = cf_; break;
    case 2:  t = zf_; break;
    case 3:  t = cf_ | zf_; break;
    case 4:  t = sf_; break;
    case 5:  t = pf_; break;
    case 6:  t = sf_ != of_; break;
    default: t = zf_ || (sf_ != of_); break;
    }
    return t != (cc & 1);
}

void Cpu::interrupt(uint8_t vector)
{
    push(flags());
    if_ = tf_ = 0;
    push(sreg[CS]);
    push(ip);
    ip = mrd16(0, uint16_t(vector * 4));
    sreg[CS] = mrd16(0, uint16_t(vector * 4 + 2));
    halted_ = false;
    cyc_ += 51 + 12;    // 51 documented + the 8088's three word pushes
}

// One element of a string instruction. Returns false when a REPE / REPNE
// compare ends the repetition.
inline bool Cpu::string_op(uint8_t op)
{
    const int step = (op & 1) ? 2 : 1;
    const uint16_t d = uint16_t(df_ ? -step : step);
    switch (op) {
    case 0xa4: mwr8(sreg[ES], r[DI], mrd8(dseg(DS), r[SI])); r[SI] += d; r[DI] += d; cyc_ += 17; break;
    case 0xa5: mwr16(sreg[ES], r[DI], mrd16(dseg(DS), r[SI])); r[SI] += d; r[DI] += d; cyc_ += 25; break;
    case 0xa6:
    case 0xa7: {
        if (op == 0xa6) alu<8>(kCmp, mrd8(dseg(DS), r[SI]), mrd8(sreg[ES], r[DI]));
        else            alu<16>(kCmp, mrd16(dseg(DS), r[SI]), mrd16(sreg[ES], r[DI]));
        r[SI] += d;
        r[DI] += d;
        cyc_ += op == 0xa6 ? 22 : 30;
        return rep_ == 0 || zf_ == (rep_ == 0xf3);
    }
    case 0xaa: mwr8(sreg[ES], r[DI], uint8_t(r[AX])); r[DI] += d; cyc_ += 10; break;
    case 0xab: mwr16(sreg[ES], r[DI], r[AX]); r[DI] += d; cyc_ += 14; break;
    case 0xac: r8(0) = mrd8(dseg(DS), r[SI]); r[SI] += d; cyc_ += 13; break;
    case 0xad: r[AX] = mrd16(dseg(DS), r[SI]); r[SI] += d; cyc_ += 17; break;
    case 0xae:
    case 0xaf:
        if (op == 0xae) alu<8>(kCmp, uint8_t(r[AX]), mrd8(sreg[ES], r[DI]));
        else            alu<16>(kCmp, r[AX], mrd16(sreg[ES], r[DI]));
        r[DI] += d;
        cyc_ += op == 0xae ? 15 : 19;
        return rep_ == 0 || zf_ == (rep_ == 0xf3);
    default: break;
    }
    return true;
}

void Cpu::daa()
{
    const uint8_t al = uint8_t(r[AX]);
    const uint8_t old_cf = cf_, old_af = af_;
    uint8_t v = al;
    if ((al & 0x0f) > 9 || af_) {
        v = uint8_t(v + 6);
        af_ = 1;
    } else {
        af_ = 0;
    }
    // The 8088 compares with 9F instead of 99 when AF came in set.
    if (al > (old_af ? 0x9f : 0x99) || old_cf) {
        v = uint8_t(v + 0x60);
        cf_ = 1;
    } else {
        cf_ = 0;
    }
    // OF is undefined; the chip leaves the overflow of the last addition.
    of_ = ((al ^ v) & ~al & 0x80) != 0;
    r8(0) = v;
    szp8(v);
}

void Cpu::das()
{
    const uint8_t al = uint8_t(r[AX]);
    const uint8_t old_cf = cf_, old_af = af_;
    uint8_t v = al;
    if ((al & 0x0f) > 9 || af_) {
        v = uint8_t(v - 6);
        af_ = 1;
    } else {
        af_ = 0;
    }
    // The 8088 compares with 9F instead of 99 when AF came in set.
    if (al > (old_af ? 0x9f : 0x99) || old_cf) {
        v = uint8_t(v - 0x60);
        cf_ = 1;
    } else {
        cf_ = 0;
    }
    of_ = ((al ^ v) & al & 0x80) != 0;
    r8(0) = v;
    szp8(v);
}

void Cpu::aaa()
{
    uint8_t al = uint8_t(r[AX]);
    if ((al & 0x0f) > 9 || af_) {
        al = uint8_t(al + 6);
        r8(4) = uint8_t(r8(4) + 1);
        af_ = cf_ = 1;
    } else {
        af_ = cf_ = 0;
    }
    szp8(al);
    r8(0) = al & 0x0f;
}

void Cpu::aas()
{
    uint8_t al = uint8_t(r[AX]);
    if ((al & 0x0f) > 9 || af_) {
        al = uint8_t(al - 6);
        r8(4) = uint8_t(r8(4) - 1);
        af_ = cf_ = 1;
    } else {
        af_ = cf_ = 0;
    }
    szp8(al);
    r8(0) = al & 0x0f;
}

int Cpu::run(int cycles)
{
    target_ = cycles;
    done_ = 0;
    while (done_ < target_) {
        if (!inhibit_ && (nmi_pending_ || (intr_ && if_))) {
            cyc_ = 0;
            if (nmi_pending_) {
                nmi_pending_ = false;
                interrupt(2);
            } else {
                cyc_ += 10;     // the INTA cycles
                interrupt(int_ack(ctx));
            }
            done_ += cyc_;
            continue;
        }
        if (halted_) {
            done_ = target_ > done_ ? target_ : done_;
            break;
        }
        inhibit_ = false;
        done_ += step();
    }
    return done_;
}

int Cpu::step_instruction()
{
    target_ = 0x7fffffff;
    done_ = 0;
    halted_ = false;
    const int c = step();
    target_ = 0;
    return c;
}

int Cpu::step()
{
    ++instructions;
    cyc_ = 0;
    fetched_ = 0;
    seg_ovr_ = -1;
    rep_ = 0;
    start_ip_ = ip;
    uint8_t op;
    for (;;) {
        op = fetch8();
        switch (op) {
        case 0x26: seg_ovr_ = ES; cyc_ += 2; continue;
        case 0x2e: seg_ovr_ = CS; cyc_ += 2; continue;
        case 0x36: seg_ovr_ = SS; cyc_ += 2; continue;
        case 0x3e: seg_ovr_ = DS; cyc_ += 2; continue;
        case 0xf0: case 0xf1: cyc_ += 2; continue;       // LOCK
        case 0xf2: case 0xf3: rep_ = op; cyc_ += 2; continue;
        default: break;
        }
        break;
    }

    switch (op) {
    // --- ALU: ADD OR ADC SBB AND SUB XOR CMP ------------------------------
    case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: {
        modrm();
        const int a = op >> 3;
        const uint8_t v = uint8_t(alu<8>(a, get_rm8(), r8(reg_field())));
        if (a != kCmp) set_rm8(v);
        cyc_ += is_reg() ? 3 : (a == kCmp ? 9 : 16);
        break;
    }
    case 0x01: case 0x09: case 0x11: case 0x19: case 0x21: case 0x29: case 0x31: case 0x39: {
        modrm();
        const int a = op >> 3;
        const uint16_t v = uint16_t(alu<16>(a, get_rm16(), r[reg_field()]));
        if (a != kCmp) set_rm16(v);
        cyc_ += is_reg() ? 3 : (a == kCmp ? 13 : 24);
        break;
    }
    case 0x02: case 0x0a: case 0x12: case 0x1a: case 0x22: case 0x2a: case 0x32: case 0x3a: {
        modrm();
        const int a = op >> 3;
        uint8_t& d = r8(reg_field());
        const uint8_t v = uint8_t(alu<8>(a, d, get_rm8()));
        if (a != kCmp) d = v;
        cyc_ += is_reg() ? 3 : 9;
        break;
    }
    case 0x03: case 0x0b: case 0x13: case 0x1b: case 0x23: case 0x2b: case 0x33: case 0x3b: {
        modrm();
        const int a = op >> 3;
        uint16_t& d = r[reg_field()];
        const uint16_t v = uint16_t(alu<16>(a, d, get_rm16()));
        if (a != kCmp) d = v;
        cyc_ += is_reg() ? 3 : 13;
        break;
    }
    case 0x04: case 0x0c: case 0x14: case 0x1c: case 0x24: case 0x2c: case 0x34: case 0x3c: {
        const int a = op >> 3;
        const uint8_t v = uint8_t(alu<8>(a, uint8_t(r[AX]), fetch8()));
        if (a != kCmp) r8(0) = v;
        cyc_ += 4;
        break;
    }
    case 0x05: case 0x0d: case 0x15: case 0x1d: case 0x25: case 0x2d: case 0x35: case 0x3d: {
        const int a = op >> 3;
        const uint16_t v = uint16_t(alu<16>(a, r[AX], fetch16()));
        if (a != kCmp) r[AX] = v;
        cyc_ += 4;
        break;
    }

    // --- segment push / pop, BCD ------------------------------------------
    case 0x06: case 0x0e: case 0x16: case 0x1e: push(sreg[op >> 3]); cyc_ += 14; break;
    case 0x07: case 0x0f: case 0x17: case 0x1f:
        sreg[op >> 3] = pop();
        if (op == 0x17) inhibit_ = true;
        cyc_ += 12;
        break;
    case 0x27: daa(); cyc_ += 4; break;
    case 0x2f: das(); cyc_ += 4; break;
    case 0x37: aaa(); cyc_ += 8; break;
    case 0x3f: aas(); cyc_ += 8; break;

    // --- INC / DEC / PUSH / POP r16 ---------------------------------------
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        r[op & 7] = uint16_t(inc<16>(r[op & 7]));
        cyc_ += 2;
        break;
    case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
        r[op & 7] = uint16_t(dec<16>(r[op & 7]));
        cyc_ += 2;
        break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57:
        push(r[op & 7]);
        cyc_ += 15;
        break;
    case 0x54:      // PUSH SP: the 8086 pushes the decremented SP
        r[SP] = uint16_t(r[SP] - 2);
        mwr16(sreg[SS], r[SP], r[SP]);
        cyc_ += 15;
        break;
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        r[op & 7] = pop();
        cyc_ += 12;
        break;

    // --- conditional jumps (60-6F are the same on the 8088) ---------------
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
        const int8_t d = int8_t(fetch8());
        if (cond(op & 15)) { ip = uint16_t(ip + d); cyc_ += 16; }
        else cyc_ += 4;
        break;
    }

    // --- group 1: ALU r/m, imm --------------------------------------------
    case 0x80: case 0x82: {
        modrm();
        const int a = reg_field();
        const uint8_t v = uint8_t(alu<8>(a, get_rm8(), fetch8()));
        if (a != kCmp) set_rm8(v);
        cyc_ += is_reg() ? 4 : (a == kCmp ? 10 : 17);
        break;
    }
    case 0x81: case 0x83: {
        modrm();
        const int a = reg_field();
        const uint16_t lhs = get_rm16();
        const uint16_t imm = op == 0x81 ? fetch16() : uint16_t(int8_t(fetch8()));
        const uint16_t v = uint16_t(alu<16>(a, lhs, imm));
        if (a != kCmp) set_rm16(v);
        cyc_ += is_reg() ? 4 : (a == kCmp ? 14 : 25);
        break;
    }

    // --- TEST, XCHG, MOV --------------------------------------------------
    case 0x84: modrm(); alu<8>(kAnd, get_rm8(), r8(reg_field())); cyc_ += is_reg() ? 3 : 9; break;
    case 0x85: modrm(); alu<16>(kAnd, get_rm16(), r[reg_field()]); cyc_ += is_reg() ? 3 : 13; break;
    case 0x86: {
        modrm();
        const uint8_t t = get_rm8();
        set_rm8(r8(reg_field()));
        r8(reg_field()) = t;
        cyc_ += is_reg() ? 4 : 17;
        break;
    }
    case 0x87: {
        modrm();
        const uint16_t t = get_rm16();
        set_rm16(r[reg_field()]);
        r[reg_field()] = t;
        cyc_ += is_reg() ? 4 : 25;
        break;
    }
    case 0x88: modrm(); set_rm8(r8(reg_field())); cyc_ += is_reg() ? 2 : 9; break;
    case 0x89: modrm(); set_rm16(r[reg_field()]); cyc_ += is_reg() ? 2 : 13; break;
    case 0x8a: modrm(); r8(reg_field()) = get_rm8(); cyc_ += is_reg() ? 2 : 8; break;
    case 0x8b: modrm(); r[reg_field()] = get_rm16(); cyc_ += is_reg() ? 2 : 12; break;
    case 0x8c: modrm(); set_rm16(sreg[reg_field() & 3]); cyc_ += is_reg() ? 2 : 13; break;
    case 0x8d:      // LEA (the register form is undefined: keeps the last EA)
        modrm();
        r[reg_field()] = ea_off_;
        cyc_ += 2;
        break;
    case 0x8e: {
        modrm();
        const int s = reg_field() & 3;
        sreg[s] = get_rm16();
        if (s == SS) inhibit_ = true;
        cyc_ += is_reg() ? 2 : 12;
        break;
    }
    case 0x8f: {    // POP r/m (reg != 0 undefined: the same)
        const uint16_t v = pop();
        modrm();
        set_rm16(v);
        cyc_ += is_reg() ? 12 : 25;
        break;
    }

    // --- XCHG AX, CBW, CWD, CALL far, flags -------------------------------
    case 0x90: cyc_ += 3; break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        const uint16_t t = r[AX];
        r[AX] = r[op & 7];
        r[op & 7] = t;
        cyc_ += 3;
        break;
    }
    case 0x98: r[AX] = uint16_t(int8_t(uint8_t(r[AX]))); cyc_ += 2; break;
    case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xffff : 0; cyc_ += 5; break;
    case 0x9a: {
        const uint16_t off = fetch16();
        const uint16_t seg = fetch16();
        push(sreg[CS]);
        push(ip);
        sreg[CS] = seg;
        ip = off;
        cyc_ += 36;
        break;
    }
    case 0x9b: cyc_ += 4; break;                         // WAIT
    case 0x9c: push(flags()); cyc_ += 14; break;
    case 0x9d: set_flags(pop()); cyc_ += 12; break;
    case 0x9e: {    // SAHF
        const uint8_t ah = r8(4);
        cf_ = ah & 1;
        pf_ = (ah >> 2) & 1;
        af_ = (ah >> 4) & 1;
        zf_ = (ah >> 6) & 1;
        sf_ = (ah >> 7) & 1;
        cyc_ += 4;
        break;
    }
    case 0x9f: r8(4) = uint8_t(flags()); cyc_ += 4; break; // LAHF

    // --- MOV acc <-> moffs, string instructions, TEST acc -----------------
    case 0xa0: r8(0) = mrd8(dseg(DS), fetch16()); cyc_ += 10; break;
    case 0xa1: r[AX] = mrd16(dseg(DS), fetch16()); cyc_ += 14; break;
    case 0xa2: mwr8(dseg(DS), fetch16(), uint8_t(r[AX])); cyc_ += 10; break;
    case 0xa3: mwr16(dseg(DS), fetch16(), r[AX]); cyc_ += 14; break;
    case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab: case 0xac: case 0xad:
    case 0xae: case 0xaf:
        if (!rep_) {
            cyc_ += 1;
            string_op(op);
            break;
        }
        cyc_ += 9;
        while (r[CX] != 0) {
            const bool go_on = string_op(op);
            r[CX] = uint16_t(r[CX] - 1);
            if (!go_on || r[CX] == 0) break;
            if (done_ + cyc_ >= target_ || nmi_pending_ || (intr_ && if_)) {
                ip = start_ip_;     // resume the repetition later
                break;
            }
        }
        break;
    case 0xa8: alu<8>(kAnd, uint8_t(r[AX]), fetch8()); cyc_ += 4; break;
    case 0xa9: alu<16>(kAnd, r[AX], fetch16()); cyc_ += 4; break;

    // --- MOV r, imm -------------------------------------------------------
    case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
        r8(op & 7) = fetch8();
        cyc_ += 4;
        break;
    case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
        r[op & 7] = fetch16();
        cyc_ += 4;
        break;

    // --- returns, LES / LDS, MOV r/m imm, INT -----------------------------
    case 0xc0: case 0xc2: {
        const uint16_t n = fetch16();
        ip = pop();
        r[SP] = uint16_t(r[SP] + n);
        cyc_ += 27;
        break;
    }
    case 0xc1: case 0xc3: ip = pop(); cyc_ += 20; break;
    case 0xc4: case 0xc5:
        modrm();
        r[reg_field()] = mrd16(ea_seg_, ea_off_);
        sreg[op == 0xc4 ? ES : DS] = mrd16(ea_seg_, uint16_t(ea_off_ + 2));
        cyc_ += 24;
        break;
    case 0xc6: modrm(); set_rm8(fetch8()); cyc_ += is_reg() ? 4 : 10; break;
    case 0xc7: modrm(); set_rm16(fetch16()); cyc_ += is_reg() ? 4 : 14; break;
    case 0xc8: case 0xca: {
        const uint16_t n = fetch16();
        ip = pop();
        sreg[CS] = pop();
        r[SP] = uint16_t(r[SP] + n);
        cyc_ += 38;
        break;
    }
    case 0xc9: case 0xcb: ip = pop(); sreg[CS] = pop(); cyc_ += 34; break;
    case 0xcc: interrupt(3); cyc_ += 1; break;
    case 0xcd: interrupt(fetch8()); break;
    case 0xce: if (of_) { interrupt(4); cyc_ += 2; } else cyc_ += 4; break;
    case 0xcf:
        ip = pop();
        sreg[CS] = pop();
        set_flags(pop());
        cyc_ += 36;
        break;

    // --- shifts, BCD, XLAT, escapes ---------------------------------------
    case 0xd0: case 0xd2: {
        modrm();
        const int n = op == 0xd0 ? 1 : uint8_t(r[CX]);
        set_rm8(uint8_t(shift<8>(reg_field(), get_rm8(), n)));
        cyc_ += op == 0xd0 ? (is_reg() ? 2 : 15) : (is_reg() ? 8 : 20) + 4 * n;
        break;
    }
    case 0xd1: case 0xd3: {
        modrm();
        const int n = op == 0xd1 ? 1 : uint8_t(r[CX]);
        set_rm16(uint16_t(shift<16>(reg_field(), get_rm16(), n)));
        cyc_ += op == 0xd1 ? (is_reg() ? 2 : 23) : (is_reg() ? 8 : 28) + 4 * n;
        break;
    }
    case 0xd4: {    // AAM
        const uint8_t base = fetch8();
        cyc_ += 83;
        if (base == 0) {    // the chip's flags at the trap: as for a zero result
            szp8(0);
            interrupt(0);
            break;
        }
        const uint8_t al = uint8_t(r[AX]);
        r[AX] = uint16_t((al / base) << 8 | (al % base));
        szp8(uint8_t(r[AX]));
        break;
    }
    case 0xd5: {    // AAD
        const uint8_t base = fetch8();
        const uint8_t v = uint8_t(alu<8>(kAdd, uint8_t(r[AX]), uint8_t(r8(4) * base)));
        r[AX] = v;
        cyc_ += 60;
        break;
    }
    case 0xd6: r8(0) = cf_ ? 0xff : 0; cyc_ += 4; break;  // SALC (undocumented)
    case 0xd7: r8(0) = mrd8(dseg(DS), uint16_t(r[BX] + uint8_t(r[AX]))); cyc_ += 11; break;
    case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
        modrm();    // ESC: no coprocessor; the operand is read and dropped
        if (!is_reg()) mrd8(ea_seg_, ea_off_);
        cyc_ += 8;
        break;

    // --- loops, I/O, jumps and calls --------------------------------------
    case 0xe0: case 0xe1: case 0xe2: {
        const int8_t d = int8_t(fetch8());
        r[CX] = uint16_t(r[CX] - 1);
        bool t = r[CX] != 0;
        if (op == 0xe0) t = t && !zf_;
        else if (op == 0xe1) t = t && zf_;
        if (t) { ip = uint16_t(ip + d); cyc_ += 17; }
        else cyc_ += 5;
        break;
    }
    case 0xe3: {
        const int8_t d = int8_t(fetch8());
        if (r[CX] == 0) { ip = uint16_t(ip + d); cyc_ += 18; }
        else cyc_ += 6;
        break;
    }
    case 0xe4: r8(0) = io_read(ctx, fetch8()); cyc_ += 10; break;
    case 0xe5: {
        const uint8_t p = fetch8();
        r[AX] = uint16_t(io_read(ctx, p) | io_read(ctx, uint16_t(p + 1)) << 8);
        cyc_ += 14;
        break;
    }
    case 0xe6: io_write(ctx, fetch8(), uint8_t(r[AX])); cyc_ += 10; break;
    case 0xe7: {
        const uint8_t p = fetch8();
        io_write(ctx, p, uint8_t(r[AX]));
        io_write(ctx, uint16_t(p + 1), uint8_t(r[AX] >> 8));
        cyc_ += 14;
        break;
    }
    case 0xe8: {
        const uint16_t d = fetch16();
        push(ip);
        ip = uint16_t(ip + d);
        cyc_ += 23;
        break;
    }
    case 0xe9: { const uint16_t d = fetch16(); ip = uint16_t(ip + d); cyc_ += 20; break; }
    case 0xea: {
        const uint16_t off = fetch16();
        sreg[CS] = fetch16();
        ip = off;
        cyc_ += 25;
        break;
    }
    case 0xeb: { const int8_t d = int8_t(fetch8()); ip = uint16_t(ip + d); cyc_ += 15; break; }
    case 0xec: r8(0) = io_read(ctx, r[DX]); cyc_ += 8; break;
    case 0xed:
        r[AX] = uint16_t(io_read(ctx, r[DX]) | io_read(ctx, uint16_t(r[DX] + 1)) << 8);
        cyc_ += 12;
        break;
    case 0xee: io_write(ctx, r[DX], uint8_t(r[AX])); cyc_ += 8; break;
    case 0xef:
        io_write(ctx, r[DX], uint8_t(r[AX]));
        io_write(ctx, uint16_t(r[DX] + 1), uint8_t(r[AX] >> 8));
        cyc_ += 12;
        break;

    // --- HLT, flags, groups 3 / 4 / 5 -------------------------------------
    case 0xf4: halted_ = true; cyc_ += 2; break;
    case 0xf5: cf_ ^= 1; cyc_ += 2; break;
    case 0xf6: modrm(); group3<8>(op); break;
    case 0xf7: modrm(); group3<16>(op); break;
    case 0xf8: cf_ = 0; cyc_ += 2; break;
    case 0xf9: cf_ = 1; cyc_ += 2; break;
    case 0xfa: if_ = 0; cyc_ += 2; break;
    case 0xfb: if_ = 1; cyc_ += 2; break;
    case 0xfc: df_ = 0; cyc_ += 2; break;
    case 0xfd: df_ = 1; cyc_ += 2; break;
    case 0xfe: {    // INC / DEC r/m8 (reg 2-7 undefined: treated as INC / DEC)
        modrm();
        const uint8_t v = get_rm8();
        set_rm8(uint8_t((reg_field() & 1) ? dec<8>(v) : inc<8>(v)));
        cyc_ += is_reg() ? 3 : 15;
        break;
    }
    case 0xff: {
        modrm();
        const bool mem = !is_reg();
        switch (reg_field()) {
        case 0: set_rm16(uint16_t(inc<16>(get_rm16()))); cyc_ += mem ? 23 : 2; break;
        case 1: set_rm16(uint16_t(dec<16>(get_rm16()))); cyc_ += mem ? 23 : 2; break;
        case 2: {
            const uint16_t t = get_rm16();
            push(ip);
            ip = t;
            cyc_ += mem ? 29 : 20;
            break;
        }
        case 3: {   // CALL far [mem]
            const uint16_t off = mrd16(ea_seg_, ea_off_);
            const uint16_t seg = mrd16(ea_seg_, uint16_t(ea_off_ + 2));
            push(sreg[CS]);
            push(ip);
            sreg[CS] = seg;
            ip = off;
            cyc_ += 53;
            break;
        }
        case 4: ip = get_rm16(); cyc_ += mem ? 22 : 11; break;
        case 5: {   // JMP far [mem]
            const uint16_t off = mrd16(ea_seg_, ea_off_);
            sreg[CS] = mrd16(ea_seg_, uint16_t(ea_off_ + 2));
            ip = off;
            cyc_ += 32;
            break;
        }
        default: {  // PUSH r/m (7 is an alias); PUSH SP pushes the new SP
            if (!mem && (modrm_ & 7) == SP) {
                r[SP] = uint16_t(r[SP] - 2);
                mwr16(sreg[SS], r[SP], r[SP]);
            } else {
                push(get_rm16());
            }
            cyc_ += mem ? 24 : 15;
            break;
        }
        }
        break;
    }
    default: cyc_ += 2; break;      // nothing left: every opcode is handled above
    }
    // The 8088 fetches each instruction byte over its 8-bit bus in 4 cycles,
    // so short instructions run at the speed of their fetches.
    return cyc_ > 4 * fetched_ ? cyc_ : 4 * fetched_;
}

} // namespace i8088
