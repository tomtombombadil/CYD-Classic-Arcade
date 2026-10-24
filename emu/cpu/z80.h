// Z80 CPU interpreter, written for this project (MIT).
// Plain C++17, no allocation. Runs all documented and undocumented opcodes
// (IX/IY halves, DD CB / FD CB with register copies, SLL, ED mirrors) with
// the undocumented X/Y flags, instruction-level T-state timing, IM 0/1/2,
// NMI, HALT and the EI delay. MEMPTR (WZ) is not modelled, so the X/Y flags
// of BIT n,(HL) are approximate (nothing known depends on them).
//
// Memory goes through a table of 1 KB pages: a page with a pointer is read
// or written directly; a null page calls mem_read / mem_write (memory-mapped
// I/O). Port I/O always goes through port_in / port_out with the full
// 16-bit port address (for IN A,(n) the high byte is A, as on the chip).
#pragma once

#include <cstdint>

#include "hot.h"

// The interpreter's small helpers (memory, fetch, stack, ALU) must be inlined
// into the big decoder; left to itself GCC called fetch16 / wr16 / cp8 ...
// out of line there (~20 % of Galaga's emulation time on the PC).
#define Z80_INLINE inline __attribute__((always_inline))

namespace z80 {

using read_fn  = uint8_t (*)(void* ctx, uint16_t addr);
using write_fn = void (*)(void* ctx, uint16_t addr, uint8_t value);

// A register pair; .b.l / .b.h are its low / high halves (both targets are
// little-endian, checked in z80.cpp).
union Pair {
    uint16_t w;
    struct { uint8_t l, h; } b;
};

// Flag bits.
enum : uint8_t {
    CF = 0x01, NF = 0x02, PF = 0x04, XF = 0x08,
    HF = 0x10, YF = 0x20, ZF = 0x40, SF = 0x80,
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
    void reset();
    // Runs at least `cycles` T-states (finishing the last instruction) and
    // returns the T-states executed. A callback may call stop() to end the
    // run after the current instruction.
    int  run(int cycles);
    void stop() { target_ = 0; }
    // T-states executed so far in the current run() (for callbacks that
    // need a time stamp inside the slice).
    int  elapsed() const { return done_; }

    // Level-triggered maskable interrupt; `vector` is the byte the device
    // puts on the bus (IM 2 low address byte, IM 0 instruction - RST only).
    void irq(bool level, uint8_t vector) { irq_line_ = level; irq_vector_ = vector; }
    void irq(bool level) { irq_line_ = level; }
    void set_irq_vector(uint8_t vector) { irq_vector_ = vector; }
    bool irq_line() const { return irq_line_; }
    void nmi() { nmi_pending_ = true; }   // edge: taken once

    // --- registers (public for tests and debuggers) -----------------------
    Pair af, bc, de, hl, ix, iy, sp, pc;
    Pair af2, bc2, de2, hl2;
    uint8_t i = 0;
    uint8_t r = 0;          // counts every M1; bit 7 comes from r7
    uint8_t r7 = 0;         // bit 7 of R as last loaded by LD R,A
    uint8_t iff1 = 0, iff2 = 0, im = 0;
    bool    halted = false;

    uint8_t r_value() const { return uint8_t((r & 0x7f) | r7); }

    // Memory helpers (also handy for test harnesses).
    Z80_INLINE uint8_t rd(uint16_t a)
    {
        const uint8_t* p = read_page[a >> kPageShift];
        return p ? p[a & (kPageSize - 1)] : mem_read(ctx, a);
    }
    Z80_INLINE void wr(uint16_t a, uint8_t v)
    {
        uint8_t* p = write_page[a >> kPageShift];
        if (p) p[a & (kPageSize - 1)] = v;
        else   mem_write(ctx, a, v);
    }

private:
    bool    irq_line_ = false;
    uint8_t irq_vector_ = 0xff;
    bool    nmi_pending_ = false;
    bool    ei_delay_ = false;       // no interrupt right after EI
    bool    use_iy_ = false;         // exec_index: FD (IY), else DD (IX)
    int     done_ = 0, target_ = 0;

    Z80_INLINE uint8_t fetch() { return rd(pc.w++); }
    Z80_INLINE uint16_t fetch16()
    {
        const uint8_t lo = rd(pc.w++);
        return uint16_t(lo | (rd(pc.w++) << 8));
    }
    Z80_INLINE uint16_t rd16(uint16_t a) { return uint16_t(rd(a) | (rd(uint16_t(a + 1)) << 8)); }
    Z80_INLINE void wr16(uint16_t a, uint16_t v)
    {
        wr(a, uint8_t(v));
        wr(uint16_t(a + 1), uint8_t(v >> 8));
    }
    Z80_INLINE void push(uint16_t v)
    {
        sp.w = uint16_t(sp.w - 2);
        wr16(sp.w, v);
    }
    Z80_INLINE uint16_t pop()
    {
        const uint16_t v = rd16(sp.w);
        sp.w = uint16_t(sp.w + 2);
        return v;
    }
    uint8_t in(uint16_t port) { return port_in(ctx, port); }
    void out(uint16_t port, uint8_t v) { port_out(ctx, port, v); }

    // ALU helpers (set F).
    void add8(uint8_t v);
    void adc8(uint8_t v);
    void sub8(uint8_t v);
    void sbc8(uint8_t v);
    void and8(uint8_t v);
    void xor8(uint8_t v);
    void or8(uint8_t v);
    void cp8(uint8_t v);
    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    void add16(Pair& d, uint16_t v);
    void adc16(uint16_t v);
    void sbc16(uint16_t v);
    uint8_t shift_op(int kind, uint8_t v);
    void bit_op(int n, uint8_t v, uint8_t xy_src);
    void daa();

    uint8_t& reg8(int z);           // B C D E H L - A (z != 6)
    int  take_irq();
    template <int IDX> int exec(uint8_t op);    // inlined into run() (IDX 0) and exec_index (1)
    int  exec_index(uint8_t op);                // after DD / FD (use_iy_ set for FD)
    int  exec_cb(uint8_t op);
    int  exec_index_cb(uint16_t base);
    int  exec_ed(uint8_t op, int pre);         // pre: T-states of a DD / FD prefix
    int  block_copy(uint8_t op, int spent);
};

} // namespace z80
