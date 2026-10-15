// Motorola 6800 / 6802 / 6808 CPU interpreter, written for this project
// (MIT). Plain C++17, no allocation. The three chips run the same
// instruction set; the 6802 adds 128 bytes of RAM at 0000-007F and the
// 6802 / 6808 divide their crystal by 4 - both are the machine's business
// (map the RAM, pass the E clock). Every documented opcode with the
// datasheet's cycle counts, IRQ (level), NMI (edge), SWI and WAI. The
// undocumented opcodes run as 2-cycle no-ops that skip their operand
// bytes. Hardware facts from the Motorola datasheet and MAME's m6800 core
// (read for facts only).
//
// Memory goes through a table of 1 KB pages like the other cores: a page
// with a pointer is read or written directly; a null page calls mem_read /
// mem_write (memory-mapped I/O).
#pragma once

#include <cstdint>

#include "hot.h"

namespace m6800 {

using read_fn  = uint8_t (*)(void* ctx, uint16_t addr);
using write_fn = void (*)(void* ctx, uint16_t addr, uint8_t value);

// Condition code bits (bits 6 and 7 read as 1).
enum : uint8_t {
    CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08, CC_I = 0x10, CC_H = 0x20,
};

enum : uint16_t { kVecIrq = 0xfff8, kVecSwi = 0xfffa, kVecNmi = 0xfffc, kVecReset = 0xfffe };

class Cpu {
public:
    static constexpr int kPageShift = 10;
    static constexpr int kPageSize  = 1 << kPageShift;
    static constexpr int kPages     = 0x10000 >> kPageShift;

    Cpu();

    // --- wiring -----------------------------------------------------------
    const uint8_t* read_page[kPages];
    uint8_t*       write_page[kPages];
    void*    ctx       = nullptr;
    read_fn  mem_read  = nullptr;
    write_fn mem_write = nullptr;

    void map_read(uint16_t start, uint32_t len, const uint8_t* p);
    void map_write(uint16_t start, uint32_t len, uint8_t* p);
    void map_ram(uint16_t start, uint32_t len, uint8_t* p)
    {
        map_read(start, len, p);
        map_write(start, len, p);
    }

    // --- control ----------------------------------------------------------
    void reset();                       // sets I, loads PC from FFFE
    // Runs at least `cycles` cycles (finishing the last instruction);
    // returns the cycles executed. stop() from a callback ends the run.
    int  run(int cycles);
    void stop() { target_ = 0; }
    int  elapsed() const { return done_; }

    void irq(bool level) { irq_line_ = level; }
    void nmi() { nmi_pending_ = true; }
    bool irq_line() const { return irq_line_; }

    // --- registers ---------------------------------------------------------
    uint8_t  a = 0, b = 0, cc = 0xc0 | CC_I;
    uint16_t x = 0, s = 0, pc = 0;
    bool     wai = false;               // waiting in WAI (state pushed)

    uint8_t rd(uint16_t ad)
    {
        const uint8_t* p = read_page[ad >> kPageShift];
        return p ? p[ad & (kPageSize - 1)] : mem_read(ctx, ad);
    }
    void wr(uint16_t ad, uint8_t v)
    {
        uint8_t* p = write_page[ad >> kPageShift];
        if (p) p[ad & (kPageSize - 1)] = v;
        else   mem_write(ctx, ad, v);
    }
    uint16_t rd16(uint16_t ad) { return uint16_t((rd(ad) << 8) | rd(uint16_t(ad + 1))); }
    void wr16(uint16_t ad, uint16_t v)
    {
        wr(ad, uint8_t(v >> 8));
        wr(uint16_t(ad + 1), uint8_t(v));
    }

private:
    bool irq_line_ = false, nmi_pending_ = false;
    int  done_ = 0, target_ = 0;

    uint8_t  fetch() { return rd(pc++); }
    uint16_t fetch16()
    {
        const uint16_t v = rd16(pc);
        pc = uint16_t(pc + 2);
        return v;
    }
    void push8(uint8_t v) { wr(s, v); s = uint16_t(s - 1); }
    uint8_t pull8() { s = uint16_t(s + 1); return rd(s); }
    void push16(uint16_t v) { push8(uint8_t(v)); push8(uint8_t(v >> 8)); }
    uint16_t pull16() { const uint8_t h = pull8(); return uint16_t((h << 8) | pull8()); }
    void push_state();                  // PC X A B CC
    void interrupt(uint16_t vec);

    void exec(uint8_t op);

    void    nz(uint8_t v) { cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z)); }
    void    nz16(uint16_t v) { cc = uint8_t((cc & ~(CC_N | CC_Z | CC_V)) | (v & 0x8000 ? CC_N : 0) | (v ? 0 : CC_Z)); }
    uint8_t op_sub(uint8_t x1, uint8_t x2, int carry);
    uint8_t op_add(uint8_t x1, uint8_t x2, int carry);
    uint8_t rmw(uint8_t op, uint8_t v);
    void    shift_v() { const bool n = cc & CC_N, c = cc & CC_C; cc = uint8_t((cc & ~CC_V) | (n != c ? CC_V : 0)); }
    bool    cond(uint8_t op) const;
};

} // namespace m6800
