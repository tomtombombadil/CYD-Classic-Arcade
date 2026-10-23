// Unit tests for Elevator Action's parts, with hand-made programs and data -
// no ROMs: the 68705P3 core (every ALU result and flag against a plain
// model, the read-modify-write group, each addressing mode, every branch,
// BRSET / BRCLR / BSET / BCLR, the stack and its wrap, SWI / RTI, the /INT
// edge and BIL / BIH, the timer and its interrupt, the ports and DDRs, the
// cycle table), the Taito SJ board on tiny Z80 and 6805 programs (the MCU's
// latches both ways, its bus requests writing and reading the Z80's
// memory, the graphics ROM port, inputs and DIPs, the sound command and
// NMI, the sound status, the VBLANK IRQ, the watchdog), the picture
// against a per-pixel model of the hardware on random video RAM (dense and
// sparse characters, one or many column scrolls), and that
// idle skipping (the MCU's loop, the main CPU's frame wait, the sound
// CPU's main loop) leaves everything as it is without it.
//
//   elevator_tests
#include "../../emu/cpu/m68705.h"
#include "../../emu/machines/elevator.h"

#include <cstdio>
#include <cstdlib>
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

using namespace m68705;

// --- the 68705 ---------------------------------------------------------------------

struct Rig {
    uint8_t rom[0x800] = {};
    Cpu cpu;
    uint8_t in[3] = {0xff, 0xff, 0xff};
    uint8_t out_latch[3] = {}, out_ddr[3] = {};
    int outs = 0;
    static uint8_t pin(void* c, int p) { return static_cast<Rig*>(c)->in[p]; }
    static void pout(void* c, int p, uint8_t l, uint8_t d)
    {
        Rig& r = *static_cast<Rig*>(c);
        r.out_latch[p] = l;
        r.out_ddr[p] = d;
        ++r.outs;
    }
    Rig()
    {
        vec(kVecReset, 0x100);
        cpu.ctx = this;
        cpu.port_in = pin;
        cpu.port_out = pout;
        cpu.init(rom);
    }
    void vec(uint16_t at, uint16_t to) { rom[at] = uint8_t(to >> 8); rom[at + 1] = uint8_t(to); }
    void put(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) rom[at++] = b;
    }
    // Runs one instruction at 0x100 (interrupts masked unless the caller
    // cleared I).
    int one(std::initializer_list<uint8_t> bytes)
    {
        put(0x100, bytes);
        cpu.pc = 0x100;
        return cpu.run(1);
    }
};

uint8_t nzf(unsigned r) { return uint8_t((r & 0x80 ? CC_N : 0) | ((r & 0xff) ? 0 : CC_Z)); }

void test_mcu_alu()
{
    Rig r;
    int bad = 0;
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            for (int c = 0; c < 2; ++c) {
                const uint8_t cc0 = uint8_t(0xe0 | CC_I | (c ? CC_C : 0) | CC_H);
                struct { uint8_t op; int kind; } ops[] = {
                    {0xab, 0}, {0xa9, 1}, {0xa0, 2}, {0xa2, 3}, {0xa1, 4}, {0xa4, 5}, {0xaa, 6}, {0xa8, 7},
                    {0xa5, 8}, {0xa6, 9},
                };
                for (const auto& o : ops) {
                    r.cpu.a = uint8_t(a);
                    r.cpu.cc = cc0;
                    const int cyc = r.one({o.op, uint8_t(b)});
                    int res = a;
                    uint8_t wf = cc0;
                    switch (o.kind) {
                    case 0: case 1: {
                        const unsigned u = unsigned(a + b + (o.kind ? c : 0));
                        res = u & 0xff;
                        wf = uint8_t((cc0 & ~(CC_H | CC_N | CC_Z | CC_C)) | nzf(u) | (u > 0xff ? CC_C : 0) |
                                     (((a & 15) + (b & 15) + (o.kind ? c : 0)) > 15 ? CC_H : 0));
                        break;
                    }
                    case 2: case 3: case 4: {
                        const int s = a - b - (o.kind == 3 ? c : 0);
                        if (o.kind != 4) res = s & 0xff;
                        wf = uint8_t((cc0 & ~(CC_N | CC_Z | CC_C)) | nzf(unsigned(s) & 0xff) | (s < 0 ? CC_C : 0));
                        break;
                    }
                    case 5: res = a & b; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(res))); break;
                    case 6: res = a | b; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(res))); break;
                    case 7: res = a ^ b; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(res))); break;
                    case 8: wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(a & b))); break;
                    default: res = b; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(b))); break;
                    }
                    if (r.cpu.a != res || r.cpu.cc != wf || cyc != 2) {
                        if (bad++ < 5)
                            std::printf("  alu op %02x a %02x b %02x c %d: a %02x cc %02x (want %02x %02x) cyc %d\n", o.op,
                                        a, b, c, r.cpu.a, r.cpu.cc, res, wf, cyc);
                    }
                }
                // CPX
                r.cpu.x = uint8_t(a);
                r.cpu.cc = cc0;
                r.one({0xa3, uint8_t(b)});
                const int s = a - b;
                if (r.cpu.cc != uint8_t((cc0 & ~(CC_N | CC_Z | CC_C)) | nzf(unsigned(s) & 0xff) | (s < 0 ? CC_C : 0))) ++bad;
            }
    CHECK_EQ(bad, 0);
}

void test_mcu_rmw()
{
    Rig r;
    int bad = 0;
    for (int v = 0; v < 256; ++v)
        for (int c = 0; c < 2; ++c) {
            const uint8_t cc0 = uint8_t(0xe0 | CC_I | (c ? CC_C : 0));
            struct { uint8_t lo; } ops[] = {{0x0}, {0x3}, {0x4}, {0x6}, {0x7}, {0x8}, {0x9}, {0xa}, {0xc}, {0xd}, {0xf}};
            for (const auto& o : ops) {
                int res = v;
                uint8_t wf;
                const uint8_t base = uint8_t(cc0 & ~(CC_N | CC_Z | CC_C));
                switch (o.lo) {
                case 0x0: res = (256 - v) & 0xff; wf = uint8_t(base | nzf(unsigned(res)) | (v ? CC_C : 0)); break;
                case 0x3: res = ~v & 0xff; wf = uint8_t(base | nzf(unsigned(res)) | CC_C); break;
                case 0x4: res = v >> 1; wf = uint8_t(base | nzf(unsigned(res)) | (v & 1)); break;
                case 0x6: res = (v >> 1) | (c << 7); wf = uint8_t(base | nzf(unsigned(res)) | (v & 1)); break;
                case 0x7: res = (v >> 1) | (v & 0x80); wf = uint8_t(base | nzf(unsigned(res)) | (v & 1)); break;
                case 0x8: res = (v << 1) & 0xff; wf = uint8_t(base | nzf(unsigned(res)) | (v >> 7)); break;
                case 0x9: res = ((v << 1) | c) & 0xff; wf = uint8_t(base | nzf(unsigned(res)) | (v >> 7)); break;
                case 0xa: res = (v - 1) & 0xff; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(res))); break;
                case 0xc: res = (v + 1) & 0xff; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(res))); break;
                case 0xd: wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | nzf(unsigned(v))); break;
                default: res = 0; wf = uint8_t((cc0 & ~(CC_N | CC_Z)) | CC_Z); break;
                }
                // A (4x), X (5x), direct (3x), indexed + 1 (6x), indexed (7x)
                r.cpu.a = uint8_t(v);
                r.cpu.cc = cc0;
                int cyc = r.one({uint8_t(0x40 | o.lo)});
                if (r.cpu.a != res || r.cpu.cc != wf || cyc != 4) ++bad;
                r.cpu.x = uint8_t(v);
                r.cpu.cc = cc0;
                cyc = r.one({uint8_t(0x50 | o.lo)});
                if (r.cpu.x != res || r.cpu.cc != wf || cyc != 4) ++bad;
                r.cpu.wr(0x40, uint8_t(v));
                r.cpu.cc = cc0;
                cyc = r.one({uint8_t(0x30 | o.lo), 0x40});
                if (r.cpu.rd(0x40) != (o.lo == 0xd ? v : res) || r.cpu.cc != wf || cyc != 6) ++bad;
                r.cpu.wr(0x45, uint8_t(v));
                r.cpu.x = 0x40;
                r.cpu.cc = cc0;
                cyc = r.one({uint8_t(0x60 | o.lo), 0x05});
                if (r.cpu.rd(0x45) != (o.lo == 0xd ? v : res) || r.cpu.cc != wf || cyc != 7) ++bad;
                r.cpu.wr(0x50, uint8_t(v));
                r.cpu.x = 0x50;
                r.cpu.cc = cc0;
                cyc = r.one({uint8_t(0x70 | o.lo)});
                if (r.cpu.rd(0x50) != (o.lo == 0xd ? v : res) || r.cpu.cc != wf || cyc != 6) ++bad;
            }
        }
    CHECK_EQ(bad, 0);
    // Undocumented opcodes: 2-cycle, 1-byte no-ops, counted.
    r.cpu.a = 0x12;
    CHECK_EQ(r.one({0x31, 0x00}), 2);
    CHECK_EQ(r.cpu.pc, 0x101);
    CHECK_EQ(r.cpu.illegal_count, 1u);
}

void test_mcu_modes()
{
    Rig r;
    // Data for each mode: LDA reads it, STA writes it back elsewhere.
    r.rom[0x123] = 0x11;                                // extended
    r.rom[0x234] = 0x22;                                // indexed + 2 bytes (0x200 + X 0x34)
    r.cpu.wr(0x44, 0x33);                               // direct
    r.cpu.wr(0x57, 0x44);                               // indexed + 1 (X 0x50 + 7)
    r.cpu.wr(0x60, 0x55);                               // indexed
    r.cpu.x = 0x34;
    CHECK_EQ(r.one({0xd6, 0x02, 0x00}), 6);
    CHECK_EQ(r.cpu.a, 0x22);
    CHECK_EQ(r.one({0xc6, 0x01, 0x23}), 5);
    CHECK_EQ(r.cpu.a, 0x11);
    CHECK_EQ(r.one({0xb6, 0x44}), 4);
    CHECK_EQ(r.cpu.a, 0x33);
    r.cpu.x = 0x50;
    CHECK_EQ(r.one({0xe6, 0x07}), 5);
    CHECK_EQ(r.cpu.a, 0x44);
    r.cpu.x = 0x60;
    CHECK_EQ(r.one({0xf6}), 4);
    CHECK_EQ(r.cpu.a, 0x55);
    // STA / STX: N Z; cycles
    r.cpu.a = 0x80;
    CHECK_EQ(r.one({0xb7, 0x41}), 5);
    CHECK_EQ(r.cpu.rd(0x41), 0x80);
    CHECK(r.cpu.cc & CC_N);
    r.cpu.x = 0;
    CHECK_EQ(r.one({0xcf, 0x00, 0x42}), 6);
    CHECK_EQ(r.cpu.rd(0x42), 0);
    CHECK(r.cpu.cc & CC_Z);
    r.cpu.a = 7;
    r.cpu.x = 0x40;
    CHECK_EQ(r.one({0xe7, 0x03}), 6);
    CHECK_EQ(r.cpu.rd(0x43), 7);
    CHECK_EQ(r.one({0xd7, 0x00, 0x05}), 7);
    CHECK_EQ(r.cpu.rd(0x45), 7);
    CHECK_EQ(r.one({0xf7}), 5);
    CHECK_EQ(r.cpu.rd(0x40), 7);
    // LDX / TAX / TXA
    CHECK_EQ(r.one({0xae, 0x99}), 2);
    CHECK_EQ(r.cpu.x, 0x99);
    r.one({0x9f});
    CHECK_EQ(r.cpu.a, 0x99);
    r.cpu.a = 0x13;
    r.one({0x97});
    CHECK_EQ(r.cpu.x, 0x13);
    // JMP in each mode
    CHECK_EQ(r.one({0xcc, 0x03, 0x21}), 4);
    CHECK_EQ(r.cpu.pc, 0x321);
    CHECK_EQ(r.one({0xbc, 0x99}), 3);
    CHECK_EQ(r.cpu.pc, 0x99);
    r.cpu.x = 0x10;
    CHECK_EQ(r.one({0xdc, 0x02, 0x00}), 5);
    CHECK_EQ(r.cpu.pc, 0x210);
    CHECK_EQ(r.one({0xec, 0x05}), 4);
    CHECK_EQ(r.cpu.pc, 0x15);
    CHECK_EQ(r.one({0xfc}), 3);
    CHECK_EQ(r.cpu.pc, 0x10);
    // Addresses wrap at 11 bits.
    CHECK_EQ(r.one({0xcc, 0xf9, 0x00}), 4);
    CHECK_EQ(r.cpu.pc, 0x100);                          // F900 & 7FF
}

void test_mcu_branches()
{
    // 2x: BRA BRN BHI BLS BCC BCS BNE BEQ BHCC BHCS BPL BMI BMC BMS BIL BIH
    for (int op = 0x20; op < 0x30; ++op)
        for (int f = 0; f < 32; ++f)
            for (int pin = 0; pin < 2; ++pin) {
                Rig r;
                const uint8_t cc = uint8_t(0xe0 | f);
                bool t;
                switch (op & 0x0e) {
                case 0x0: t = true; break;
                case 0x2: t = !(cc & (CC_C | CC_Z)); break;
                case 0x4: t = !(cc & CC_C); break;
                case 0x6: t = !(cc & CC_Z); break;
                case 0x8: t = !(cc & CC_H); break;
                case 0xa: t = !(cc & CC_N); break;
                case 0xc: t = !(cc & CC_I); break;
                default: t = pin; break;                // BIL: the pin low = asserted
                }
                if (op & 1) t = !t;
                // (with I clear an asserted /INT would be taken: the pin only with I set)
                if (pin && !(cc & CC_I)) continue;
                r.cpu.set_int(pin);
                r.cpu.cc = cc;
                const int cyc = r.one({uint8_t(op), 0x10});
                if (r.cpu.pc != (t ? 0x112 : 0x102) || cyc != 4) {
                    std::printf("  branch %02x cc %02x pin %d: pc %03x\n", op, cc, pin, r.cpu.pc);
                    ++g_fail;
                }
            }
    Rig r;
    // backwards
    r.cpu.cc = 0xe0 | CC_I;
    r.one({0x20, 0xfe});
    CHECK_EQ(r.cpu.pc, 0x100);
    // BRSET / BRCLR: the bit to C, 10 cycles
    r.cpu.wr(0x30, 0x24);
    for (int b = 0; b < 8; ++b) {
        const bool set = (0x24 >> b) & 1;
        r.cpu.cc = 0xe0 | CC_I;
        CHECK_EQ(r.one({uint8_t(2 * b), 0x30, 0x20}), 10);
        CHECK_EQ(r.cpu.pc, set ? 0x123 : 0x103);
        CHECK_EQ(r.cpu.cc & CC_C, set ? CC_C : 0);
        r.cpu.cc = 0xe0 | CC_I | CC_C;
        r.one({uint8_t(2 * b + 1), 0x30, 0x20});
        CHECK_EQ(r.cpu.pc, set ? 0x103 : 0x123);
        CHECK_EQ(r.cpu.cc & CC_C, set ? CC_C : 0);
    }
    // BSET / BCLR: 7 cycles, flags untouched
    r.cpu.cc = 0xe0 | CC_I;
    CHECK_EQ(r.one({0x1e, 0x30}), 7);                   // BSET 7
    CHECK_EQ(r.cpu.rd(0x30), 0xa4);
    r.one({0x15, 0x30});                                // BCLR 2
    CHECK_EQ(r.cpu.rd(0x30), 0xa0);
    CHECK_EQ(r.cpu.cc, 0xe0 | CC_I);
}

void test_mcu_stack_and_interrupts()
{
    Rig r;
    // JSR / RTS, BSR; SP wraps 0x60 <-> 0x7F
    r.cpu.sp = 0x7f;
    r.put(0x200, {0x81});                               // RTS
    CHECK_EQ(r.one({0xcd, 0x02, 0x00}), 8);             // JSR ext
    CHECK_EQ(r.cpu.pc, 0x200);
    CHECK_EQ(r.cpu.sp, 0x7d);
    CHECK_EQ(r.cpu.rd(0x7f), 0x03);                     // return address, low byte first
    CHECK_EQ(r.cpu.rd(0x7e), 0x01);
    CHECK_EQ(r.cpu.run(1), 6);
    CHECK_EQ(r.cpu.pc, 0x103);
    CHECK_EQ(r.cpu.sp, 0x7f);
    CHECK_EQ(r.one({0xad, 0x10}), 8);                   // BSR
    CHECK_EQ(r.cpu.pc, 0x112);
    r.cpu.sp = 0x60;
    r.one({0xad, 0x00});
    CHECK_EQ(r.cpu.sp, 0x7e);                           // 60 then wrapped to 7F
    r.one({0x9c});                                      // RSP
    CHECK_EQ(r.cpu.sp, 0x7f);
    // SWI: pushes PC, X, A, CC; sets I; RTI pulls them
    r.vec(kVecSwi, 0x300);
    r.put(0x300, {0x80});
    r.cpu.a = 0x12;
    r.cpu.x = 0x34;
    r.cpu.cc = 0xe0 | CC_C;
    CHECK_EQ(r.one({0x83}), 11);
    CHECK_EQ(r.cpu.pc, 0x300);
    CHECK(r.cpu.cc & CC_I);
    CHECK_EQ(r.cpu.sp, 0x7a);
    CHECK_EQ(r.cpu.rd(0x7b), 0xe0 | CC_C);
    CHECK_EQ(r.cpu.rd(0x7c), 0x12);
    CHECK_EQ(r.cpu.rd(0x7d), 0x34);
    r.cpu.a = r.cpu.x = 0;
    CHECK_EQ(r.cpu.run(1), 9);
    CHECK_EQ(r.cpu.pc, 0x101);
    CHECK_EQ(r.cpu.a, 0x12);
    CHECK_EQ(r.cpu.x, 0x34);
    CHECK_EQ(r.cpu.cc, 0xe0 | CC_C);

    // /INT: an edge latches a request, taken once I is clear; 11 cycles
    Rig s;
    s.vec(kVecInt, 0x200);
    s.put(0x200, {0x3c, 0x40, 0x80});                   // INC $40; RTI
    s.put(0x100, {0x9d, 0x9d, 0x9a, 0x20, 0xfe});       // NOP NOP CLI; BRA *
    s.cpu.pc = 0x100;
    s.cpu.set_int(true);
    s.cpu.run(4);                                       // the NOPs: masked
    CHECK_EQ(s.cpu.rd(0x40), 0);
    s.cpu.run(2);                                       // CLI
    CHECK_EQ(s.cpu.run(1), 11);                         // taken
    CHECK_EQ(s.cpu.pc, 0x200);
    s.cpu.run(15);
    CHECK_EQ(s.cpu.rd(0x40), 1);
    s.cpu.run(40);                                      // still asserted: no new edge, no new interrupt
    CHECK_EQ(s.cpu.rd(0x40), 1);
    s.cpu.set_int(false);
    s.cpu.set_int(true);
    s.cpu.run(40);
    CHECK_EQ(s.cpu.rd(0x40), 2);

    // The timer: internal clock / 2^n, TIR when it passes zero, the
    // interrupt while TIM is clear (level: again until TIR is cleared).
    Rig t;
    t.vec(kVecTimer, 0x200);
    t.put(0x200, {0x3c, 0x40, 0xa6, 0x02, 0xb7, 0x09, 0x80});   // INC $40; LDA #02; STA TCR (clear TIR); RTI
    t.put(0x100, {0xa6, 0x0a, 0xb7, 0x08,               // LDA #10; STA TDR
                  0xa6, 0x0a, 0xb7, 0x09,               // LDA #0A: /4, prescaler cleared, unmasked
                  0x9a, 0x20, 0xfe});                   // CLI; BRA *
    t.cpu.pc = 0x100;
    t.cpu.run(2 + 4);                                   // TDR written
    CHECK_EQ(t.cpu.tdr, 10);
    t.cpu.run(2 + 5);                                   // TCR written (prescaler cleared; the STA's 5 cycles count after)
    CHECK_EQ(t.cpu.tcr & 0x7f, 0x02);
    CHECK_EQ(t.cpu.tdr, 9);
    CHECK_EQ(t.cpu.cycles_to_timer_irq(), 35);          // 9 counts of 4, 1 cycle into this one
    // The request comes with the instruction that reaches it, the
    // interrupt before the next one.
    int done = 0;
    while (done < 35) {
        CHECK(!(t.cpu.tcr & 0x80));
        done += t.cpu.run(1);
    }
    CHECK(t.cpu.tcr & 0x80);
    CHECK_EQ(t.cpu.tdr, 0);
    CHECK_EQ(t.cpu.run(1), 11);
    CHECK_EQ(t.cpu.pc, 0x200);
    t.cpu.run(20);
    CHECK_EQ(t.cpu.rd(0x40), 1);
    CHECK(!(t.cpu.tcr & 0x80));
    t.cpu.run(256 * 4 + 40);                            // a whole turn: one more
    CHECK_EQ(t.cpu.rd(0x40), 2);
    // Masked (TIM): TIR still set, no interrupt
    t.cpu.wr(0x09, 0x42);
    t.cpu.run(256 * 4 * 2);
    CHECK(t.cpu.tcr & 0x80);
    CHECK_EQ(t.cpu.rd(0x40), 2);
    CHECK_EQ(t.cpu.cycles_to_timer_irq(), -1);
    // Reset: TDR FF, TCR 7F
    t.cpu.reset();
    CHECK_EQ(t.cpu.tdr, 0xff);
    CHECK_EQ(t.cpu.rd(0x09), 0x77);                     // (bit 3 reads 0)
}

void test_mcu_ports()
{
    Rig r;
    r.cpu.cc = 0xe0 | CC_I;
    r.in[0] = 0x5a;
    r.in[2] = 0x03;
    CHECK_EQ(r.cpu.rd(0x00), 0x5a);                     // all inputs after reset
    CHECK_EQ(r.cpu.rd(0x02), 0xf3);                     // port C: four bits
    CHECK_EQ(r.cpu.rd(0x04), 0xff);                     // DDRs read 0xFF
    r.cpu.a = 0xf0;
    CHECK_EQ(r.one({0xb7, 0x04}), 5);                   // STA DDRA
    CHECK_EQ(r.out_ddr[0], 0xf0);
    r.cpu.a = 0x3c;
    r.one({0xb7, 0x00});                                // STA PORTA: latch 3C
    CHECK_EQ(r.out_latch[0], 0x3c);
    CHECK_EQ(r.cpu.rd(0x00), 0x3a);                     // 3C & F0 | 5A & 0F
    // BSET on a port: read (pins) - modify - write (latch)
    r.one({0x10, 0x00});                                // BSET 0,PORTA
    CHECK_EQ(r.out_latch[0], 0x3b);                     // 3A | 1: the input pins' bits went into the latch
    // the timer's registers
    r.cpu.wr(0x08, 0x77);
    CHECK_EQ(r.cpu.rd(0x08), 0x77);
    CHECK_EQ(r.cpu.rd(0x0b), 0xff);
    CHECK_EQ(r.cpu.rd(0x07), 0xff);
    // EPROM can't be written
    r.cpu.wr(0x200, 0x12);
    CHECK_EQ(r.cpu.rd(0x200), 0);
    // RAM 10-7F, mirrored by nothing
    r.cpu.wr(0x10, 0x99);
    CHECK_EQ(r.cpu.ram[0], 0x99);
    CHECK_EQ(r.cpu.rd(0x810), 0x99);                    // 11-bit address
}

// --- the board ---------------------------------------------------------------------

struct Board {
    std::vector<uint8_t> cpu, snd, mcu, gfx, prom;
    Board() : cpu(0x8000, 0), snd(0x2000, 0xff), mcu(0x800, 0), gfx(0x8000), prom(0x100, 0)
    {
        for (size_t i = 0; i < gfx.size(); ++i) gfx[i] = uint8_t(i * 7 + (i >> 8));
    }
    void put(std::vector<uint8_t>& v, int at, std::initializer_list<uint8_t> b)
    {
        for (uint8_t x : b) v[size_t(at++)] = x;
    }
    elevator::Roms roms() { return {cpu.data(), snd.data(), mcu.data(), gfx.data(), prom.data()}; }
};

// The main program: a command to the MCU and its answer, the graphics
// port, the sound command and status, then the game's frame wait at 73F2
// (inputs to RAM, the watchdog). VBLANK IRQ: counts at 8000, clears 80AB.
void main_program(Board& b)
{
    b.put(b.cpu, 0x0000, {0xc3, 0x00, 0x01});           // JP 0100
    b.put(b.cpu, 0x0100, {
        0x31, 0x00, 0x88,               // LD SP,8800
        0xed, 0x56,                     // IM 1
        0x3e, 0x5a, 0x32, 0x21, 0x81,   // LD A,5A; LD (8121),A  (the MCU reads it)
        0x3e, 0x37, 0x32, 0x00, 0x88,   // LD A,37; LD (8800),A  command to the MCU
        0x3a, 0x01, 0x88,               // 010F LD A,(8801)
        0xe6, 0x02, 0x28, 0xf9,         //      AND 2; JR Z,010F  (its answer)
        0x3a, 0x00, 0x88,               // LD A,(8800)
        0x32, 0x10, 0x80,               // LD (8010),A
        0x21, 0x10, 0x00, 0x22, 0x09, 0xd5,     // LD HL,0010; LD (D509),HL
        0x3a, 0x04, 0xd4, 0x32, 0x11, 0x80,     // graphics byte 0010 -> 8011
        0x3a, 0x07, 0xd4, 0x32, 0x12, 0x80,     // 0011 (D407: a mirror) -> 8012
        0x3e, 0x02, 0x32, 0x0e, 0xd5,   // sound on
        0x3e, 0x99, 0x32, 0x0b, 0xd5,   // the sound command
        0xc3, 0xe0, 0x73,               // JP 73E0
    });
    b.put(b.cpu, 0x0038, {
        0xf5, 0x3a, 0x00, 0x80, 0x3c, 0x32, 0x00, 0x80,     // PUSH AF; INC (8000)
        0xaf, 0x32, 0xab, 0x80, 0xf1, 0xed, 0x4d,           // XOR A; LD (80AB),A; POP AF; RETI
    });
    b.put(b.cpu, 0x73e0, {
        0x3e, 0x01, 0x32, 0xab, 0x80,   // LD A,1; LD (80AB),A
        0x3a, 0x08, 0xd4, 0x32, 0x20, 0x80,     // IN0 -> 8020
        0x32, 0x0d, 0xd5,               // watchdog
        0xfb, 0x00, 0x00, 0x00,         // EI; NOPs
        0x3a, 0xab, 0x80, 0xb7, 0x20, 0xfa,     // 73F2: the frame wait
        0x3a, 0x0b, 0xd4, 0x32, 0x21, 0x80,     // IN2 -> 8021
        0x3a, 0x0a, 0xd4, 0x32, 0x22, 0x80,     // DSW A -> 8022
        0x3a, 0x0d, 0xd4, 0x32, 0x23, 0x80,     // IN4 (sound status) -> 8023
        0x3a, 0x0c, 0xd4, 0x32, 0x24, 0x80,     // IN3 -> 8024
        0x3e, 0x0e, 0x32, 0x0e, 0xd4, 0x3a, 0x0f, 0xd4, 0x32, 0x25, 0x80,     // AY 0 port A (DSW B) -> 8025
        0xc3, 0xe0, 0x73,               // JP 73E0
    });
}

// The MCU program: the game's idle loop at 0180 behind a timer interrupt;
// its /INT handler takes the Z80's byte, answers byte + 1, then takes the
// bus to write the byte to 8120 and read 8121 into $31.
void mcu_program(Board& b)
{
    std::vector<uint8_t>& m = b.mcu;
    m[0x80] = 0xff;                                     // (AND $80 in the loop)
    b.put(m, 0x100, {
        0xa6, 0xff, 0xb7, 0x01, 0xb7, 0x05,             // PB = FF, DDRB = FF
        0x3f, 0x04,                                     // DDRA = 0
        0xa6, 0xff, 0xb7, 0x08, 0xa6, 0x07, 0xb7, 0x09, // TDR FF, TCR 07
        0x3f, 0x20, 0x9a, 0xcc, 0x01, 0x80,             // CLR $20; CLI; JMP 0180
    });
    b.put(m, 0x180, {0xb9, 0x23, 0x4c, 0xb7, 0x23, 0xb6, 0x20, 0xa4, 0x28, 0xb4, 0x80, 0x27, 0xf3,
                     0x3f, 0x20, 0x20, 0xef});
    b.put(m, 0x200, {
        0x13, 0x01, 0xb6, 0x00, 0x12, 0x01,             // 68LRD: the byte
        0xb7, 0x30, 0x4c, 0xb7, 0x00,                   // $30; A + 1 on port A
        0xae, 0xff, 0xbf, 0x04,                         // DDRA = FF
        0x15, 0x01, 0x14, 0x01,                         // 68LWR: for the Z80
        0xa6, 0x81, 0xb7, 0x00, 0x1f, 0x01, 0x1e, 0x01, // address high 81
        0xa6, 0x20, 0xb7, 0x00, 0x1d, 0x01, 0x1c, 0x01, // address low 20
        0xb6, 0x30, 0xb7, 0x00,                         // the byte on the bus
        0x17, 0x01, 0x04, 0x02, 0xfd,                   // BUSRQ; wait for BUSAK
        0x19, 0x01, 0x18, 0x01,                         // 68WRITE (then address + 1)
        0x3f, 0x04,                                     // DDRA = 0
        0x1b, 0x01, 0xb6, 0x00, 0x1a, 0x01, 0xb7, 0x31, // 68READ -> $31
        0x16, 0x01, 0x3c, 0x32, 0x80,                   // release the bus; INC $32; RTI
    });
    b.put(m, 0x250, {0x3c, 0x40, 0xa6, 0x07, 0xb7, 0x09, 0x80});  // timer: INC $40; TCR 07; RTI
    b.put(m, 0x7f8, {0x02, 0x50, 0x02, 0x00, 0x02, 0x50, 0x01, 0x00});
}

// The sound program: AYs set up (AY 3 port B bit 0 low = NMI on, AY 2
// port A = A0 for the main CPU, AY 1 tone and DAC), the game's main loop
// at 0164; NMI: the command -> 4001, flag 4000; IRQ counts at 4010.
void sound_program(Board& b)
{
    std::vector<uint8_t>& s = b.snd;
    b.put(s, 0x0000, {0x31, 0x00, 0x44, 0xed, 0x56, 0xc3, 0x00, 0x01});
    b.put(s, 0x0038, {0xf5, 0xe5, 0x21, 0x10, 0x40, 0x34, 0xe1, 0xf1, 0xfb, 0xc9});
    b.put(s, 0x0066, {0xf5, 0x3a, 0x00, 0x50, 0x32, 0x01, 0x40, 0x3e, 0x01, 0x32, 0x00, 0x40, 0xf1, 0xed, 0x45});
    auto ay = [&](int& at, uint16_t addr, uint8_t reg, uint8_t v) {
        b.put(s, at, {0x3e, reg, 0x32, uint8_t(addr), uint8_t(addr >> 8), 0x3e, v, 0x32, uint8_t(addr + 1),
                      uint8_t((addr + 1) >> 8)});
        at += 10;
    };
    int at = 0x100;
    b.put(s, at, {0xaf, 0x32, 0x00, 0x40, 0x32, 0x10, 0x40});    // XOR A; (4000) = (4010) = 0
    at += 7;
    ay(at, 0x4804, 7, 0xbf);                            // AY 3: port B out
    ay(at, 0x4804, 15, 0xfe);                           // NMI on
    ay(at, 0x4802, 7, 0x7f);                            // AY 2: port A out
    ay(at, 0x4802, 14, 0xa0);
    ay(at, 0x4800, 7, 0xfe);                            // AY 1: ports out, tone A
    ay(at, 0x4800, 0, 0x40);
    ay(at, 0x4800, 8, 0x0f);
    ay(at, 0x4800, 14, 0xc0);                           // the DAC
    ay(at, 0x4800, 15, 0x00);                           // its volume: full (inverted)
    b.put(s, at, {0xc3, 0x64, 0x01});
    b.put(s, 0x164, {0xfb, 0xdd, 0x21, 0x00, 0x40, 0xdd, 0xcb, 0x00, 0x46, 0xca, 0xd8, 0x01,
                     0xdd, 0xcb, 0x00, 0x86,            // RES 0,(IX+0)
                     0x3a, 0x01, 0x40, 0x32, 0x02, 0x40, // (4001) -> 4002
                     0xc3, 0x64, 0x01});
    b.put(s, 0x1d8, {0xc3, 0x64, 0x01});
}

void test_board()
{
    static Board b;
    main_program(b);
    mcu_program(b);
    sound_program(b);
    static elevator::Machine m, plain;
    m.init(b.roms(), 24000);
    plain.init(b.roms(), 24000);
    plain.set_idle_skip(false);
    int16_t audio[1024], audio2[1024];
    bool same = true;
    int loud = 0;
    for (int f = 0; f < 150; ++f) {
        const int n = m.run_frame(audio, 1024);
        const int n2 = plain.run_frame(audio2, 1024);
        same = same && n == n2 && std::memcmp(audio, audio2, size_t(n) * 2) == 0;
        same = same && m.cpu().pc.w == plain.cpu().pc.w && m.cpu().r_value() == plain.cpu().r_value() &&
               m.cpu().af.w == plain.cpu().af.w;
        same = same && m.sound_cpu().pc.w == plain.sound_cpu().pc.w &&
               m.sound_cpu().r_value() == plain.sound_cpu().r_value() &&
               m.sound_cpu().af.w == plain.sound_cpu().af.w && m.sound_cpu().ix.w == plain.sound_cpu().ix.w;
        same = same && m.mcu().pc == plain.mcu().pc && m.mcu().a == plain.mcu().a && m.mcu().cc == plain.mcu().cc &&
               m.mcu().tdr == plain.mcu().tdr && std::memcmp(m.mcu().ram, plain.mcu().ram, 0x70) == 0;
        for (int i = 0; i < n; ++i) loud = std::max(loud, std::abs(int(audio[i])));
    }
    CHECK(same);
    // ... and the skipping happened.
    CHECK(m.mcu_skipped() > 150 * 10000ull);
    CHECK(m.main_skipped() > 150 * 30000ull);
    CHECK(m.sound_skipped() > 150 * 30000ull);
    CHECK_EQ(plain.mcu_skipped() + plain.main_skipped() + plain.sound_skipped(), 0ull);

    z80::Cpu& c = m.cpu();
    z80::Cpu& s = m.sound_cpu();
    m68705::Cpu& u = m.mcu();
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK(c.rd(0x8000) >= 148);                         // one VBLANK IRQ a frame
    // The MCU: the command, its answer, the bus request's write and read.
    CHECK_EQ(u.ram[0x30 - 0x10], 0x37);
    CHECK_EQ(c.rd(0x8010), 0x38);
    CHECK_EQ(c.rd(0x8120), 0x37);
    CHECK_EQ(u.ram[0x31 - 0x10], 0x5a);
    CHECK_EQ(u.ram[0x32 - 0x10], 1);
    CHECK_EQ(m.bus_grants(), 1u);
    CHECK_EQ(m.host_writes(), 1u);
    // Its timer: every 256 x 128 cycles (750 kHz): 150 frames = 2.53 s.
    const int ticks = u.ram[0x40 - 0x10];
    CHECK(ticks >= 56 && ticks <= 58);
    // The graphics port counts up.
    CHECK_EQ(c.rd(0x8011), b.gfx[0x10]);
    CHECK_EQ(c.rd(0x8012), b.gfx[0x11]);
    // The sound command by NMI, the status back through AY 2 port A.
    CHECK_EQ(s.rd(0x4002), 0x99);
    CHECK_EQ(m.sound_latch(), 0x99);
    CHECK_EQ(c.rd(0x8023), 0xaf);
    // The sound CPU's IRQ: 36.6 a second (held until taken).
    const int irqs = s.rd(0x4010);
    CHECK(irqs >= 91 && irqs <= 94);
    CHECK(m.sound_on());
    CHECK_EQ(m.dac(), 0xc0);
    CHECK(loud > 1000);
    // Inputs (active low) and DIPs.
    CHECK_EQ(c.rd(0x8020), 0xff);
    CHECK_EQ(c.rd(0x8021), 0xff);
    CHECK_EQ(c.rd(0x8022), elevator::kDipDefault);
    CHECK_EQ(c.rd(0x8024), 0xef);                       // service low (active high), tilt high
    CHECK_EQ(c.rd(0x8025), elevator::kDsw2Default);
    m.set_inputs(elevator::kLeft | elevator::kUp | elevator::kFire | elevator::kCoin | elevator::kStart2 |
                 elevator::kService);
    m.set_dips(elevator::kDipLives5 | elevator::kDipNoFlip);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x8020), 0xff & ~0x01 & ~0x08 & ~0x10);
    CHECK_EQ(c.rd(0x8021), 0xff & ~0x20 & ~0x80);
    CHECK_EQ(c.rd(0x8022), elevator::kDipLives5 | elevator::kDipNoFlip);
    CHECK_EQ(c.rd(0x8024), 0xff);
    m.set_inputs(elevator::kRight | elevator::kDown | elevator::kJump | elevator::kCoin2 | elevator::kStart1 |
                 elevator::kTilt);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x8020), 0xff & ~0x02 & ~0x04 & ~0x20);
    CHECK_EQ(c.rd(0x8021), 0xff & ~0x10 & ~0x40);
    CHECK_EQ(c.rd(0x8024), 0xef & ~0x20);
    // The palette: inverted bits, 1k / 470 / 270 ohm.
    c.wr(0xd200 + 2 * 5, 0xfe);                         // entry 5: red bit 2 on (bit 0 of the even byte low)
    c.wr(0xd200 + 2 * 5 + 1, 0xff);
    CHECK_EQ(m.video().pens[5], (138 >> 3) << 11);
    c.wr(0xd280 + 2 * 6, 0xfe);                         // (a mirror) entry 6: white
    c.wr(0xd280 + 2 * 6 + 1, 0x00);
    CHECK_EQ(m.video().pens[6], 0xffff);
    CHECK_EQ(c.rd(0xd20d), 0x00);
    // Sound off: silence.
    c.wr(0xd50e, 0x00);
    for (int f = 0; f < 4; ++f) m.run_frame(audio, 1024);
    const int n = m.run_frame(audio, 1024);
    int quiet = 0;
    for (int i = 0; i < n; ++i) quiet = std::max(quiet, std::abs(int(audio[i])));
    CHECK(quiet < 200);
    // The watchdog: 128 frames without D50D.
    static Board w;
    w.put(w.cpu, 0, {0x18, 0xfe});                      // JR *
    static elevator::Machine wm;
    wm.init(w.roms(), 24000);
    for (int f = 0; f < 130; ++f) wm.run_frame(nullptr, 0);
    CHECK_EQ(wm.watchdog_resets(), 1);
}

// --- the picture ---------------------------------------------------------------------

uint32_t g_rand = 12345;
uint32_t rnd()
{
    g_rand = g_rand * 1103515245u + 12345u;
    return g_rand >> 8;
}

// The hardware's picture, pixel by pixel (MAME's description of the board):
// each plane's pixel, then the PROM picks one from the mask of blank planes.
uint16_t model_pixel(const elevator::VideoState& vs, const uint8_t* prom, int y, int x)
{
    const int line = y + elevator::kFirstLine;
    static const int f1[3] = {3, 1, -1}, f2[3] = {8, 10, 12};
    int pen[4] = {0, 0, 0, 0};
    const int colour[3] = {vs.colorbank[0] & 7, (vs.colorbank[0] >> 4) & 7, vs.colorbank[1] & 7};
    for (int l = 0; l < 3; ++l) {
        if (!(vs.mode & (0x10 << l))) continue;
        const int s = vs.scroll[2 * l];
        const int scrollx = -(s & 0xf8) + ((s + f1[l]) & 7) + f2[l];
        const int sx = (x - scrollx) & 255;
        const int col = sx >> 3;
        const int sy = (line + vs.colscroll[32 * l + col] + vs.scroll[2 * l + 1]) & 255;
        const int code = vs.vram[l][(sy >> 3) * 32 + col];
        const bool bank = l == 0 ? vs.colorbank[0] & 0x08 : l == 1 ? vs.colorbank[0] & 0x80 : vs.colorbank[1] & 0x08;
        const uint8_t* g = vs.chars + (bank ? 0x1800 : 0) + code * 8 + (sy & 7);
        const int bit = sx & 7;
        const int p = ((g[0] >> bit) & 1) | (((g[0x800] >> bit) & 1) << 1) | (((g[0x1000] >> bit) & 1) << 2);
        if (p) pen[l + 1] = colour[l] * 8 + p;
    }
    if (vs.mode & 0x80) {
        const uint8_t* sr = vs.sprites + ((vs.mode & 4) ? 0x80 : 0);
        for (int k = 0x1f; k >= 0; --k) {
            const int which = (k - 1) & 0x1f;
            if (which >= 0x10 && which <= 0x17) continue;
            const uint8_t* s = sr + 4 * which;
            const int sx = uint8_t(s[0] - 1), sy = uint8_t(240 - s[1]);
            if (sy >= 240) continue;
            for (int dx = 0; dx < 16; ++dx) {
                const int px = (sx + dx) & 255;
                if (px != x || x < 3 || x > 254) continue;
                const int r = line - sy;
                if (r < 0 || r > 15) continue;
                const int ry = (s[2] & 2) ? 15 - r : r;
                const int rx = (s[2] & 1) ? 15 - dx : dx;
                const uint8_t* g = vs.chars + ((s[3] & 0x40) ? 0x1800 : 0) + (s[3] & 0x3f) * 32 + (ry & 7) +
                                   (ry & 8) * 2 + (rx & 8);
                const int bit = rx & 7;
                const int p = ((g[0] >> bit) & 1) | (((g[0x800] >> bit) & 1) << 1) | (((g[0x1000] >> bit) & 1) << 2);
                if (p) pen[0] = (2 * ((vs.colorbank[1] >> 4) & 3) + ((s[2] >> 2) & 1)) * 8 + p;
            }
        }
    }
    const int mask = (pen[0] ? 0 : 1) | (pen[1] ? 0 : 2) | (pen[2] ? 0 : 4) | (pen[3] ? 0 : 8);
    const uint8_t v = prom[((vs.priority & 0x0f) << 4) | mask];
    const int pick = (vs.priority & 0x10) ? (v >> 2) & 3 : v & 3;
    const int chosen = pen[pick] ? pen[pick] : colour[2] * 8;
    return vs.pens[chosen];
}

void test_picture()
{
    static Board b;
    b.put(b.cpu, 0, {0x18, 0xfe});                      // JR *: the test writes the video RAM itself
    // A priority PROM: rows 0-7 plain orders in both halves, row 8 not an
    // order (the pixel-by-pixel path).
    static const int orders[8][4] = {{0, 1, 2, 3}, {1, 0, 2, 3}, {3, 2, 1, 0}, {1, 2, 3, 0},
                                     {2, 0, 3, 1}, {0, 3, 1, 2}, {3, 0, 1, 2}, {2, 1, 0, 3}};
    for (int row = 0; row < 16; ++row)
        for (int mask = 0; mask < 16; ++mask) {
            int lo = 3, hi = 3;
            if (row < 8) {
                for (int k = 0; k < 4; ++k)
                    if (!(mask & (1 << orders[row][k]))) { lo = orders[row][k]; break; }
                for (int k = 3; k >= 0; --k)
                    if (!(mask & (1 << orders[row][k]))) { hi = orders[row][k]; break; }
            } else {
                lo = mask & 3;                          // not an order
                hi = (mask >> 2) & 3;
            }
            b.prom[size_t(row * 16 + mask)] = uint8_t(lo | hi << 2);
        }
    static elevator::Machine m;
    m.init(b.roms(), 24000);
    z80::Cpu& c = m.cpu();
    uint16_t line[256];
    int bad = 0, checked = 0;
    for (int trial = 0; trial < 20; ++trial) {
        // trials 12-19: sparse characters (whole blank rows and 32-pixel
        // spans) and playfields with one column scroll for all columns
        const bool sparse = trial >= 12;
        for (int a = 0x9000; a < 0xc000; ++a)
            c.wr(uint16_t(a), uint8_t(sparse ? (rnd() % 4 ? 0 : rnd() & rnd()) : rnd() & rnd()));
        for (int a = 0xc400; a < 0xd000; ++a) c.wr(uint16_t(a), uint8_t(sparse && rnd() % 2 ? 0 : rnd()));
        for (int l = 0; l < 3; ++l) {
            const bool one = sparse && ((trial + l) % 3 != 0);
            const uint8_t v = uint8_t(rnd());
            for (int col = 0; col < 32; ++col) c.wr(uint16_t(0xd000 + 32 * l + col), one ? v : uint8_t(rnd()));
            if (one && (trial & 1)) {                   // a column away and back: one scroll again
                c.wr(uint16_t(0xd000 + 32 * l + 7), uint8_t(v + 1));
                c.wr(uint16_t(0xd000 + 32 * l + 7), v);
            } else if (one) {                           // the first column away: not one scroll any more
                c.wr(uint16_t(0xd000 + 32 * l), uint8_t(v + 8));
            }
        }
        for (int a = 0xd200; a < 0xd280; ++a) c.wr(uint16_t(a), uint8_t(rnd()));
        for (int a = 0xd100; a < 0xd200; ++a) c.wr(uint16_t(a), uint8_t(rnd()));
        for (int a = 0xd500; a < 0xd506; ++a) c.wr(uint16_t(a), uint8_t(rnd()));
        c.wr(0xd506, uint8_t(rnd()));
        c.wr(0xd507, uint8_t(rnd()));
        c.wr(0xd300, uint8_t(trial < 8 ? trial | ((trial & 1) << 4) : trial < 12 ? 8 + (trial & 1) * 0x10 : (trial & 7) | (trial & 0x10)));
        c.wr(0xd600, uint8_t((rnd() & 0xf4) | (trial < 4 || sparse ? 0xf0 : 0)));
        const elevator::VideoState& vs = m.video();
        for (int l = 0; l < 3; ++l) {                   // the count of columns off the first one's scroll
            int diff = 0;
            for (int col = 1; col < 32; ++col) diff += vs.colscroll[32 * l + col] != vs.colscroll[32 * l];
            CHECK_EQ(vs.cs_diff[l], diff);
        }
        for (int y = 0; y < elevator::kScreenH; y += (trial < 2 || sparse ? 1 : 3)) {
            m.render_line(y, line);
            for (int x = 0; x < 256; ++x) {
                ++checked;
                if (line[x] != model_pixel(vs, b.prom.data(), y, x) && bad++ < 5)
                    std::printf("  picture trial %d y %d x %d: %04x, model %04x\n", trial, y, x, line[x],
                                model_pixel(vs, b.prom.data(), y, x));
            }
        }
    }
    CHECK_EQ(bad, 0);
    CHECK(checked > 100000);
    // The char RAM can't be read back (write only).
    c.wr(0x9123, 0x55);
    CHECK_EQ(c.rd(0x9123), 0);
    CHECK_EQ(m.video().chars[0x123], 0x55);
}

} // namespace

int main()
{
    test_mcu_alu();
    test_mcu_rmw();
    test_mcu_modes();
    test_mcu_branches();
    test_mcu_stack_and_interrupts();
    test_mcu_ports();
    test_board();
    test_picture();
    std::printf("Elevator Action tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
