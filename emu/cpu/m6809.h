// Motorola 6809 CPU interpreter, written for this project (MIT).
// Plain C++17, no allocation. Runs every documented opcode with the
// datasheet's cycle counts (indexed modes add their extra cycles), the
// three interrupt lines (NMI edge, FIRQ and IRQ level), SWI / SWI2 / SWI3,
// CWAI and SYNC. The undocumented opcodes do what the chip is known to do
// with them (they're aliases of documented ones: 01 = NEG, 05 = LSR,
// 0B = DEC that also sets C, 4E / 5E = CLR, 38 = ANDCC, 3E = a reset
// through the FFFE vector, 02 = NEG or COM by the carry, unknown page 2 /
// page 3 codes run as page 1 codes); 14, 15 and CD stop the CPU until a
// reset ("halt and catch fire"). NMI is ignored until S has been loaded,
// as on the chip. Hardware facts from the Motorola datasheet and MAME's
// m6809 core (read for facts only).
//
// Memory goes through a table of 1 KB pages: a page with a pointer is read
// or written directly; a null page calls mem_read / mem_write (memory-mapped
// I/O). The 6809 has no I/O ports.
#pragma once

#include <cstdint>

#include "hot.h"

namespace m6809 {

using read_fn  = uint8_t (*)(void* ctx, uint16_t addr);
using write_fn = void (*)(void* ctx, uint16_t addr, uint8_t value);

// Condition code bits.
enum : uint8_t {
    CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08,
    CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80,
};

// Interrupt vectors.
enum : uint16_t {
    kVecSwi3 = 0xfff2, kVecSwi2 = 0xfff4, kVecFirq = 0xfff6, kVecIrq = 0xfff8,
    kVecSwi = 0xfffa, kVecNmi = 0xfffc, kVecReset = 0xfffe,
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

    // Maps [start, start + len) (page aligned) to `p` for reads, writes or
    // both; null unmaps (back to the callbacks).
    void map_read(uint16_t start, uint32_t len, const uint8_t* p);
    void map_write(uint16_t start, uint32_t len, uint8_t* p);
    void map_ram(uint16_t start, uint32_t len, uint8_t* p)
    {
        map_read(start, len, p);
        map_write(start, len, p);
    }

    // --- control ----------------------------------------------------------
    // Sets I and F, clears the waits and loads PC from FFFE.
    void reset();
    // Runs at least `cycles` cycles (finishing the last instruction) and
    // returns the cycles executed. A callback may call stop() to end the
    // run after the current instruction.
    int  run(int cycles);
    void stop() { target_ = 0; }
    // Cycles executed so far in the current run().
    int  elapsed() const { return done_; }
    // A device held the bus (e.g. a blitter halting the CPU): counts these
    // cycles into the current instruction.
    void add_cycles(int n) { done_ += n; }

    void irq(bool level) { lines_ = uint8_t(level ? lines_ | kIrq : lines_ & ~kIrq); }
    void firq(bool level) { lines_ = uint8_t(level ? lines_ | kFirq : lines_ & ~kFirq); }
    void nmi() { lines_ |= kNmi; }          // edge: taken once (after the first LDS)
    bool irq_line() const { return lines_ & kIrq; }
    bool firq_line() const { return lines_ & kFirq; }

    // Waiting for an interrupt in CWAI (state pushed) or SYNC; stopped by
    // 14 / 15 / CD until a reset.
    bool in_cwai() const { return wait_ & kCwai; }
    bool in_sync() const { return wait_ & kSync; }
    bool halted() const { return wait_ & kHalted; }

    // --- registers (public for tests and debuggers) -----------------------
    union {
        uint16_t d;
        struct { uint8_t b, a; } r;          // little-endian host (checked in m6809.cpp)
    };
    uint16_t x = 0, y = 0, u = 0, s = 0, pc = 0;
    uint8_t  dp = 0, cc = 0;

    uint8_t rd(uint16_t a)
    {
        const uint8_t* p = read_page[a >> kPageShift];
        return p ? p[a & (kPageSize - 1)] : mem_read(ctx, a);
    }
    // Memory writes so far (any address): a machine looking for idle loops
    // checks that none happened in one.
    uint32_t writes = 0;

    void wr(uint16_t a, uint8_t v)
    {
        ++writes;
        uint8_t* p = write_page[a >> kPageShift];
        if (p) p[a & (kPageSize - 1)] = v;
        else   mem_write(ctx, a, v);
    }
    uint16_t rd16(uint16_t a) { return uint16_t((rd(a) << 8) | rd(uint16_t(a + 1))); }
    void wr16(uint16_t a, uint16_t v)
    {
        wr(a, uint8_t(v >> 8));
        wr(uint16_t(a + 1), uint8_t(v));
    }

private:
    enum : uint8_t { kIrq = 1, kFirq = 2, kNmi = 4 };           // lines_
    enum : uint8_t { kCwai = 1, kSync = 2, kHalted = 4 };       // wait_
    uint8_t lines_ = 0, wait_ = 0;
    bool nmi_armed_ = false;        // set by the first load of S
    int  done_ = 0, target_ = 0;

    uint8_t  fetch() { return rd(pc++); }
    uint16_t fetch16()
    {
        const uint16_t v = rd16(pc);
        pc = uint16_t(pc + 2);
        return v;
    }
    uint16_t ea_direct() { return uint16_t((dp << 8) | fetch()); }
    uint16_t ea_indexed();          // adds its extra cycles to done_

    void push8s(uint8_t v) { wr(--s, v); }
    void push16s(uint16_t v) { wr(--s, uint8_t(v)); wr(--s, uint8_t(v >> 8)); }
    void push_entire();             // PC U Y X DP B A CC onto S
    void pull_regs(uint16_t& sp, uint8_t mask, bool from_s);
    void push_regs(uint16_t& sp, uint8_t mask, bool to_s);
    bool interrupt();               // takes a pending interrupt; true if one was taken

    uint16_t tfr_read(int reg, bool cc_doubled) const;
    void     tfr_write(int reg, uint16_t v);

    void page2();
    void page3();
    void exec(uint8_t op);          // one page 1 opcode (its fetch already done)
    inline void exec_inline(uint8_t op) __attribute__((always_inline));

    // 8-bit operations (flags into cc)
    uint8_t op_neg(uint8_t v);
    uint8_t op_com(uint8_t v);
    uint8_t op_lsr(uint8_t v);
    uint8_t op_ror(uint8_t v);
    uint8_t op_asr(uint8_t v);
    uint8_t op_asl(uint8_t v);
    uint8_t op_rol(uint8_t v);
    uint8_t op_dec(uint8_t v);
    uint8_t op_inc(uint8_t v);
    void    op_tst(uint8_t v);
    uint8_t op_sub(uint8_t a, uint8_t b, int carry);
    uint8_t op_add(uint8_t a, uint8_t b, int carry);
    void    nz8(uint8_t v) { cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z)); }
    void    nz16(uint16_t v) { cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (v & 0x8000 ? CC_N : 0) | (v ? 0 : CC_Z)); }
    uint16_t op_sub16(uint16_t a, uint16_t b);
    uint16_t op_add16(uint16_t a, uint16_t b);
    uint8_t  rmw(uint8_t op, uint8_t v);     // op & 0x0F of the 0x00-0x7F block
};

} // namespace m6809
