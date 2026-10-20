// MOS 6502 CPU interpreter (NMOS), written for this project (MIT).
// Plain C++17, no allocation. All documented opcodes with the NMOS details:
// decimal mode ADC / SBC with the NMOS N / V / Z flags (from the binary
// sum for SBC, from the half-corrected sum for ADC), the JMP ($xxFF) page
// wrap, zero-page index wrap, instruction-level cycle timing (page-cross
// and branch penalties), level IRQ with the one-instruction delay after
// CLI / PLP, edge NMI, BRK. Undocumented opcodes run as NOPs of their
// length and are counted (illegal_count) - no game here needs them yet.
//
// Memory goes through a table of 256-byte pages (6502 boards bank and map
// I/O at that grain): a page with a pointer is read or written directly; a
// null page calls mem_read / mem_write.
//
// opcode_hook (optional): called on every opcode fetch with the address,
// the byte read and whether anything was written since the last opcode
// fetch; it returns the opcode to run. For CPUs that decrypt opcodes (Data
// East's DECO CPU-7 in Burger Time swaps bits when the previous
// instruction wrote).
//
// indexed_io (set by opcode_hook): the (zp,X) instruction just fetched makes
// its data access through mem_read / mem_write whatever the page table says
// (Missile Command's MADSEL: the board sends that access to its pixel
// logic). stall() adds wait states to the instruction running.
#pragma once

#include <cstdint>

#include "hot.h"

// The memory helpers must inline into the interpreter's loop.
#define M6502_INLINE inline __attribute__((always_inline))

namespace m6502 {

using read_fn   = uint8_t (*)(void* ctx, uint16_t addr);
using write_fn  = void (*)(void* ctx, uint16_t addr, uint8_t value);
using opcode_fn = uint8_t (*)(void* ctx, uint16_t addr, uint8_t op, bool wrote);

// Status register bits.
enum : uint8_t {
    CF = 0x01, ZF = 0x02, IF = 0x04, DF = 0x08, BF = 0x10, UF = 0x20, VF = 0x40, NF = 0x80,
};

class Cpu {
public:
    static constexpr int kPageShift = 8;             // 256-byte pages
    static constexpr int kPageSize  = 1 << kPageShift;
    static constexpr int kPages     = 0x10000 >> kPageShift;

    Cpu();

    // --- wiring -----------------------------------------------------------
    const uint8_t* read_page[kPages];   // null = mem_read
    uint8_t*       write_page[kPages];  // null = mem_write
    void*     ctx         = nullptr;    // handed to every callback
    read_fn   mem_read    = nullptr;    // unset callbacks read 0xFF / ignore
    write_fn  mem_write   = nullptr;
    opcode_fn opcode_hook = nullptr;

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
    // Loads PC from the reset vector (FFFC), sets I, S = FD.
    void reset();
    // Runs at least `cycles` cycles (finishing the last instruction) and
    // returns the cycles executed. A callback may call stop() to end the
    // run after the current instruction.
    int  run(int cycles);
    void stop() { target_ = 0; }
    // Cycles executed so far in the current run(), up to the start of the
    // instruction running now (for callbacks that need a time stamp).
    int  elapsed() const { return done_; }

    void irq(bool level) { irq_line_ = level; check_ = level || nmi_pending_; }   // level-triggered
    bool irq_line() const { return irq_line_; }
    void nmi() { nmi_pending_ = true; check_ = true; }  // edge: taken once
    // From a callback: the instruction running takes `n` more cycles.
    void stall(int n) { done_ += n; }

    // Set by opcode_hook for a (zp,X) opcode: its data access goes to the
    // callbacks; cleared by that access.
    bool indexed_io = false;

    // --- registers (public for tests and debuggers) -----------------------
    uint8_t  a = 0, x = 0, y = 0, s = 0xfd;
    uint16_t pc = 0;
    uint32_t illegal_count = 0;         // undocumented opcodes run (as NOPs)

    uint8_t p() const
    {
        return uint8_t((n_ & NF) | (v_ ? VF : 0) | UF | (d_ ? DF : 0) | (i_ ? IF : 0) | (z_ ? 0 : ZF) | c_);
    }
    void set_p(uint8_t v)
    {
        n_ = v & NF;
        v_ = (v & VF) != 0;
        d_ = (v & DF) != 0;
        i_ = (v & IF) != 0;
        z_ = (v & ZF) ? 0 : 1;
        c_ = v & CF;
    }

    // Memory helpers (also handy for test harnesses).
    M6502_INLINE uint8_t rd(uint16_t a)
    {
        const uint8_t* p = read_page[a >> kPageShift];
        return p ? p[a & (kPageSize - 1)] : mem_read(ctx, a);
    }
    M6502_INLINE void wr(uint16_t a, uint8_t v)
    {
        wrote_ = true;
        uint8_t* p = write_page[a >> kPageShift];
        if (p) p[a & (kPageSize - 1)] = v;
        else   mem_write(ctx, a, v);
    }

private:
    M6502_INLINE uint8_t rd_indx(uint16_t a)
    {
        if (!indexed_io) return rd(a);
        indexed_io = false;
        return mem_read(ctx, a);
    }
    M6502_INLINE void wr_indx(uint16_t a, uint8_t v)
    {
        if (!indexed_io) return wr(a, v);
        indexed_io = false;
        wrote_ = true;
        mem_write(ctx, a, v);
    }

    // Flags kept apart: N = bit 7 of n_, Z = (z_ == 0), C = c_ (0 / 1).
    uint8_t n_ = 0, z_ = 1, c_ = 0;
    bool    v_ = false, d_ = false, i_ = true;

    bool    irq_line_ = false;
    bool    nmi_pending_ = false;
    bool    irq_delay_ = false;     // the I flag just cleared: one more instruction first
    bool    check_ = false;         // look at the interrupt lines before the next instruction
    bool    wrote_ = false;         // a write since the last opcode fetch
    int     done_ = 0, target_ = 0;

    M6502_INLINE uint8_t  fetch() { return rd(pc++); }
    M6502_INLINE uint16_t fetch16()
    {
        const uint8_t lo = rd(pc++);
        return uint16_t(lo | (rd(pc++) << 8));
    }
    M6502_INLINE void push(uint8_t v) { wr(uint16_t(0x100 | s--), v); }
    M6502_INLINE uint8_t pull() { return rd(uint16_t(0x100 | ++s)); }

    void interrupt(uint16_t vector, bool brk);
    void adc(uint8_t m);
    void sbc(uint8_t m);
    void cmp(uint8_t r, uint8_t m)
    {
        const unsigned d = unsigned(r) - m;
        c_ = d < 0x100;
        n_ = z_ = uint8_t(d);
    }
    uint8_t asl(uint8_t v) { c_ = v >> 7; v = uint8_t(v << 1); n_ = z_ = v; return v; }
    uint8_t lsr(uint8_t v) { c_ = v & 1; v >>= 1; n_ = z_ = v; return v; }
    uint8_t rol(uint8_t v) { const uint8_t r = uint8_t(v << 1 | c_); c_ = v >> 7; n_ = z_ = r; return r; }
    uint8_t ror(uint8_t v) { const uint8_t r = uint8_t(v >> 1 | c_ << 7); c_ = v & 1; n_ = z_ = r; return r; }
};

} // namespace m6502
