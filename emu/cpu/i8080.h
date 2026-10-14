// Intel 8080 CPU interpreter, written for this project (MIT).
// Plain C++17, no allocation. All 256 opcodes including the undocumented
// duplicates (08-38 NOP, CB JMP, D9 RET, DD / ED / FD CALL), 8080 flags (P
// is parity for arithmetic too, AC as the 8080 sets it: ANA = bit 3 of the
// OR of the operands, subtraction = carry of the complement add; bits 1,
// 3, 5 of F read 1, 0, 0), instruction-level state timing, INTE with the
// one-instruction EI delay, and HLT.
//
// Same wiring as the Z80 core (emu/cpu/z80.h): memory through a table of
// 1 KB pages, a null page calls mem_read / mem_write; I/O through port_in /
// port_out. The 8080 puts the 8-bit port number on both halves of the
// address bus, so callbacks get it as (n << 8) | n.
//
// Interrupts: the machine raises the INT line with the instruction its bus
// logic supplies during the acknowledge cycle - an RST n opcode, as on
// every board we emulate (others are taken as RST 7). The CPU calls
// irq_ack (when set) as it accepts one, where a board clears its request
// flip-flop.
#pragma once

#include <cstdint>

#include "hot.h"

namespace i8080 {

using read_fn  = uint8_t (*)(void* ctx, uint16_t addr);
using write_fn = void (*)(void* ctx, uint16_t addr, uint8_t value);
using ack_fn   = void (*)(void* ctx);

// A register pair; .b.l / .b.h are its low / high halves (little-endian
// hosts only, checked in i8080.cpp). PSW is af: A = af.b.h, flags = af.b.l.
union Pair {
    uint16_t w;
    struct { uint8_t l, h; } b;
};

// Flag bits. Bit 1 always reads 1, bits 3 and 5 always read 0.
enum : uint8_t {
    CF = 0x01, F1 = 0x02, PF = 0x04, AC = 0x10, ZF = 0x40, SF = 0x80,
};

class Cpu {
public:
    static constexpr int kPageShift = 10;            // 1 KB pages
    static constexpr int kPageSize  = 1 << kPageShift;
    static constexpr int kPages     = 0x10000 >> kPageShift;

    Cpu();

    // --- wiring -----------------------------------------------------------
    const uint8_t* read_page[kPages];   // null = mem_read
    uint8_t*       write_page[kPages];  // null = mem_write
    void*    ctx       = nullptr;       // handed to every callback
    read_fn  mem_read  = nullptr;       // unset callbacks read 0xFF / ignore
    write_fn mem_write = nullptr;
    read_fn  port_in   = nullptr;
    write_fn port_out  = nullptr;
    ack_fn   irq_ack   = nullptr;       // optional: called on interrupt acknowledge

    void map_read(uint16_t start, uint32_t len, const uint8_t* p);
    void map_write(uint16_t start, uint32_t len, uint8_t* p);
    void map_ram(uint16_t start, uint32_t len, uint8_t* p)
    {
        map_read(start, len, p);
        map_write(start, len, p);
    }

    // --- control ----------------------------------------------------------
    void reset();
    // Runs at least `cycles` states (finishing the last instruction) and
    // returns the states executed. A callback may call stop() to end the
    // run after the current instruction.
    int  run(int cycles);
    void stop() { target_ = 0; }
    int  elapsed() const { return done_; }

    // Level-triggered INT; `opcode` is what the board puts on the bus
    // during the acknowledge (RST n = 0xC7 | n << 3).
    void irq(bool level, uint8_t opcode) { irq_line_ = level; irq_opcode_ = opcode; }
    void irq(bool level) { irq_line_ = level; }
    bool irq_line() const { return irq_line_; }

    // --- registers (public for tests and debuggers) -----------------------
    Pair af, bc, de, hl, sp, pc;
    uint8_t inte = 0;           // interrupt enable flip-flop
    bool    halted = false;

    uint8_t rd(uint16_t a)
    {
        const uint8_t* p = read_page[a >> kPageShift];
        return p ? p[a & (kPageSize - 1)] : mem_read(ctx, a);
    }
    void wr(uint16_t a, uint8_t v)
    {
        uint8_t* p = write_page[a >> kPageShift];
        if (p) p[a & (kPageSize - 1)] = v;
        else   mem_write(ctx, a, v);
    }

private:
    bool    irq_line_ = false;
    uint8_t irq_opcode_ = 0xff;
    bool    ei_delay_ = false;       // no interrupt right after EI
    int     done_ = 0, target_ = 0;

    uint8_t  fetch() { return rd(pc.w++); }
    uint16_t fetch16()
    {
        const uint8_t lo = rd(pc.w++);
        return uint16_t(lo | (rd(pc.w++) << 8));
    }
    uint16_t rd16(uint16_t a) { return uint16_t(rd(a) | (rd(uint16_t(a + 1)) << 8)); }
    void wr16(uint16_t a, uint16_t v)
    {
        wr(a, uint8_t(v));
        wr(uint16_t(a + 1), uint8_t(v >> 8));
    }
    void push(uint16_t v)
    {
        sp.w = uint16_t(sp.w - 2);
        wr16(sp.w, v);
    }
    uint16_t pop()
    {
        const uint16_t v = rd16(sp.w);
        sp.w = uint16_t(sp.w + 2);
        return v;
    }

    // ALU helpers (set F).
    void add8(uint8_t v, uint8_t carry);
    void sub8(uint8_t v, uint8_t borrow);   // also CMP (result dropped by the caller)
    void ana8(uint8_t v);
    void xra8(uint8_t v);
    void ora8(uint8_t v);
    void cmp8(uint8_t v);
    uint8_t inr8(uint8_t v);
    uint8_t dcr8(uint8_t v);
    void daa();

    int  take_irq();
    int  exec(uint8_t op);
};

} // namespace i8080
