// Unit tests for the 6809 core (emu/cpu/m6809.*), with no test program
// from outside: every 8-bit ALU result and flag against a plain signed /
// unsigned model for all operands, 16-bit ALU on random operands, DAA by
// decimal sums, MUL for all operands, every branch condition, every indexed
// mode's address, side effect and cycles, the datasheet's cycle counts,
// TFR / EXG, the stack, interrupts (IRQ, FIRQ, NMI armed by LDS, CWAI, SYNC,
// SWI 1-3, RTI both ways) and a few hand-assembled programs.
//
//   m6809_tests
#include "../../emu/cpu/m6809.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) ++g_pass;                                                           \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        const long long va_ = (long long)(a), vb_ = (long long)(b);                        \
        if (va_ == vb_) ++g_pass;                                                          \
        else { ++g_fail; std::printf("FAIL %s:%d: %s = %lld (0x%llx), want %lld (0x%llx)\n", \
                                     __FILE__, __LINE__, #a, va_, va_, vb_, vb_); }        \
    } while (0)

using namespace m6809;

// A 6809 with 64 KB of RAM.
struct Rig {
    uint8_t mem[0x10000] = {};
    Cpu cpu;
    Rig()
    {
        cpu.map_ram(0x0000, 0x10000, mem);
        set_vec(kVecReset, 0x1000);
        cpu.reset();
        cpu.s = 0x8000;
        cpu.u = 0x7000;
    }
    void set_vec(uint16_t v, uint16_t to) { mem[v] = uint8_t(to >> 8); mem[v + 1] = uint8_t(to); }
    void put(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) mem[at++] = b;
    }
    // Puts the bytes at 0x1000 and runs one instruction; returns its cycles.
    int one(std::initializer_list<uint8_t> bytes)
    {
        put(0x1000, bytes);
        cpu.pc = 0x1000;
        return cpu.run(1);
    }
};

uint32_t g_rand = 0x12345678;
uint32_t rnd()
{
    g_rand ^= g_rand << 13;
    g_rand ^= g_rand >> 17;
    g_rand ^= g_rand << 5;
    return g_rand;
}

// --- 8-bit ALU -----------------------------------------------------------------------

// Flags of a + b + c by a plain model: signed sum for V, unsigned for C.
uint8_t add_flags(int a, int b, int c, uint8_t cc_in)
{
    const int u = a + b + c;
    const int sres = int8_t(a) + int8_t(b) + c;
    uint8_t f = cc_in & ~(CC_H | CC_N | CC_Z | CC_V | CC_C);
    if (((a & 15) + (b & 15) + c) > 15) f |= CC_H;
    if (u & 0x80) f |= CC_N;
    if ((u & 0xff) == 0) f |= CC_Z;
    if (sres < -128 || sres > 127) f |= CC_V;
    if (u > 255) f |= CC_C;
    return f;
}

uint8_t sub_flags(int a, int b, int c, uint8_t cc_in)
{
    const int u = a - b - c;
    const int sres = int8_t(a) - int8_t(b) - c;
    uint8_t f = cc_in & ~(CC_N | CC_Z | CC_V | CC_C);
    if (u & 0x80) f |= CC_N;
    if ((u & 0xff) == 0) f |= CC_Z;
    if (sres < -128 || sres > 127) f |= CC_V;
    if (u < 0) f |= CC_C;
    return f;
}

void test_alu8()
{
    Rig r;
    int bad = 0;
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            for (int c = 0; c < 2; ++c) {
                const uint8_t cc0 = uint8_t(c ? CC_C : 0) | CC_I | CC_F;
                struct { uint8_t op; bool add, carry, store; } ops[] = {
                    {0x8b, true, false, true},   // ADDA
                    {0x89, true, true, true},    // ADCA
                    {0x80, false, false, true},  // SUBA
                    {0x82, false, true, true},   // SBCA
                    {0x81, false, false, false}, // CMPA
                };
                for (const auto& o : ops) {
                    r.cpu.r.a = uint8_t(a);
                    r.cpu.cc = cc0;
                    const int cyc = r.one({o.op, uint8_t(b)});
                    const int cin = o.carry ? c : 0;
                    const int want = o.add ? (a + b + cin) & 0xff : (a - b - cin) & 0xff;
                    const uint8_t wf = o.add ? add_flags(a, b, cin, cc0) : sub_flags(a, b, cin, cc0);
                    if (r.cpu.r.a != (o.store ? want : a) || r.cpu.cc != wf || cyc != 2) {
                        if (bad++ < 10)
                            std::printf("FAIL alu op %02x a %02x b %02x c %d: got a %02x cc %02x, want %02x cc %02x\n",
                                        o.op, a, b, c, r.cpu.r.a, r.cpu.cc, o.store ? want : a, wf);
                    }
                }
                // The B forms (ADDB, SBCB) on the same operands.
                r.cpu.r.b = uint8_t(a);
                r.cpu.cc = cc0;
                r.one({0xcb, uint8_t(b)});
                if (r.cpu.r.b != ((a + b) & 0xff) || r.cpu.cc != add_flags(a, b, 0, cc0)) ++bad;
                r.cpu.r.b = uint8_t(a);
                r.cpu.cc = cc0;
                r.one({0xc2, uint8_t(b)});
                if (r.cpu.r.b != ((a - b - c) & 0xff) || r.cpu.cc != sub_flags(a, b, c, cc0)) ++bad;
                // Logic: N Z, V cleared, C kept.
                const uint8_t logic_ops[3] = {0x84, 0x8a, 0x88};    // AND OR EOR
                for (int k = 0; k < 3; ++k) {
                    r.cpu.r.a = uint8_t(a);
                    r.cpu.cc = cc0 | CC_V;
                    r.one({logic_ops[k], uint8_t(b)});
                    const int v = k == 0 ? a & b : k == 1 ? a | b : a ^ b;
                    const uint8_t wf = uint8_t((cc0 & ~(CC_N | CC_Z | CC_V)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z));
                    if (r.cpu.r.a != v || r.cpu.cc != wf) ++bad;
                }
                // BITA: like AND, A kept.
                r.cpu.r.a = uint8_t(a);
                r.cpu.cc = cc0 | CC_V;
                r.one({0x85, uint8_t(b)});
                if (r.cpu.r.a != a || (r.cpu.cc & CC_V) || bool(r.cpu.cc & CC_Z) != ((a & b) == 0)) ++bad;
            }
    CHECK_EQ(bad, 0);
}

// The read-modify-write group on A, every value, both carries.
void test_unary()
{
    Rig r;
    int bad = 0;
    for (int v = 0; v < 256; ++v)
        for (int c = 0; c < 2; ++c) {
            const uint8_t cc0 = uint8_t((c ? CC_C : 0) | CC_H);
            auto nz = [](int x) { return uint8_t(((x & 0x80) ? CC_N : 0) | ((x & 0xff) ? 0 : CC_Z)); };
            struct Case { uint8_t op; int res; uint8_t flags; uint8_t mask; };
            const int neg = (-v) & 0xff, com = (~v) & 0xff, lsr = v >> 1;
            const int ror = (v >> 1) | (c << 7), asr = (v >> 1) | (v & 0x80);
            const int asl = (v << 1) & 0xff, rol = ((v << 1) | c) & 0xff;
            const int dec = (v - 1) & 0xff, inc = (v + 1) & 0xff;
            const uint8_t nzvc = CC_N | CC_Z | CC_V | CC_C;
            Case cases[] = {
                {0x40, neg, uint8_t(nz(neg) | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0)), nzvc},
                {0x43, com, uint8_t(nz(com) | CC_C), nzvc},
                {0x44, lsr, uint8_t(nz(lsr) | (v & 1 ? CC_C : 0)), CC_N | CC_Z | CC_C},
                {0x46, ror, uint8_t(nz(ror) | (v & 1 ? CC_C : 0)), CC_N | CC_Z | CC_C},
                {0x47, asr, uint8_t(nz(asr) | (v & 1 ? CC_C : 0)), CC_N | CC_Z | CC_C},
                {0x48, asl, uint8_t(nz(asl) | (((v >> 7) ^ (v >> 6)) & 1 ? CC_V : 0) | (v & 0x80 ? CC_C : 0)), nzvc},
                {0x49, rol, uint8_t(nz(rol) | (((v >> 7) ^ (v >> 6)) & 1 ? CC_V : 0) | (v & 0x80 ? CC_C : 0)), nzvc},
                {0x4a, dec, uint8_t(nz(dec) | (v == 0x80 ? CC_V : 0)), CC_N | CC_Z | CC_V},
                {0x4c, inc, uint8_t(nz(inc) | (v == 0x7f ? CC_V : 0)), CC_N | CC_Z | CC_V},
                {0x4d, v, nz(v), CC_N | CC_Z | CC_V},
                {0x4f, 0, CC_Z, nzvc},
                // undocumented aliases
                {0x41, neg, uint8_t(nz(neg) | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0)), nzvc},
                {0x45, lsr, uint8_t(nz(lsr) | (v & 1 ? CC_C : 0)), CC_N | CC_Z | CC_C},
                {0x4b, dec, uint8_t(nz(dec) | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0)), nzvc},
                {0x4e, 0, uint8_t(CC_Z | (c ? CC_C : 0)), nzvc},
                {0x42, c ? com : neg, c ? uint8_t(nz(com) | CC_C)
                                       : uint8_t(nz(neg) | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0)), nzvc},
            };
            for (const Case& k : cases) {
                r.cpu.r.a = uint8_t(v);
                r.cpu.cc = cc0;
                const int cyc = r.one({k.op});
                const uint8_t want_cc = uint8_t((cc0 & ~k.mask) | k.flags);
                if (r.cpu.r.a != k.res || r.cpu.cc != want_cc || cyc != 2) {
                    if (bad++ < 10)
                        std::printf("FAIL unary %02x v %02x c %d: a %02x cc %02x, want %02x cc %02x\n", k.op, v, c,
                                    r.cpu.r.a, r.cpu.cc, k.res, want_cc);
                }
            }
            // The same on memory (direct, 6 cycles) for NEG and ROL.
            r.mem[0x0040] = uint8_t(v);
            r.cpu.dp = 0;
            r.cpu.cc = cc0;
            if (r.one({0x09, 0x40}) != 6 || r.mem[0x40] != rol) ++bad;
        }
    CHECK_EQ(bad, 0);
}

void test_alu16()
{
    Rig r;
    int bad = 0;
    for (int i = 0; i < 200000; ++i) {
        const uint16_t a = uint16_t(rnd()), b = uint16_t(i < 16 ? (i & 1 ? 0x8000 : 0) : rnd());
        const uint8_t cc0 = uint8_t(rnd() & 0x0f);
        // ADDD #b
        r.cpu.d = a;
        r.cpu.cc = cc0;
        int cyc = r.one({0xc3, uint8_t(b >> 8), uint8_t(b)});
        int u = a + b, s = int16_t(a) + int16_t(b);
        uint8_t wf = uint8_t((cc0 & ~0x0f) | ((u & 0x8000) ? CC_N : 0) | ((u & 0xffff) ? 0 : CC_Z)
                             | ((s < -32768 || s > 32767) ? CC_V : 0) | (u > 0xffff ? CC_C : 0));
        if (r.cpu.d != uint16_t(u) || r.cpu.cc != wf || cyc != 4) ++bad;
        // SUBD #b, CMPX #b, CMPY (page 2), CMPU (page 3)
        u = a - b;
        s = int16_t(a) - int16_t(b);
        wf = uint8_t((cc0 & ~0x0f) | ((u & 0x8000) ? CC_N : 0) | ((u & 0xffff) ? 0 : CC_Z)
                     | ((s < -32768 || s > 32767) ? CC_V : 0) | (u < 0 ? CC_C : 0));
        r.cpu.d = a;
        r.cpu.cc = cc0;
        cyc = r.one({0x83, uint8_t(b >> 8), uint8_t(b)});
        if (r.cpu.d != uint16_t(u) || r.cpu.cc != wf || cyc != 4) ++bad;
        r.cpu.x = a;
        r.cpu.cc = cc0;
        cyc = r.one({0x8c, uint8_t(b >> 8), uint8_t(b)});
        if (r.cpu.x != a || r.cpu.cc != wf || cyc != 4) ++bad;
        r.cpu.y = a;
        r.cpu.cc = cc0;
        cyc = r.one({0x10, 0x8c, uint8_t(b >> 8), uint8_t(b)});
        if (r.cpu.cc != wf || cyc != 5) ++bad;
        r.cpu.u = a;
        r.cpu.cc = cc0;
        cyc = r.one({0x11, 0x83, uint8_t(b >> 8), uint8_t(b)});
        if (r.cpu.cc != wf || cyc != 5) ++bad;
        r.cpu.d = a;
        r.cpu.cc = cc0;
        cyc = r.one({0x10, 0x83, uint8_t(b >> 8), uint8_t(b)});
        if (r.cpu.cc != wf || cyc != 5) ++bad;
    }
    CHECK_EQ(bad, 0);
}

// DAA after ADDA on every pair of BCD bytes gives the decimal sum.
void test_daa_mul()
{
    Rig r;
    int bad = 0;
    for (int x = 0; x < 100; ++x)
        for (int y = 0; y < 100; ++y)
            for (int c = 0; c < 2; ++c) {
                const uint8_t bx = uint8_t((x / 10) << 4 | (x % 10)), by = uint8_t((y / 10) << 4 | (y % 10));
                r.cpu.r.a = bx;
                r.cpu.cc = c ? CC_C : 0;
                r.put(0x1000, {0x89, by, 0x19});        // ADCA #by ; DAA
                r.cpu.pc = 0x1000;
                r.cpu.run(1);
                const int cyc = r.cpu.run(1);
                const int sum = x + y + c;
                const uint8_t want = uint8_t(((sum % 100) / 10) << 4 | (sum % 10));
                if (r.cpu.r.a != want || bool(r.cpu.cc & CC_C) != (sum >= 100) || cyc != 2
                    || bool(r.cpu.cc & CC_Z) != (want == 0) || (r.cpu.cc & CC_V)) {
                    if (bad++ < 5)
                        std::printf("FAIL daa %d+%d+%d: a %02x cc %02x\n", x, y, c, r.cpu.r.a, r.cpu.cc);
                }
            }
    CHECK_EQ(bad, 0);
    bad = 0;
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b) {
            r.cpu.r.a = uint8_t(a);
            r.cpu.r.b = uint8_t(b);
            r.cpu.cc = CC_N | CC_V;
            const int cyc = r.one({0x3d});
            const int p = a * b;
            const uint8_t wf = uint8_t(CC_N | CC_V | (p ? 0 : CC_Z) | ((p & 0x80) ? CC_C : 0));
            if (r.cpu.d != p || r.cpu.cc != wf || cyc != 11) ++bad;
        }
    CHECK_EQ(bad, 0);
}

// Every branch, short and long, against the conditions written out.
void test_branches()
{
    Rig r;
    int bad = 0;
    for (int f = 0; f < 16; ++f) {
        const bool c = f & 1, v = f & 2, z = f & 4, n = f & 8;
        const bool want[16] = {true, false, !(c || z), c || z, !c, c, !z, z, !v, v, !n, n,
                               n == v, n != v, !z && n == v, z || n != v};
        for (int k = 0; k < 16; ++k) {
            r.cpu.cc = uint8_t(f);
            int cyc = r.one({uint8_t(0x20 + k), 0x10});
            if (r.cpu.pc != (want[k] ? 0x1012 : 0x1002) || cyc != 3) ++bad;
            r.cpu.cc = uint8_t(f);
            cyc = r.one({uint8_t(0x20 + k), 0xfc});             // backwards
            if (r.cpu.pc != (want[k] ? 0x0ffe : 0x1002)) ++bad;
            r.cpu.cc = uint8_t(f);
            cyc = r.one({0x10, uint8_t(0x20 + k), 0x12, 0x34});
            if (r.cpu.pc != (want[k] ? 0x1004 + 0x1234 : 0x1004) || cyc != (want[k] ? 6 : 5)) {
                if (bad++ < 5) std::printf("FAIL lbranch %02x cc %x: pc %04x cyc %d\n", 0x20 + k, f, r.cpu.pc, cyc);
            }
        }
    }
    CHECK_EQ(bad, 0);
    // LBRA 5, BSR 7, LBSR 9, JSR, RTS.
    r.cpu.s = 0x8000;
    CHECK_EQ(r.one({0x16, 0x01, 0x00}), 5);
    CHECK_EQ(r.cpu.pc, 0x1103);
    CHECK_EQ(r.one({0x8d, 0x20}), 7);
    CHECK_EQ(r.cpu.pc, 0x1022);
    CHECK_EQ(r.cpu.s, 0x7ffe);
    CHECK_EQ(r.mem[0x7ffe], 0x10);
    CHECK_EQ(r.mem[0x7fff], 0x02);
    CHECK_EQ(r.one({0x17, 0xff, 0xfd}), 9);
    CHECK_EQ(r.cpu.pc, 0x1000);
    CHECK_EQ(r.cpu.s, 0x7ffc);
    CHECK_EQ(r.one({0x39}), 5);                         // RTS -> 1003
    CHECK_EQ(r.cpu.pc, 0x1003);
    CHECK_EQ(r.one({0xbd, 0x20, 0x00}), 8);             // JSR ext
    CHECK_EQ(r.cpu.pc, 0x2000);
    r.cpu.dp = 0x30;
    CHECK_EQ(r.one({0x9d, 0x40}), 7);                   // JSR direct
    CHECK_EQ(r.cpu.pc, 0x3040);
    r.cpu.x = 0x4000;
    CHECK_EQ(r.one({0xad, 0x84}), 7);                   // JSR ,X
    CHECK_EQ(r.cpu.pc, 0x4000);
    CHECK_EQ(r.one({0x7e, 0x55, 0x66}), 4);             // JMP ext
    CHECK_EQ(r.cpu.pc, 0x5566);
    CHECK_EQ(r.one({0x0e, 0x12}), 3);                   // JMP direct
    CHECK_EQ(r.cpu.pc, 0x3012);
    CHECK_EQ(r.one({0x6e, 0x84}), 3);                   // JMP ,X
    CHECK_EQ(r.cpu.pc, 0x4000);
}

// Each indexed mode: the address LDA reads, the register afterwards, the
// cycles (4 + the mode's extra), and the instruction length.
void test_indexed()
{
    struct Mode {
        std::vector<uint8_t> bytes;     // postbyte and offset bytes
        uint16_t ea;
        uint16_t reg_after;             // of the register used
        int      cycles;
        int      len;                   // including the opcode
    };
    // X = 0x2000, Y = 0x3000, U = 0x4000, S = 0x5000, A = 0xF0 (-16), B = 0x05, D = F005.
    const Mode modes[] = {
        {{0x84}, 0x2000, 0x2000, 4, 2},                 // ,X
        {{0xa4}, 0x3000, 0x3000, 4, 2},                 // ,Y
        {{0xc4}, 0x4000, 0x4000, 4, 2},                 // ,U
        {{0xe4}, 0x5000, 0x5000, 4, 2},                 // ,S
        {{0x05}, 0x2005, 0x2000, 5, 2},                 // 5,X
        {{0x1f}, 0x1fff, 0x2000, 5, 2},                 // -1,X
        {{0x10}, 0x1ff0, 0x2000, 5, 2},                 // -16,X
        {{0x80}, 0x2000, 0x2001, 6, 2},                 // ,X+
        {{0x81}, 0x2000, 0x2002, 7, 2},                 // ,X++
        {{0x82}, 0x1fff, 0x1fff, 6, 2},                 // ,-X
        {{0x83}, 0x1ffe, 0x1ffe, 7, 2},                 // ,--X
        {{0x85}, 0x2005, 0x2000, 5, 2},                 // B,X
        {{0x86}, 0x1ff0, 0x2000, 5, 2},                 // A,X
        {{0x88, 0x80}, 0x1f80, 0x2000, 5, 3},           // -128,X
        {{0x89, 0x01, 0x00}, 0x2100, 0x2000, 8, 4},     // 256,X
        {{0x8b}, 0x1005, 0x2000, 8, 2},                 // D,X (wraps)
        {{0x8c, 0x10}, 0x1013, 0x2000, 5, 3},           // 16,PCR (pc after = 1003)
        {{0x8d, 0x01, 0x00}, 0x1104, 0x2000, 9, 4},     // 256,PCR (pc after = 1004)
        // indirect: the pointer at the address is read
        {{0x94}, 0x6000, 0x2000, 7, 2},                 // [,X]
        {{0x91}, 0x6000, 0x2002, 10, 2},                // [,X++]
        {{0x93}, 0x6100, 0x1ffe, 10, 2},                // [,--X]
        {{0x98, 0x02}, 0x6200, 0x2000, 8, 3},           // [2,X]
        {{0x99, 0x00, 0x02}, 0x6200, 0x2000, 11, 4},    // [2,X] 16-bit
        {{0x95}, 0x6300, 0x2000, 8, 2},                 // [B,X]
        {{0x9b}, 0x6400, 0x2000, 11, 2},                // [D,X]
        {{0x9c, 0x10}, 0x6500, 0x2000, 8, 3},           // [16,PCR]
        {{0x9d, 0x00, 0x20}, 0x6600, 0x2000, 12, 4},    // [32,PCR] 16-bit
        {{0x9f, 0x20, 0x10}, 0x6700, 0x2000, 9, 4},     // [0x2010]
    };
    int i = 0;
    for (const Mode& m : modes) {
        Rig r;
        r.cpu.x = 0x2000;
        r.cpu.y = 0x3000;
        r.cpu.u = 0x4000;
        r.cpu.s = 0x5000;
        r.cpu.r.a = 0xf0;
        r.cpu.r.b = 0x05;
        auto ptr = [&](uint16_t at, uint16_t to) { r.mem[at] = uint8_t(to >> 8); r.mem[at + 1] = uint8_t(to); };
        ptr(0x2000, 0x6000);
        ptr(0x1ffe, 0x6100);
        ptr(0x2002, 0x6200);
        ptr(0x2005, 0x6300);
        ptr(0x1005, 0x6400);
        ptr(0x1013, 0x6500);
        ptr(0x1024, 0x6600);
        ptr(0x2010, 0x6700);
        r.mem[m.ea] = 0x5a;
        r.mem[0x1000] = 0xa6;                          // LDA indexed
        for (size_t k = 0; k < m.bytes.size(); ++k) r.mem[0x1001 + k] = m.bytes[k];
        r.cpu.pc = 0x1000;
        const int cyc = r.cpu.run(1);
        const uint16_t* reg = &r.cpu.x;
        switch (m.bytes[0] & 0x60) {
        case 0x20: reg = &r.cpu.y; break;
        case 0x40: reg = &r.cpu.u; break;
        case 0x60: reg = &r.cpu.s; break;
        default: break;
        }
        if (r.cpu.r.a != 0x5a || *reg != m.reg_after || cyc != m.cycles || r.cpu.pc != 0x1000 + m.len) {
            ++g_fail;
            std::printf("FAIL indexed mode %d (postbyte %02x): a %02x reg %04x cycles %d pc %04x\n", i,
                        m.bytes[0], r.cpu.r.a, *reg, cyc, r.cpu.pc);
        } else {
            ++g_pass;
        }
        ++i;
    }
    // LEAX / LEAY set Z; LEAS / LEAU don't touch the flags.
    Rig r;
    r.cpu.x = 0x0001;
    r.cpu.cc = 0;
    CHECK_EQ(r.one({0x30, 0x1f}), 5);                   // LEAX -1,X
    CHECK_EQ(r.cpu.x, 0);
    CHECK_EQ(r.cpu.cc, CC_Z);
    r.cpu.y = 0x8000;
    r.one({0x31, 0x21});                                // LEAY 1,Y
    CHECK_EQ(r.cpu.y, 0x8001);
    CHECK_EQ(r.cpu.cc, 0);
    r.cpu.cc = CC_Z | CC_N;
    r.cpu.u = 0x0001;
    r.one({0x33, 0x5f});                                // LEAU -1,U
    CHECK_EQ(r.cpu.u, 0);
    CHECK_EQ(r.cpu.cc, CC_Z | CC_N);
    r.cpu.s = 0x1000;
    CHECK_EQ(r.one({0x32, 0xe8, 0xf0}), 5);             // LEAS -16,S
    CHECK_EQ(r.cpu.s, 0x0ff0);
    // ABX: unsigned B
    r.cpu.x = 0x10f0;
    r.cpu.r.b = 0xf0;
    CHECK_EQ(r.one({0x3a}), 3);
    CHECK_EQ(r.cpu.x, 0x11e0);
}

// Cycle counts from the datasheet, one instruction of each kind.
void test_cycles()
{
    struct T { std::vector<uint8_t> b; int cycles; const char* what; };
    const T tests[] = {
        {{0x12}, 2, "NOP"},             {{0x19}, 2, "DAA"},             {{0x1a, 0x00}, 3, "ORCC"},
        {{0x1c, 0xff}, 3, "ANDCC"},     {{0x1d}, 2, "SEX"},             {{0x1e, 0x89}, 8, "EXG A,B"},
        {{0x1f, 0x12}, 6, "TFR X,Y"},   {{0x3a}, 3, "ABX"},             {{0x3d}, 11, "MUL"},
        {{0x86, 0x01}, 2, "LDA #"},     {{0x96, 0x01}, 4, "LDA <"},     {{0xb6, 0x20, 0x00}, 5, "LDA >"},
        {{0x97, 0x01}, 4, "STA <"},     {{0xb7, 0x20, 0x00}, 5, "STA >"}, {{0xa7, 0x84}, 4, "STA ,X"},
        {{0x8e, 0, 0}, 3, "LDX #"},     {{0x9e, 0x01}, 5, "LDX <"},     {{0xbe, 0x20, 0x00}, 6, "LDX >"},
        {{0x9f, 0x01}, 5, "STX <"},     {{0xbf, 0x20, 0x00}, 6, "STX >"}, {{0xae, 0x84}, 5, "LDX ,X"},
        {{0xcc, 0, 0}, 3, "LDD #"},     {{0xdc, 0x01}, 5, "LDD <"},     {{0xfc, 0x20, 0x00}, 6, "LDD >"},
        {{0xdd, 0x01}, 5, "STD <"},     {{0xfd, 0x20, 0x00}, 6, "STD >"}, {{0xed, 0x84}, 5, "STD ,X"},
        {{0xce, 0, 0}, 3, "LDU #"},     {{0xde, 0x01}, 5, "LDU <"},     {{0xff, 0x20, 0x00}, 6, "STU >"},
        {{0x93, 0x01}, 6, "SUBD <"},    {{0xb3, 0x20, 0x00}, 7, "SUBD >"}, {{0xa3, 0x84}, 6, "SUBD ,X"},
        {{0xd3, 0x01}, 6, "ADDD <"},    {{0xf3, 0x20, 0x00}, 7, "ADDD >"}, {{0x9c, 0x01}, 6, "CMPX <"},
        {{0xbc, 0x20, 0x00}, 7, "CMPX >"}, {{0xac, 0x84}, 6, "CMPX ,X"},
        {{0x00, 0x01}, 6, "NEG <"},     {{0x70, 0x20, 0x00}, 7, "NEG >"}, {{0x60, 0x84}, 6, "NEG ,X"},
        {{0x0d, 0x01}, 6, "TST <"},     {{0x7d, 0x20, 0x00}, 7, "TST >"}, {{0x0f, 0x01}, 6, "CLR <"},
        {{0x6f, 0x84}, 6, "CLR ,X"},    {{0x7f, 0x20, 0x00}, 7, "CLR >"},
        {{0x10, 0x8e, 0, 0}, 4, "LDY #"}, {{0x10, 0x9e, 0x01}, 6, "LDY <"}, {{0x10, 0xbe, 0x20, 0}, 7, "LDY >"},
        {{0x10, 0xae, 0x84}, 6, "LDY ,X"}, {{0x10, 0x9f, 0x01}, 6, "STY <"}, {{0x10, 0xbf, 0x20, 0}, 7, "STY >"},
        {{0x10, 0xce, 0x80, 0}, 4, "LDS #"}, {{0x10, 0xdf, 0x01}, 6, "STS <"}, {{0x10, 0xfe, 0x20, 0}, 7, "LDS >"},
        {{0x10, 0x93, 0x01}, 7, "CMPD <"}, {{0x10, 0xb3, 0x20, 0}, 8, "CMPD >"}, {{0x10, 0xa3, 0x84}, 7, "CMPD ,X"},
        {{0x11, 0x9c, 0x01}, 7, "CMPS <"}, {{0x11, 0xbc, 0x20, 0}, 8, "CMPS >"}, {{0x11, 0xac, 0x84}, 7, "CMPU ,X"},
        {{0x34, 0xff}, 17, "PSHS all"}, {{0x34, 0x06}, 7, "PSHS D"},     {{0x36, 0x10}, 7, "PSHU X"},
        {{0x34, 0x00}, 5, "PSHS none"},
    };
    for (const T& t : tests) {
        Rig r;
        r.cpu.x = 0x2000;
        for (size_t k = 0; k < t.b.size(); ++k) r.mem[0x1000 + k] = t.b[k];
        r.cpu.pc = 0x1000;
        const int cyc = r.cpu.run(1);
        if (cyc == t.cycles) ++g_pass;
        else { ++g_fail; std::printf("FAIL cycles %s: %d, want %d\n", t.what, cyc, t.cycles); }
    }
    // PULS all: 5 + 12
    Rig r;
    CHECK_EQ(r.one({0x35, 0x7f}), 15);
    CHECK_EQ(r.one({0x37, 0x06}), 7);
}

void test_transfers()
{
    Rig r;
    r.cpu.r.a = 0x12;
    r.cpu.r.b = 0x34;
    r.cpu.x = 0xabcd;
    r.one({0x1e, 0x89});                // EXG A,B
    CHECK_EQ(r.cpu.r.a, 0x34);
    CHECK_EQ(r.cpu.r.b, 0x12);
    r.one({0x1f, 0x81});                // TFR A,X: FF in the high byte
    CHECK_EQ(r.cpu.x, 0xff34);
    r.cpu.x = 0xabcd;
    r.one({0x1f, 0x19});                // TFR X,B: low byte
    CHECK_EQ(r.cpu.r.b, 0xcd);
    r.cpu.d = 0x1122;
    r.cpu.y = 0x3344;
    r.one({0x1e, 0x02});                // EXG D,Y
    CHECK_EQ(r.cpu.d, 0x3344);
    CHECK_EQ(r.cpu.y, 0x1122);
    r.cpu.cc = 0x5a;
    r.one({0x1f, 0xa1});                // TFR CC,X: CC twice
    CHECK_EQ(r.cpu.x, 0x5a5a);
    r.cpu.dp = 0x77;
    r.cpu.x = 0x1234;
    r.one({0x1e, 0x1b});                // EXG X,DP: DP <- 34, X <- FF77
    CHECK_EQ(r.cpu.dp, 0x34);
    CHECK_EQ(r.cpu.x, 0xff77);
    r.one({0x1f, 0x61});                // TFR (no register 6),X: FFFF
    CHECK_EQ(r.cpu.x, 0xffff);
    r.cpu.x = 0x2000;
    r.one({0x1f, 0x15});                // TFR X,PC
    CHECK_EQ(r.cpu.pc, 0x2000);
    // SEX
    r.cpu.r.b = 0x80;
    r.cpu.cc = CC_V;
    r.one({0x1d});
    CHECK_EQ(r.cpu.d, 0xff80);
    CHECK_EQ(r.cpu.cc, CC_N | CC_V);    // V untouched
    r.cpu.r.b = 0x00;
    r.cpu.r.a = 0x55;
    r.cpu.cc = 0;
    r.one({0x1d});
    CHECK_EQ(r.cpu.d, 0x0000);
    CHECK_EQ(r.cpu.cc, CC_Z);
    // Loads and stores: N Z, V cleared, C kept.
    r.cpu.cc = CC_V | CC_C;
    r.one({0xcc, 0x80, 0x00});          // LDD #8000
    CHECK_EQ(r.cpu.cc, CC_N | CC_C);
    r.cpu.dp = 0x20;
    r.cpu.cc = CC_V;
    r.one({0xdd, 0x10});                // STD <10
    CHECK_EQ(r.mem[0x2010], 0x80);
    CHECK_EQ(r.mem[0x2011], 0x00);
    CHECK_EQ(r.cpu.cc, CC_N);
    r.cpu.r.a = 0;
    r.one({0x97, 0x12});                // STA <12
    CHECK_EQ(r.cpu.cc, CC_Z);
}

void test_stack()
{
    Rig r;
    r.cpu.s = 0x8000;
    r.cpu.u = 0x7000;
    r.cpu.cc = 0x0f;
    r.cpu.r.a = 0x11;
    r.cpu.r.b = 0x22;
    r.cpu.dp = 0x33;
    r.cpu.x = 0x4455;
    r.cpu.y = 0x6677;
    r.one({0x34, 0xff});                // PSHS PC,U,Y,X,DP,B,A,CC
    CHECK_EQ(r.cpu.s, 0x8000 - 12);
    const uint8_t want[12] = {0x0f, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x70, 0x00, 0x10, 0x02};
    bool same = true;
    for (int i = 0; i < 12; ++i) same = same && r.mem[0x8000 - 12 + i] == want[i];
    CHECK(same);
    r.cpu.cc = 0;
    r.cpu.d = 0;
    r.cpu.dp = 0;
    r.cpu.x = r.cpu.y = r.cpu.u = 0;
    r.one({0x35, 0x7f});                // PULS CC,A,B,DP,X,Y,U
    CHECK_EQ(r.cpu.cc, 0x0f);
    CHECK_EQ(r.cpu.d, 0x1122);
    CHECK_EQ(r.cpu.dp, 0x33);
    CHECK_EQ(r.cpu.x, 0x4455);
    CHECK_EQ(r.cpu.y, 0x6677);
    CHECK_EQ(r.cpu.u, 0x7000);
    CHECK_EQ(r.cpu.s, 0x8000 - 2);
    r.one({0x35, 0x80});                // PULS PC
    CHECK_EQ(r.cpu.pc, 0x1002);
    // PSHU S / PULU
    r.cpu.s = 0x1234;
    r.cpu.u = 0x7000;
    r.one({0x36, 0x40});
    CHECK_EQ(r.cpu.u, 0x6ffe);
    CHECK_EQ(r.mem[0x6ffe], 0x12);
    CHECK_EQ(r.mem[0x6fff], 0x34);
    r.cpu.s = 0;
    r.one({0x37, 0x40});
    CHECK_EQ(r.cpu.s, 0x1234);
    CHECK_EQ(r.cpu.u, 0x7000);
}

// --- interrupts ----------------------------------------------------------------------

struct IntRig : Rig {
    IntRig()
    {
        set_vec(kVecIrq, 0x3000);
        set_vec(kVecFirq, 0x3100);
        set_vec(kVecNmi, 0x3200);
        set_vec(kVecSwi, 0x3300);
        set_vec(kVecSwi2, 0x3400);
        set_vec(kVecSwi3, 0x3500);
        mem[0x3000] = 0x3b;             // RTI
        mem[0x3100] = 0x3b;
        mem[0x3200] = 0x3b;
        mem[0x3300] = 0x3b;
        mem[0x3400] = 0x3b;
        mem[0x3500] = 0x3b;
        for (int i = 0; i < 0x40; ++i) mem[0x1000 + i] = 0x12;     // NOPs
        cpu.pc = 0x1000;
        cpu.s = 0x8000;
    }
};

void test_irq_firq()
{
    IntRig r;
    r.cpu.cc = 0x05;                    // I and F clear, Z C set
    r.cpu.d = 0xaabb;
    r.cpu.irq(true);
    int cyc = r.cpu.run(1);
    CHECK_EQ(cyc, 19);
    CHECK_EQ(r.cpu.pc, 0x3000);
    CHECK_EQ(r.cpu.s, 0x8000 - 12);
    CHECK_EQ(r.mem[0x8000 - 12], 0x85);         // CC with E
    CHECK_EQ(r.cpu.cc, 0x95);                   // E I, F still clear
    r.cpu.irq(false);
    r.cpu.d = 0;
    cyc = r.cpu.run(1);                         // RTI: entire state
    CHECK_EQ(cyc, 15);
    CHECK_EQ(r.cpu.pc, 0x1000);
    CHECK_EQ(r.cpu.cc, 0x85);
    CHECK_EQ(r.cpu.d, 0xaabb);
    CHECK_EQ(r.cpu.s, 0x8000);
    // Masked: not taken.
    r.cpu.cc = CC_I | CC_F;
    r.cpu.irq(true);
    r.cpu.firq(true);
    CHECK_EQ(r.cpu.run(1), 2);
    CHECK_EQ(r.cpu.pc, 0x1001);
    // FIRQ before IRQ; only PC and CC.
    r.cpu.cc = 0x01;
    cyc = r.cpu.run(1);
    CHECK_EQ(cyc, 10);
    CHECK_EQ(r.cpu.pc, 0x3100);
    CHECK_EQ(r.cpu.s, 0x8000 - 3);
    CHECK_EQ(r.mem[0x8000 - 3], 0x01);          // E clear
    CHECK_EQ(r.cpu.cc, 0x51);
    r.cpu.firq(false);
    r.cpu.irq(false);
    cyc = r.cpu.run(1);
    CHECK_EQ(cyc, 6);
    CHECK_EQ(r.cpu.pc, 0x1001);
    CHECK_EQ(r.cpu.s, 0x8000);
    CHECK_EQ(r.cpu.cc, 0x01);
    // IRQ while FIRQ is masked only.
    r.cpu.cc = CC_F;
    r.cpu.firq(true);
    r.cpu.irq(true);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x3000);
    r.cpu.firq(false);
    r.cpu.irq(false);
}

void test_nmi_swi()
{
    IntRig r;
    r.cpu.reset();
    r.cpu.pc = 0x1000;
    r.cpu.s = 0x8000;                   // set directly: not a load, NMI not armed
    r.cpu.nmi();
    CHECK_EQ(r.cpu.run(1), 2);          // ignored
    CHECK_EQ(r.cpu.pc, 0x1001);
    r.put(0x1001, {0x10, 0xce, 0x80, 0x00});    // LDS #8000 arms it
    r.cpu.run(1);
    r.cpu.nmi();
    CHECK_EQ(r.cpu.run(1), 19);
    CHECK_EQ(r.cpu.pc, 0x3200);
    CHECK_EQ(r.cpu.cc & (CC_I | CC_F | CC_E), CC_I | CC_F | CC_E);
    r.cpu.run(1);                       // RTI
    CHECK_EQ(r.cpu.pc, 0x1005);
    CHECK_EQ(r.cpu.run(1), 2);          // an edge: taken once
    // SWI sets I and F; SWI2 / SWI3 don't.
    r.cpu.cc = 0;
    r.put(0x1006, {0x3f});
    CHECK_EQ(r.cpu.run(1), 19);
    CHECK_EQ(r.cpu.pc, 0x3300);
    CHECK_EQ(r.cpu.cc, CC_E | CC_I | CC_F);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x1007);
    CHECK_EQ(r.cpu.cc, CC_E);           // E as stacked
    r.cpu.cc = 0;
    r.put(0x1007, {0x10, 0x3f, 0x11, 0x3f});
    CHECK_EQ(r.cpu.run(1), 20);
    CHECK_EQ(r.cpu.pc, 0x3400);
    CHECK_EQ(r.cpu.cc, CC_E);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.run(1), 20);
    CHECK_EQ(r.cpu.pc, 0x3500);
    CHECK_EQ(r.cpu.cc, CC_E);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x100b);
}

void test_cwai_sync()
{
    IntRig r;
    r.cpu.cc = CC_I | CC_F;
    r.put(0x1000, {0x3c, 0xef});        // CWAI #EF: clear I, push, wait
    CHECK_EQ(r.cpu.run(1), 16);
    CHECK(r.cpu.cwai);
    CHECK_EQ(r.cpu.s, 0x8000 - 12);
    CHECK_EQ(r.cpu.run(100), 100);      // still waiting
    CHECK_EQ(r.cpu.pc, 0x1002);
    r.cpu.firq(true);                   // masked (F set): still waiting
    CHECK_EQ(r.cpu.run(50), 50);
    CHECK(r.cpu.cwai);
    r.cpu.firq(false);
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 4);          // straight to the vector
    CHECK_EQ(r.cpu.pc, 0x3000);
    CHECK_EQ(r.cpu.s, 0x8000 - 12);
    r.cpu.irq(false);
    CHECK_EQ(r.cpu.run(1), 15);         // RTI: everything back
    CHECK_EQ(r.cpu.pc, 0x1002);
    CHECK_EQ(r.cpu.s, 0x8000);
    // CWAI then FIRQ: the entire state is on the stack, so RTI pulls it all.
    r.cpu.cc = CC_I | CC_F;
    r.put(0x1002, {0x3c, 0xbf});        // clear F
    r.cpu.run(1);
    r.cpu.firq(true);
    CHECK_EQ(r.cpu.run(1), 4);
    CHECK_EQ(r.cpu.pc, 0x3100);
    CHECK_EQ(r.cpu.cc & (CC_I | CC_F), CC_I | CC_F);
    r.cpu.firq(false);
    CHECK_EQ(r.cpu.run(1), 15);
    CHECK_EQ(r.cpu.pc, 0x1004);
    // SYNC: a masked interrupt line just ends the wait.
    r.cpu.cc = CC_I | CC_F;
    r.put(0x1004, {0x13, 0x12});
    r.cpu.run(1);
    CHECK(r.cpu.sync);
    CHECK_EQ(r.cpu.run(30), 30);
    r.cpu.irq(true);
    r.cpu.run(1);
    CHECK(!r.cpu.sync);
    CHECK_EQ(r.cpu.pc, 0x1006);         // ran the NOP after SYNC
    r.cpu.irq(false);
    // An enabled one is taken.
    r.cpu.cc = 0;
    r.put(0x1006, {0x13});
    r.cpu.run(1);
    r.cpu.irq(true);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x3000);
    r.cpu.irq(false);
}

// --- programs --------------------------------------------------------------------------

// Sum 1..100 in a loop; copy a block with auto-increment; a subroutine.
void test_programs()
{
    Rig r;
    r.put(0x1000, {
        0x4f, 0x5f,                     // CLRA ; CLRB         D = 0
        0x8e, 0x00, 0x64,               // LDX #100
        0x34, 0x10,                     // loop: PSHS X
        0xe3, 0xe1,                     //       ADDD ,S++
        0x30, 0x1f,                     //       LEAX -1,X
        0x26, 0xf8,                     //       BNE loop
        0xfd, 0x20, 0x00,               // STD $2000
        0x8e, 0x30, 0x00,               // LDX #$3000
        0x10, 0x8e, 0x40, 0x00,         // LDY #$4000
        0xc6, 0x10,                     // LDB #16
        0xa6, 0x80,                     // copy: LDA ,X+
        0xa7, 0xa0,                     //       STA ,Y+
        0x5a,                           //       DECB
        0x26, 0xf9,                     //       BNE copy
        0xbd, 0x10, 0x40,               // JSR $1040
        0x20, 0xfe,                     // done: BRA done
    });
    r.put(0x1040, {0x86, 0x99, 0xb7, 0x20, 0x02, 0x39});   // LDA #$99 ; STA $2002 ; RTS
    for (int i = 0; i < 16; ++i) r.mem[0x3000 + i] = uint8_t(i * 7 + 1);
    r.cpu.pc = 0x1000;
    r.cpu.s = 0x8000;
    r.cpu.run(20000);
    CHECK_EQ((r.mem[0x2000] << 8) | r.mem[0x2001], 5050);
    CHECK_EQ(std::memcmp(r.mem + 0x3000, r.mem + 0x4000, 16), 0);
    CHECK_EQ(r.mem[0x2002], 0x99);
    CHECK_EQ(r.cpu.s, 0x8000);
    CHECK_EQ(r.cpu.pc, 0x1023);
}

// Memory-mapped I/O through the callbacks, and add_cycles from a callback.
struct IoRig {
    uint8_t mem[0x10000] = {};
    Cpu cpu;
    int writes = 0;
    uint8_t last = 0;
    static uint8_t rd(void* c, uint16_t a) { return a == 0xc000 ? 0x42 : static_cast<IoRig*>(c)->mem[a]; }
    static void wr(void* c, uint16_t a, uint8_t v)
    {
        IoRig& r = *static_cast<IoRig*>(c);
        if (a == 0xc001) {
            ++r.writes;
            r.last = v;
            r.cpu.add_cycles(100);      // e.g. a blitter holding the bus
        } else {
            r.mem[a] = v;
        }
    }
};

void test_io()
{
    IoRig r;
    r.cpu.ctx = &r;
    r.cpu.mem_read = IoRig::rd;
    r.cpu.mem_write = IoRig::wr;
    r.cpu.map_ram(0x0000, 0xc000, r.mem);
    r.mem[0x1000] = 0xb6; r.mem[0x1001] = 0xc0; r.mem[0x1002] = 0x00;     // LDA $C000
    r.mem[0x1003] = 0xb7; r.mem[0x1004] = 0xc0; r.mem[0x1005] = 0x01;     // STA $C001
    r.cpu.pc = 0x1000;
    CHECK_EQ(r.cpu.run(1), 5);
    CHECK_EQ(r.cpu.r.a, 0x42);
    CHECK_EQ(r.cpu.run(1), 105);
    CHECK_EQ(r.writes, 1);
    CHECK_EQ(r.last, 0x42);
}

void test_halt_reset()
{
    Rig r;
    r.one({0x14});
    CHECK(r.cpu.halted);
    CHECK_EQ(r.cpu.run(40), 40);
    r.cpu.reset();
    CHECK(!r.cpu.halted);
    CHECK_EQ(r.cpu.pc, 0x1000);
    CHECK_EQ(r.cpu.cc, CC_I | CC_F);
    // Unknown page 2 code: runs as page 1 with a cycle more.
    r.cpu.r.a = 0;
    CHECK_EQ(r.one({0x10, 0x86, 0x77}), 3);
    CHECK_EQ(r.cpu.r.a, 0x77);
}

} // namespace

int main()
{
    test_alu8();
    test_unary();
    test_alu16();
    test_daa_mul();
    test_branches();
    test_indexed();
    test_cycles();
    test_transfers();
    test_stack();
    test_irq_firq();
    test_nmi_swi();
    test_cwai_sync();
    test_programs();
    test_io();
    test_halt_reset();
    std::printf("6809 tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
