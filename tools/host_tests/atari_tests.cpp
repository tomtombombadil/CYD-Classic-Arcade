// Unit tests for the 6502 core's timing and interrupts (the instruction
// set is checked by Klaus Dormann's tests in m6502_test), with hand-made
// programs - no ROMs.
//
//   atari_tests
#include "../../emu/cpu/m6502.h"

#include <cstdio>
#include <cstring>
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
        else { ++g_fail; std::printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, va_, vb_); } \
    } while (0)

// --- 6502 ---------------------------------------------------------------------

struct Rig {
    uint8_t mem[0x10000] = {};
    m6502::Cpu cpu;
    Rig()
    {
        cpu.map_ram(0, 0x10000, mem);
    }
    void load(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) mem[at++] = b;
    }
    void start(uint16_t pc)
    {
        mem[0xfffc] = uint8_t(pc);
        mem[0xfffd] = uint8_t(pc >> 8);
        cpu.reset();
    }
    // Cycles of the next instruction.
    int step() { return cpu.run(1); }
};

void test_6502_timing()
{
    Rig r;
    r.load(0x0200, {
        0xa2, 0x01,             // LDX #1          2
        0xbd, 0xff, 0x10,       // LDA 10FF,X      4 + 1 (page cross)
        0xbd, 0x00, 0x10,       // LDA 1000,X      4
        0x9d, 0x00, 0x10,       // STA 1000,X      5 (always)
        0xa0, 0x10,             // LDY #10         2
        0xb1, 0x80,             // LDA (80),Y      5 + 1 (80/81 -> 10F8)
        0xd0, 0x00,             // BNE +0          2 + 1 (taken)
        0xf0, 0x10,             // BEQ +10         2 (not taken)
        0xfe, 0x00, 0x10,       // INC 1000,X      7
        0x6c, 0xff, 0x30,       // JMP (30FF)      5, high byte from 3000
    });
    r.mem[0x80] = 0xf8;
    r.mem[0x81] = 0x10;
    r.mem[0x10f8 + 0x10] = 0x42;
    r.mem[0x30ff] = 0x34;
    r.mem[0x3000] = 0x12;
    r.mem[0x3100] = 0x56;
    r.start(0x0200);
    CHECK_EQ(r.step(), 2);
    CHECK_EQ(r.step(), 5);
    CHECK_EQ(r.step(), 4);
    CHECK_EQ(r.step(), 5);
    CHECK_EQ(r.step(), 2);
    CHECK_EQ(r.step(), 6);
    CHECK_EQ(r.cpu.a, 0x42);
    CHECK_EQ(r.step(), 3);
    CHECK_EQ(r.step(), 2);
    CHECK_EQ(r.step(), 7);
    CHECK_EQ(r.step(), 5);
    CHECK_EQ(r.cpu.pc, 0x1234);       // the NMOS page wrap

    // A taken branch into the next page: 4 cycles.
    Rig b;
    b.load(0x02f0, {0x18, 0x90, 0x7f});     // CLC; BCC +127 -> 0372
    b.start(0x02f0);
    b.step();
    CHECK_EQ(b.step(), 4);
    CHECK_EQ(b.cpu.pc, 0x0372);
}

void test_6502_interrupts()
{
    Rig r;
    // Main: CLI, then NOPs. IRQ handler at 0400: INX; RTI. NMI at 0500: INY; RTI.
    r.load(0x0200, {0x58, 0xea, 0xea, 0xea, 0x78, 0xea, 0xea});
    r.load(0x0400, {0xe8, 0x40});
    r.load(0x0500, {0xc8, 0x40});
    r.mem[0xfffe] = 0x00; r.mem[0xffff] = 0x04;
    r.mem[0xfffa] = 0x00; r.mem[0xfffb] = 0x05;
    r.start(0x0200);
    r.cpu.irq(true);
    CHECK_EQ(r.step(), 2);              // CLI
    CHECK_EQ(r.step(), 2);              // one more instruction before the IRQ
    CHECK_EQ(r.cpu.pc, 0x0202);
    CHECK_EQ(r.step(), 7);              // the IRQ
    CHECK_EQ(r.cpu.pc, 0x0400);
    CHECK_EQ(r.mem[0x01fb] & (m6502::BF | m6502::IF), 0);   // pushed P: B and I clear
    CHECK(r.cpu.p() & m6502::IF);
    r.step();                           // INX
    r.cpu.irq(false);
    CHECK_EQ(r.step(), 6);              // RTI
    CHECK_EQ(r.cpu.pc, 0x0202);
    CHECK_EQ(r.cpu.x, 1);
    // NMI: taken even with I set, once per edge.
    r.cpu.set_p(m6502::IF);
    r.cpu.nmi();
    CHECK_EQ(r.step(), 7);
    CHECK_EQ(r.cpu.pc, 0x0500);
    r.step();
    r.step();
    CHECK_EQ(r.cpu.y, 1);
    CHECK_EQ(r.cpu.pc, 0x0202);
    r.step();
    CHECK_EQ(r.cpu.pc, 0x0203);

    // BRK: pushes PC + 2 and P with B set.
    Rig k;
    k.load(0x0200, {0x00, 0xff, 0xea});
    k.load(0x0400, {0x40});
    k.mem[0xfffe] = 0x00; k.mem[0xffff] = 0x04;
    k.start(0x0200);
    CHECK_EQ(k.step(), 7);
    CHECK(k.mem[0x01fb] & m6502::BF);
    k.step();
    CHECK_EQ(k.cpu.pc, 0x0202);
}

// The opcode hook sees whether the previous instruction wrote.
struct HookLog {
    int calls = 0;
    bool wrote[8] = {};
};
uint8_t hook(void* ctx, uint16_t, uint8_t op, bool wrote)
{
    HookLog& h = *static_cast<HookLog*>(ctx);
    if (h.calls < 8) h.wrote[h.calls] = wrote;
    ++h.calls;
    return op == 0x02 ? 0xea : op;      // "decrypts" 02 to NOP
}

void test_6502_hook()
{
    Rig r;
    HookLog log;
    r.cpu.ctx = &log;
    r.cpu.opcode_hook = hook;
    r.load(0x0200, {0xea, 0x85, 0x10, 0x02, 0x48, 0xea});   // NOP; STA 10; (02); PHA; NOP
    r.start(0x0200);
    for (int i = 0; i < 5; ++i) r.step();
    CHECK_EQ(log.calls, 5);
    CHECK(!log.wrote[1]);
    CHECK(log.wrote[2]);                // after the STA
    CHECK(!log.wrote[3]);
    CHECK(log.wrote[4]);                // after the PHA
    CHECK_EQ(r.cpu.illegal_count, 0);
    CHECK_EQ(r.cpu.pc, 0x0206);
}

} // namespace

int main()
{
    test_6502_timing();
    test_6502_interrupts();
    test_6502_hook();
    std::printf("atari tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
