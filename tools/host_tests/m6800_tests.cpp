// Unit tests for the 6800 / 6802 / 6808 core (emu/cpu/m6800.*), with no
// test program from outside: every 8-bit ALU result and flag against a
// plain model for all operands, the read-modify-write group (shifts set
// V = N xor C), CPX, DAA by decimal sums, every branch, the datasheet's
// cycle counts, the stack, IRQ / NMI / SWI / WAI / RTI, the undocumented
// opcodes as no-ops, and a small program.
//
//   m6800_tests
#include "../../emu/cpu/m6800.h"

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

using namespace m6800;

struct Rig {
    uint8_t mem[0x10000] = {};
    Cpu cpu;
    Rig()
    {
        cpu.map_ram(0x0000, 0x10000, mem);
        set_vec(kVecReset, 0x1000);
        cpu.reset();
        cpu.s = 0x01ff;
    }
    void set_vec(uint16_t v, uint16_t to) { mem[v] = uint8_t(to >> 8); mem[v + 1] = uint8_t(to); }
    void put(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) mem[at++] = b;
    }
    int one(std::initializer_list<uint8_t> bytes)
    {
        put(0x1000, bytes);
        cpu.pc = 0x1000;
        return cpu.run(1);
    }
};

void test_alu()
{
    Rig r;
    int bad = 0;
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            for (int c = 0; c < 2; ++c) {
                const uint8_t cc0 = uint8_t(0xc0 | (c ? CC_C : 0));
                struct { uint8_t op; bool add, carry, store; } ops[] = {
                    {0x8b, true, false, true}, {0x89, true, true, true}, {0x80, false, false, true},
                    {0x82, false, true, true}, {0x81, false, false, false},
                };
                for (const auto& o : ops) {
                    r.cpu.a = uint8_t(a);
                    r.cpu.cc = cc0;
                    const int cyc = r.one({o.op, uint8_t(b)});
                    const int cin = o.carry ? c : 0;
                    const int u = o.add ? a + b + cin : a - b - cin;
                    const int sres = o.add ? int8_t(a) + int8_t(b) + cin : int8_t(a) - int8_t(b) - cin;
                    uint8_t wf = uint8_t(cc0 & ~(CC_N | CC_Z | CC_V | CC_C | (o.add ? CC_H : 0)));
                    if (o.add && ((a & 15) + (b & 15) + cin) > 15) wf |= CC_H;
                    if (u & 0x80) wf |= CC_N;
                    if ((u & 0xff) == 0) wf |= CC_Z;
                    if (sres < -128 || sres > 127) wf |= CC_V;
                    if (o.add ? u > 255 : u < 0) wf |= CC_C;
                    if (r.cpu.a != (o.store ? (u & 0xff) : a) || r.cpu.cc != wf || cyc != 2) {
                        if (bad++ < 10)
                            std::printf("FAIL alu %02x %02x %02x %d: a %02x cc %02x want cc %02x\n", o.op, a, b, c,
                                        r.cpu.a, r.cpu.cc, wf);
                    }
                }
                // ABA / SBA / CBA use B.
                r.cpu.a = uint8_t(a);
                r.cpu.b = uint8_t(b);
                r.cpu.cc = cc0;
                r.one({0x1b});
                if (r.cpu.a != ((a + b) & 0xff)) ++bad;
                r.cpu.a = uint8_t(a);
                r.cpu.cc = cc0;
                r.one({0x10});
                if (r.cpu.a != ((a - b) & 0xff) || bool(r.cpu.cc & CC_C) != (a < b)) ++bad;
            }
    CHECK_EQ(bad, 0);
}

void test_unary()
{
    Rig r;
    int bad = 0;
    for (int v = 0; v < 256; ++v)
        for (int c = 0; c < 2; ++c) {
            const uint8_t cc0 = uint8_t(0xc0 | CC_H | (c ? CC_C : 0));
            auto nz = [](int x) { return uint8_t(((x & 0x80) ? CC_N : 0) | ((x & 0xff) ? 0 : CC_Z)); };
            // shifts: V = N ^ C (of the result)
            auto sh = [&](int res, int carry) {
                uint8_t f = uint8_t(nz(res) | (carry ? CC_C : 0));
                if (bool(f & CC_N) != bool(f & CC_C)) f |= CC_V;
                return f;
            };
            const uint8_t nzvc = CC_N | CC_Z | CC_V | CC_C;
            struct Case { uint8_t op; int res; uint8_t flags; uint8_t mask; };
            const int neg = (-v) & 0xff, com = (~v) & 0xff;
            Case cases[] = {
                {0x40, neg, uint8_t(nz(neg) | (v == 0x80 ? CC_V : 0) | (v ? CC_C : 0)), nzvc},
                {0x43, com, uint8_t(nz(com) | CC_C), nzvc},
                {0x44, v >> 1, sh(v >> 1, v & 1), nzvc},
                {0x46, (v >> 1) | (c << 7), sh((v >> 1) | (c << 7), v & 1), nzvc},
                {0x47, (v >> 1) | (v & 0x80), sh((v >> 1) | (v & 0x80), v & 1), nzvc},
                {0x48, (v << 1) & 0xff, sh((v << 1) & 0xff, v & 0x80), nzvc},
                {0x49, ((v << 1) | c) & 0xff, sh(((v << 1) | c) & 0xff, v & 0x80), nzvc},
                {0x4a, (v - 1) & 0xff, uint8_t(nz(v - 1) | (v == 0x80 ? CC_V : 0)), CC_N | CC_Z | CC_V},
                {0x4c, (v + 1) & 0xff, uint8_t(nz(v + 1) | (v == 0x7f ? CC_V : 0)), CC_N | CC_Z | CC_V},
                {0x4d, v, nz(v), nzvc},
                {0x4f, 0, CC_Z, nzvc},
            };
            for (const Case& k : cases) {
                r.cpu.a = uint8_t(v);
                r.cpu.cc = cc0;
                const int cyc = r.one({k.op});
                const uint8_t want = uint8_t((cc0 & ~k.mask) | k.flags);
                if (r.cpu.a != k.res || r.cpu.cc != want || cyc != 2) {
                    if (bad++ < 10)
                        std::printf("FAIL unary %02x v %02x c %d: a %02x cc %02x, want %02x cc %02x\n", k.op, v, c,
                                    r.cpu.a, r.cpu.cc, k.res, want);
                }
            }
            // ROR on memory, indexed (7) and extended (6).
            r.cpu.x = 0x0200;
            r.mem[0x0205] = uint8_t(v);
            r.cpu.cc = cc0;
            if (r.one({0x66, 0x05}) != 7 || r.mem[0x0205] != ((v >> 1) | (c << 7))) ++bad;
            r.mem[0x0300] = uint8_t(v);
            r.cpu.cc = cc0;
            if (r.one({0x79, 0x03, 0x00}) != 6 || r.mem[0x0300] != (((v << 1) | c) & 0xff)) ++bad;
        }
    CHECK_EQ(bad, 0);
}

void test_cpx_daa()
{
    Rig r;
    // CPX: Z from 16 bits, N / V from the high bytes, C untouched.
    r.cpu.x = 0x1234;
    r.cpu.cc = 0xc0 | CC_C;
    CHECK_EQ(r.one({0x8c, 0x12, 0x34}), 3);
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_C | CC_Z);
    r.cpu.x = 0x1234;
    r.one({0x8c, 0x12, 0x35});              // high bytes equal: N clear, not zero
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_C);
    r.cpu.x = 0x8000;
    r.cpu.cc = 0xc0;
    r.one({0x8c, 0x01, 0x00});              // 80 - 01 = 7F: V
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_V);
    r.cpu.x = 0x0000;
    r.one({0x8c, 0x01, 0x00});              // 00 - 01 = FF: N
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_N);
    int bad = 0;
    for (int x = 0; x < 100; ++x)
        for (int y = 0; y < 100; ++y) {
            r.cpu.a = uint8_t((x / 10) << 4 | (x % 10));
            r.cpu.cc = 0xc0;
            r.put(0x1000, {0x8b, uint8_t((y / 10) << 4 | (y % 10)), 0x19});
            r.cpu.pc = 0x1000;
            r.cpu.run(1);
            r.cpu.run(1);
            const int s = x + y;
            if (r.cpu.a != (((s % 100) / 10) << 4 | (s % 10)) || bool(r.cpu.cc & CC_C) != (s >= 100)) ++bad;
        }
    CHECK_EQ(bad, 0);
}

void test_branches()
{
    Rig r;
    int bad = 0;
    for (int f = 0; f < 16; ++f) {
        const bool c = f & 1, v = f & 2, z = f & 4, n = f & 8;
        const bool want[16] = {true, false, !(c || z), c || z, !c, c, !z, z, !v, v, !n, n,
                               n == v, n != v, !z && n == v, z || n != v};
        for (int k = 0; k < 16; ++k) {
            r.cpu.cc = uint8_t(0xc0 | f);
            const int cyc = r.one({uint8_t(0x20 + k), 0x10});
            if (r.cpu.pc != (want[k] ? 0x1012 : 0x1002) || cyc != 4) ++bad;
        }
    }
    CHECK_EQ(bad, 0);
    r.cpu.s = 0x01ff;
    CHECK_EQ(r.one({0x8d, 0x10}), 8);       // BSR
    CHECK_EQ(r.cpu.pc, 0x1012);
    CHECK_EQ(r.cpu.s, 0x01fd);
    CHECK_EQ(r.mem[0x01fe], 0x10);
    CHECK_EQ(r.mem[0x01ff], 0x02);
    CHECK_EQ(r.one({0x39}), 5);             // RTS
    CHECK_EQ(r.cpu.pc, 0x1002);
    CHECK_EQ(r.one({0xbd, 0x20, 0x00}), 9); // JSR ext
    CHECK_EQ(r.cpu.pc, 0x2000);
    r.cpu.x = 0x3000;
    CHECK_EQ(r.one({0xad, 0x04}), 8);       // JSR ,X
    CHECK_EQ(r.cpu.pc, 0x3004);
    CHECK_EQ(r.one({0x6e, 0x08}), 4);       // JMP ,X
    CHECK_EQ(r.cpu.pc, 0x3008);
    CHECK_EQ(r.one({0x7e, 0x45, 0x67}), 3);
    CHECK_EQ(r.cpu.pc, 0x4567);
}

void test_cycles_regs()
{
    struct T { std::vector<uint8_t> b; int cycles; const char* what; };
    const T tests[] = {
        {{0x01}, 2, "NOP"}, {{0x08}, 4, "INX"}, {{0x09}, 4, "DEX"}, {{0x30}, 4, "TSX"}, {{0x35}, 4, "TXS"},
        {{0x36}, 4, "PSHA"}, {{0x32}, 4, "PULA"}, {{0x31}, 4, "INS"},
        {{0x86, 1}, 2, "LDAA #"}, {{0x96, 1}, 3, "LDAA <"}, {{0xa6, 1}, 5, "LDAA ,X"}, {{0xb6, 0, 1}, 4, "LDAA >"},
        {{0x97, 1}, 4, "STAA <"}, {{0xa7, 1}, 6, "STAA ,X"}, {{0xb7, 0, 1}, 5, "STAA >"},
        {{0x8c, 0, 0}, 3, "CPX #"}, {{0x9c, 1}, 4, "CPX <"}, {{0xac, 1}, 6, "CPX ,X"}, {{0xbc, 0, 1}, 5, "CPX >"},
        {{0x8e, 1, 0}, 3, "LDS #"}, {{0x9e, 1}, 4, "LDS <"}, {{0xae, 1}, 6, "LDS ,X"}, {{0xbe, 0, 1}, 5, "LDS >"},
        {{0x9f, 1}, 5, "STS <"}, {{0xaf, 1}, 7, "STS ,X"}, {{0xbf, 0, 1}, 6, "STS >"},
        {{0xce, 0, 0}, 3, "LDX #"}, {{0xde, 1}, 4, "LDX <"}, {{0xee, 1}, 6, "LDX ,X"}, {{0xfe, 0, 1}, 5, "LDX >"},
        {{0xdf, 1}, 5, "STX <"}, {{0xef, 1}, 7, "STX ,X"}, {{0xff, 0, 1}, 6, "STX >"},
        {{0x60, 1}, 7, "NEG ,X"}, {{0x70, 0, 1}, 6, "NEG >"}, {{0x6d, 1}, 7, "TST ,X"}, {{0x7f, 0, 1}, 6, "CLR >"},
        {{0x87, 1}, 2, "illegal 87"}, {{0x02}, 2, "illegal 02"},
    };
    for (const T& t : tests) {
        Rig r;
        r.cpu.x = 0x0300;
        for (size_t k = 0; k < t.b.size(); ++k) r.mem[0x1000 + k] = t.b[k];
        r.cpu.pc = 0x1000;
        const int cyc = r.cpu.run(1);
        if (cyc == t.cycles && r.cpu.pc >= 0x1000 + t.b.size()) ++g_pass;
        else { ++g_fail; std::printf("FAIL cycles %s: %d, want %d (pc %04x)\n", t.what, cyc, t.cycles, r.cpu.pc); }
    }
    Rig r;
    // Undocumented opcodes skip their operands.
    CHECK_EQ(r.one({0x8f, 0x12, 0x34}), 2);
    CHECK_EQ(r.cpu.pc, 0x1003);
    // TAP / TPA keep bits 6 and 7 set.
    r.cpu.a = 0x0f;
    r.one({0x06});
    CHECK_EQ(r.cpu.cc, 0xcf);
    r.cpu.cc = 0xc5;
    r.one({0x07});
    CHECK_EQ(r.cpu.a, 0xc5);
    // TAB / TBA: N Z, V cleared.
    r.cpu.a = 0x80;
    r.cpu.cc = 0xc0 | CC_V | CC_C;
    r.one({0x16});
    CHECK_EQ(r.cpu.b, 0x80);
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_N | CC_C);
    // INX / DEX: only Z.
    r.cpu.x = 0xffff;
    r.cpu.cc = 0xc0 | CC_N;
    r.one({0x08});
    CHECK_EQ(r.cpu.x, 0);
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_N | CC_Z);
    // TSX / TXS: X = S + 1, S = X - 1.
    r.cpu.s = 0x01f0;
    r.one({0x30});
    CHECK_EQ(r.cpu.x, 0x01f1);
    r.cpu.x = 0x0100;
    r.one({0x35});
    CHECK_EQ(r.cpu.s, 0x00ff);
    // PSHA / PULB: the stack pointer points at the next free byte.
    r.cpu.s = 0x01ff;
    r.cpu.a = 0x5a;
    r.one({0x36});
    CHECK_EQ(r.mem[0x01ff], 0x5a);
    CHECK_EQ(r.cpu.s, 0x01fe);
    r.one({0x33});
    CHECK_EQ(r.cpu.b, 0x5a);
    CHECK_EQ(r.cpu.s, 0x01ff);
    // STX / LDS flags.
    r.cpu.x = 0x8001;
    r.cpu.cc = 0xc0 | CC_V;
    r.one({0xdf, 0x40});
    CHECK_EQ(r.mem[0x40], 0x80);
    CHECK_EQ(r.mem[0x41], 0x01);
    CHECK_EQ(r.cpu.cc, 0xc0 | CC_N);
}

void test_interrupts()
{
    Rig r;
    r.set_vec(kVecIrq, 0x3000);
    r.set_vec(kVecNmi, 0x3100);
    r.set_vec(kVecSwi, 0x3200);
    r.mem[0x3000] = 0x3b;
    r.mem[0x3100] = 0x3b;
    r.mem[0x3200] = 0x3b;
    for (int i = 0; i < 0x20; ++i) r.mem[0x1000 + i] = 0x01;
    r.cpu.pc = 0x1000;
    r.cpu.s = 0x01ff;
    r.cpu.a = 0x11;
    r.cpu.b = 0x22;
    r.cpu.x = 0x3344;
    r.cpu.cc = 0xc0 | CC_C;                 // I clear
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 12);
    CHECK_EQ(r.cpu.pc, 0x3000);
    CHECK_EQ(r.cpu.s, 0x01ff - 7);
    const uint8_t want[7] = {0xc1, 0x22, 0x11, 0x33, 0x44, 0x10, 0x00};     // CC B A XH XL PCH PCL
    bool same = true;
    for (int i = 0; i < 7; ++i) same = same && r.mem[0x01f9 + i] == want[i];
    CHECK(same);
    CHECK(r.cpu.cc & CC_I);
    r.cpu.a = r.cpu.b = 0;
    r.cpu.x = 0;
    r.cpu.irq(false);
    CHECK_EQ(r.cpu.run(1), 10);             // RTI
    CHECK_EQ(r.cpu.pc, 0x1000);
    CHECK_EQ(r.cpu.a, 0x11);
    CHECK_EQ(r.cpu.b, 0x22);
    CHECK_EQ(r.cpu.x, 0x3344);
    CHECK_EQ(r.cpu.cc, 0xc1);
    CHECK_EQ(r.cpu.s, 0x01ff);
    // Masked IRQ waits; NMI doesn't.
    r.cpu.cc = 0xc0 | CC_I;
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 2);
    r.cpu.nmi();
    CHECK_EQ(r.cpu.run(1), 12);
    CHECK_EQ(r.cpu.pc, 0x3100);
    r.cpu.run(1);                           // RTI
    CHECK_EQ(r.cpu.pc, 0x1001);
    r.cpu.irq(false);
    // SWI
    r.mem[0x1001] = 0x3f;
    CHECK_EQ(r.cpu.run(1), 12);
    CHECK_EQ(r.cpu.pc, 0x3200);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x1002);
    // WAI: state stacked at once; the interrupt then costs 4 cycles.
    r.cpu.cc = 0xc0;
    r.mem[0x1002] = 0x3e;
    CHECK_EQ(r.cpu.run(1), 9);
    CHECK(r.cpu.wai);
    CHECK_EQ(r.cpu.s, 0x01ff - 7);
    CHECK_EQ(r.cpu.run(50), 50);
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 4);
    CHECK_EQ(r.cpu.pc, 0x3000);
    CHECK_EQ(r.cpu.s, 0x01ff - 7);
    r.cpu.irq(false);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x1003);
    CHECK_EQ(r.cpu.s, 0x01ff);
    // CLI with I set: a pending IRQ waits for one more instruction (Qix's
    // sound handler is "LDS ; CLI ; LDAA <PIA>" - the read clears the IRQ).
    r.mem[0x1003] = 0x0e;                   // CLI
    r.mem[0x1004] = 0x01;                   // NOP
    r.cpu.cc = 0xc0 | CC_I;
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 2);              // CLI
    CHECK_EQ(r.cpu.run(1), 2);              // NOP runs first
    CHECK_EQ(r.cpu.pc, 0x1005);
    CHECK_EQ(r.cpu.run(1), 12);             // then the IRQ
    CHECK_EQ(r.cpu.pc, 0x3000);
    r.cpu.irq(false);
    r.cpu.run(1);                           // RTI
    CHECK_EQ(r.cpu.pc, 0x1005);
    // CLI with I already clear: no wait. TAP: always one instruction.
    r.mem[0x1005] = 0x0e;
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 12);             // taken before the CLI even runs
    r.cpu.irq(false);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.run(1), 2);              // the CLI
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 12);             // taken at once after it
    r.cpu.irq(false);
    r.cpu.run(1);
    CHECK_EQ(r.cpu.pc, 0x1006);
    r.mem[0x1006] = 0x06;                   // TAP (A = C0: I clear)
    r.mem[0x1007] = 0x01;
    r.cpu.a = 0xc0;
    r.cpu.cc = 0xc0 | CC_I;
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 2);
    CHECK_EQ(r.cpu.run(1), 2);              // NOP first
    CHECK_EQ(r.cpu.run(1), 12);
    r.cpu.irq(false);
    // A branch to itself idles - but not past an IRQ that is due.
    r.mem[0x1010] = 0x0e;                   // CLI
    r.mem[0x1011] = 0x20;                   // BRA *
    r.mem[0x1012] = 0xfe;
    r.cpu.pc = 0x1010;
    r.cpu.s = 0x01ff;
    r.cpu.cc = 0xc0 | CC_I;
    r.cpu.irq(true);
    CHECK_EQ(r.cpu.run(1), 2);              // CLI
    CHECK_EQ(r.cpu.run(5), 4 + 12);         // BRA once (no idling: the IRQ is due), the IRQ
    CHECK_EQ(r.cpu.pc, 0x3000);
    r.cpu.irq(false);
    r.cpu.run(1);                           // RTI
    CHECK_EQ(r.cpu.run(100), 100);          // now BRA idles to the end
    CHECK_EQ(r.cpu.pc, 0x1011);
}

// A checksum loop: sum bytes 0x2000-0x20FF into A:B with carry.
void test_program()
{
    Rig r;
    uint32_t want = 0;
    for (int i = 0; i < 256; ++i) {
        r.mem[0x2000 + i] = uint8_t(i * 13 + 7);
        want += uint8_t(i * 13 + 7);
    }
    r.put(0x1000, {
        0x4f, 0x5f,             // CLRA ; CLRB
        0xce, 0x20, 0x00,       // LDX #$2000
        0xeb, 0x00,             // loop: ADDB 0,X
        0x89, 0x00,             //       ADCA #0
        0x08,                   //       INX
        0x8c, 0x21, 0x00,       //       CPX #$2100
        0x26, 0xf6,             //       BNE loop
        0xb7, 0x30, 0x00,       // STAA $3000
        0xf7, 0x30, 0x01,       // STAB $3001
        0x20, 0xfe,             // BRA *
    });
    r.cpu.pc = 0x1000;
    r.cpu.run(10000);
    CHECK_EQ((r.mem[0x3000] << 8) | r.mem[0x3001], want);
    CHECK_EQ(r.cpu.pc, 0x1015);
}

// BRA * passes the rest of the slice; "DECA ; BNE" delay loops run in bulk
// and must match single steps exactly, whatever the slice size.
void test_idle_and_delay()
{
    Rig r;
    r.put(0x1000, {0x20, 0xfe});
    r.cpu.pc = 0x1000;
    CHECK_EQ(r.cpu.run(100), 100);
    CHECK_EQ(r.cpu.pc, 0x1000);
    struct State { int cycles; uint16_t pc; uint8_t a, b, cc; };
    int bad = 0;
    for (int dec : {0x4a, 0x5a})
        for (int start : {1, 2, 3, 7, 0x80, 0x81, 255, 0}) {
            // LDAA/LDAB #start ; loop: DEC ; BNE loop ; then NOPs
            auto setup = [&](Rig& q) {
                q.put(0x1000, {uint8_t(dec == 0x4a ? 0x86 : 0xc6), uint8_t(start), uint8_t(dec), 0x26, 0xfd});
                for (int i = 0x1005; i < 0x4000; ++i) q.mem[i] = 0x01;
                q.cpu.pc = 0x1000;
                q.cpu.cc = 0xc0;
            };
            // Reference: one instruction per run (the bulk path needs room).
            Rig ref;
            setup(ref);
            std::vector<State> steps;
            int total = 0;
            while (total < 9000) {
                total += ref.cpu.run(1);
                steps.push_back({total, ref.cpu.pc, ref.cpu.a, ref.cpu.b, ref.cpu.cc});
            }
            for (int slice : {1, 2, 5, 6, 7, 13, 64, 1000, 5000}) {
                Rig q;
                setup(q);
                int t = 0;
                while (t < 2000) {
                    t += q.cpu.run(slice);
                    // Must be at one of the reference's instruction boundaries.
                    bool found = false;
                    for (const State& st : steps)
                        if (st.cycles == t) {
                            found = st.pc == q.cpu.pc && st.a == q.cpu.a && st.b == q.cpu.b && st.cc == q.cpu.cc;
                            break;
                        }
                    if (!found) {
                        if (bad++ < 5) std::printf("FAIL delay %02x start %d slice %d at %d\n", dec, start, slice, t);
                        break;
                    }
                }
            }
        }
    CHECK_EQ(bad, 0);
}

} // namespace

int main()
{
    test_alu();
    test_unary();
    test_cpx_daa();
    test_branches();
    test_cycles_regs();
    test_interrupts();
    test_program();
    test_idle_and_delay();
    std::printf("6800 tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
