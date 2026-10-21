// Intel MCS-48 (8035 / 8039 / 8048) CPU interpreter, written for this
// project (MIT). Plain C++17, no allocation. All 8048 opcodes (the 8243
// expander's MOVD / ANLD / ORLD read 0 / are ignored - nothing we emulate
// has one), the timer (/32 prescaler) and event counter (T1 falling
// edges), the external and timer interrupts with the one-level "in
// interrupt" lock that RETR releases, register banks, the 8-level stack in
// RAM, program memory banks (SEL MB0 / MB1, A11 held 0 inside an interrupt)
// and machine-cycle timing (one or two cycles an instruction; a cycle is
// 15 clocks: 400,000 a second at 6 MHz). Facts from Intel's MCS-48 user's
// manual and MAME's core (read for facts only).
//
// Program memory is one ROM pointer (masked by rom_mask, so a 2 KB ROM can
// be mirrored over the 4 KB space). External data memory (MOVX), the BUS
// port, P1, P2 and the T0 / T1 test pins go through callbacks.
#pragma once

#include <cstdint>

#ifndef EMU_HOT
#define EMU_HOT
#endif

namespace i8035 {

using read_fn  = uint8_t (*)(void* ctx, uint8_t addr);
using write_fn = void (*)(void* ctx, uint8_t addr, uint8_t value);

// PSW bits (bits 0-2 are the stack pointer, bit 3 reads 1).
enum : uint8_t { CY = 0x80, AC = 0x40, F0 = 0x20, BS = 0x10 };

// Port numbers handed to port_in / port_out.
enum Port : uint8_t { kBus = 0, kP1 = 1, kP2 = 2 };

class Cpu {
public:
    static constexpr int kRamSize = 128;    // room for an 8039 / 8049's 128 bytes

    Cpu();

    // --- wiring -----------------------------------------------------------
    const uint8_t* rom = nullptr;       // program memory
    uint16_t rom_mask  = 0x0fff;        // e.g. 0x7ff: a 2 KB ROM seen twice
    void*    ctx       = nullptr;       // handed to every callback
    read_fn  ext_read  = nullptr;       // MOVX A,@Rr (addr = Rr); unset reads 0xFF
    write_fn ext_write = nullptr;       // MOVX @Rr,A
    // INS A,BUS (kBus) and IN A,P1 / P2: the pins. P1 and P2 are
    // quasi-bidirectional, so the CPU ANDs what the pins read with its own
    // output latch.
    read_fn  port_in   = nullptr;
    write_fn port_out  = nullptr;       // OUTL / ANL / ORL to BUS, P1, P2: the new value
    read_fn  test_in   = nullptr;       // T0 (addr 0) / T1 (addr 1): 0 or 1; unset reads 0

    // --- control ----------------------------------------------------------
    void reset();
    // Runs at least `cycles` machine cycles (finishing the last instruction)
    // and returns the cycles executed. A callback may call stop() to end the
    // run after the current instruction.
    int  run(int cycles);
    void stop() { target_ = 0; }
    // Cycles executed so far in the current run(), counting the current
    // instruction once its callbacks run (they see it as finished).
    int  elapsed() const { return done_; }

    // The INT pin, level-sensitive: true = asserted (the pin is low).
    void irq(bool level) { irq_line_ = level; }
    bool irq_line() const { return irq_line_; }

    // --- registers (public for tests and debuggers) -----------------------
    uint8_t  a = 0;
    uint8_t  psw = 0;           // CY AC F0 BS 1 SP2-0
    uint16_t pc = 0;            // 12 bits
    uint16_t a11 = 0;           // 0x800 after SEL MB1: the bank JMP / CALL go to
    uint8_t  p1 = 0xff, p2 = 0xff;  // output latches
    uint8_t  bus = 0xff;            // BUS port latch (OUTL / ANL / ORL BUS)
    uint8_t  timer = 0;
    uint8_t  ram[kRamSize] = {};
    uint8_t  ram_mask = 0x3f;           // @R0 / @R1: 0x3f = 8035 / 8048, 0x7f = 8039 / 8049
    bool     f1 = false;
    bool     xirq_enabled = false, tirq_enabled = false;
    bool     in_irq = false;            // in an interrupt routine, until RETR
    bool     timer_flag = false;        // overflow seen by JTF (cleared by it)
    bool     timer_overflow = false;    // pending timer interrupt
    uint8_t  count_mode = 0;            // 0 stopped, 1 timer, 2 event counter
    uint8_t  prescaler = 0;

    uint8_t& reg(int n) { return ram[((psw & BS) ? 24 : 0) + n]; }

private:
    bool    irq_line_ = false;
    bool    t1_last_ = false;
    int     done_ = 0, target_ = 0;

    uint8_t fetch()
    {
        const uint8_t v = rom[pc & rom_mask];
        pc = uint16_t((pc & 0x800) | ((pc + 1) & 0x7ff));
        return v;
    }
    uint8_t in(uint8_t port) { return port_in(ctx, port); }
    void    out(uint8_t port, uint8_t v) { port_out(ctx, port, v); }
    bool    test(int n) { return test_in(ctx, uint8_t(n)) != 0; }

    void push_pc();
    void pop_pc(bool restore_psw);
    void jump(uint16_t addr) { pc = uint16_t(addr | (in_irq ? 0 : a11)); }
    void call(uint16_t addr) { push_pc(); jump(addr); }
    // Conditional jumps stay in the page of their address byte.
    void branch(bool cond)
    {
        const uint16_t page = pc & 0xf00;
        const uint8_t to = fetch();
        if (cond) pc = uint16_t(page | to);
    }
    void add(uint8_t v, uint8_t carry);
    void da();
    void count();
    int  take_irq();
};

} // namespace i8035
