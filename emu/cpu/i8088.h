// Intel 8088 CPU interpreter (real mode, the 8086 instruction set), written
// for this project (MIT). Plain C++17, no allocation.
//
// Every 8086 opcode runs, with the 8088's documented flag results; flags the
// manuals leave undefined follow the real chip where that is simple
// (shifts and rotates step bit by bit, so OF is the last step's) and are
// otherwise left as they fall. The 8088 / 8086 details: a word at offset
// FFFF wraps to offset 0 of its segment, PUSH SP pushes the new SP, POP CS
// (0F) works, 60-6F are the conditional jumps again, C0 / C1 / C8 / C9 are
// RET / RETF again, 82 is 80, D6 is SALC, D0-D3 /6 is SETMO, 8C / 8E use
// the low two bits of reg, a divide error pushes the next instruction's
// address, IDIV's quotient range is -127..127 (-128 traps), a REP prefix
// before IDIV negates the quotient, FPU escapes (D8-DF) read their operand
// and do nothing, WAIT and LOCK are no-ops.
//
// Timing: the documented 8086 cycles for each instruction and addressing
// mode, plus 4 cycles for every 16-bit memory transfer (the 8088's 8-bit
// bus), and never less than 4 cycles a byte of the instruction (its
// fetches); returns and far / near jumps take what the real chip measures
// (the queue refills). The prefetch queue itself isn't modelled: against
// the SingleStepTests suite's measured cycles our counts come out within a
// few percent on average. REP string instructions run one element at a
// time against the run budget and resume (IP back at the prefixes) when it
// runs out, as the chip does when interrupted.
//
// Memory: a 20-bit bus masked by `addr_mask` (a board that decodes only
// A0-A15 sets 0xFFFF). Addresses below 64 KB go through a table of 256-byte
// pages (a pointer = direct access, null = the callbacks); the rest always
// through the callbacks. I/O ports through io_read / io_write.
#pragma once

#include <cstdint>

#include "hot.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "i8088: the byte registers assume a little-endian host"
#endif

namespace i8088 {

using read_fn  = uint8_t (*)(void* ctx, uint32_t addr);
using write_fn = void (*)(void* ctx, uint32_t addr, uint8_t value);
using in_fn    = uint8_t (*)(void* ctx, uint16_t port);
using out_fn   = void (*)(void* ctx, uint16_t port, uint8_t value);
using ack_fn   = uint8_t (*)(void* ctx);    // INTR acknowledge: the vector number

// 16-bit registers (the ModRM order) and segment registers.
enum Reg : uint8_t { AX, CX, DX, BX, SP, BP, SI, DI };
enum SegReg : uint8_t { ES, CS, SS, DS };

// Flag bits.
enum : uint16_t {
    CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080,
    TF = 0x0100, IF = 0x0200, DF = 0x0400, OF = 0x0800,
};

class Cpu {
public:
    static constexpr int kPageShift = 8;
    static constexpr int kPageSize  = 1 << kPageShift;
    static constexpr int kPages     = 0x10000 >> kPageShift;   // the low 64 KB

    Cpu();

    // --- wiring -----------------------------------------------------------
    const uint8_t* read_page[kPages];   // null = mem_read
    uint8_t*       write_page[kPages];  // null = mem_write
    void*    ctx      = nullptr;
    read_fn  mem_read = nullptr;        // unset: reads 0xFF, writes ignored
    write_fn mem_write = nullptr;
    in_fn    io_read  = nullptr;        // unset: reads 0xFF
    out_fn   io_write = nullptr;
    ack_fn   int_ack  = nullptr;        // unset: vector 0xFF
    uint32_t addr_mask = 0xfffff;

    void map_read(uint32_t start, uint32_t len, const uint8_t* p);
    void map_write(uint32_t start, uint32_t len, uint8_t* p);
    void map_ram(uint32_t start, uint32_t len, uint8_t* p)
    {
        map_read(start, len, p);
        map_write(start, len, p);
    }

    // --- control ----------------------------------------------------------
    // CS = FFFF, IP = 0, the other segments 0, flags clear.
    void reset();
    // Runs at least `cycles` cycles (finishing the instruction, except a REP
    // string instruction, which stops between elements) and returns the
    // cycles run. A halted CPU spends the whole budget. A callback may call
    // stop() to end the run after the current instruction (or element).
    EMU_HOT int run(int cycles);
    void stop() { target_ = 0; }
    // Runs exactly one instruction (a REP string instruction to its end; no
    // interrupt is taken) and returns its cycles. For tests and debuggers.
    int  step_instruction();
    // Cycles run so far in the current run() up to the start of the
    // instruction running now.
    int  elapsed() const { return done_; }

    void nmi() { nmi_pending_ = true; }             // edge
    void irq(bool level) { intr_ = level; }         // INTR level (IF masks it)
    bool halted() const { return halted_; }

    // --- registers --------------------------------------------------------
    uint16_t r[8] = {};         // AX CX DX BX SP BP SI DI
    uint16_t sreg[4] = {};      // ES CS SS DS
    uint16_t ip = 0;

    uint16_t flags() const;
    void     set_flags(uint16_t f);

    // Byte registers by ModRM number: AL CL DL BL AH CH DH BH.
    uint8_t& r8(int n) { return reinterpret_cast<uint8_t*>(&r[n & 3])[n >> 2]; }

    // Memory helpers (also for test harnesses and debuggers).
    uint8_t rd8(uint32_t a)
    {
        a &= addr_mask;
        if (a < 0x10000) {
            const uint8_t* p = read_page[a >> kPageShift];
            if (p) return p[a & (kPageSize - 1)];
        }
        return mem_read(ctx, a);
    }
    void wr8(uint32_t a, uint8_t v)
    {
        a &= addr_mask;
        if (a < 0x10000) {
            uint8_t* p = write_page[a >> kPageShift];
            if (p) { p[a & (kPageSize - 1)] = v; return; }
        }
        mem_write(ctx, a, v);
    }
    uint32_t phys(uint16_t seg, uint16_t off) const { return (uint32_t(seg) << 4) + off; }

    uint64_t instructions = 0;  // instructions run (statistics)

private:
    // Flags kept apart for speed; flags() packs them.
    uint8_t cf_ = 0, pf_ = 0, af_ = 0, zf_ = 0, sf_ = 0, of_ = 0;   // 0 / 1
    uint8_t tf_ = 0, if_ = 0, df_ = 0;

    bool nmi_pending_ = false, intr_ = false, halted_ = false;
    bool inhibit_ = false;      // MOV / POP SS: no interrupt before the next instruction
    int  done_ = 0, target_ = 0;
    int  cyc_ = 0;              // cycles of the instruction running now

    // Decode state of the current instruction.
    int      seg_ovr_ = -1;     // segment override, -1 = none
    uint8_t  rep_ = 0;          // 0, 0xF2 (REPNE) or 0xF3 (REP / REPE)
    uint16_t start_ip_ = 0;     // IP of the first prefix (REP resumes there)
    uint8_t  modrm_ = 0;
    uint16_t ea_seg_ = 0, ea_off_ = 0;   // memory operand (mod != 3)

    int      fetched_ = 0;          // instruction bytes fetched (the bus floor)
    uint8_t  fetch8() { const uint8_t v = rd8(phys(sreg[CS], ip)); ++ip; ++fetched_; return v; }
    uint16_t fetch16() { const uint8_t lo = fetch8(); return uint16_t(lo | fetch8() << 8); }

    uint8_t  mrd8(uint16_t seg, uint16_t off) { return rd8(phys(seg, off)); }
    void     mwr8(uint16_t seg, uint16_t off, uint8_t v) { wr8(phys(seg, off), v); }
    uint16_t mrd16(uint16_t seg, uint16_t off)
    {
        return uint16_t(rd8(phys(seg, off)) | rd8(phys(seg, uint16_t(off + 1))) << 8);
    }
    void mwr16(uint16_t seg, uint16_t off, uint16_t v)
    {
        wr8(phys(seg, off), uint8_t(v));
        wr8(phys(seg, uint16_t(off + 1)), uint8_t(v >> 8));
    }
    void push(uint16_t v)
    {
        r[SP] = uint16_t(r[SP] - 2);
        mwr16(sreg[SS], r[SP], v);
    }
    uint16_t pop()
    {
        const uint16_t v = mrd16(sreg[SS], r[SP]);
        r[SP] = uint16_t(r[SP] + 2);
        return v;
    }
    uint16_t dseg(int def) const { return sreg[seg_ovr_ >= 0 ? seg_ovr_ : def]; }

    // ModRM operands.
    void     modrm();               // reads the ModRM byte and the displacement
    bool     is_reg() const { return modrm_ >= 0xc0; }
    int      reg_field() const { return (modrm_ >> 3) & 7; }
    uint8_t  get_rm8() { return is_reg() ? r8(modrm_ & 7) : mrd8(ea_seg_, ea_off_); }
    uint16_t get_rm16() { return is_reg() ? r[modrm_ & 7] : mrd16(ea_seg_, ea_off_); }
    void     set_rm8(uint8_t v) { if (is_reg()) r8(modrm_ & 7) = v; else mwr8(ea_seg_, ea_off_, v); }
    void     set_rm16(uint16_t v) { if (is_reg()) r[modrm_ & 7] = v; else mwr16(ea_seg_, ea_off_, v); }

    // Flag helpers.
    void szp8(uint8_t v);
    void szp16(uint16_t v);
    template <int W> uint32_t alu(int op, uint32_t a, uint32_t b);   // ADD OR ADC SBB AND SUB XOR CMP
    template <int W> uint32_t inc(uint32_t a);
    template <int W> uint32_t dec(uint32_t a);
    template <int W> uint32_t shift(int op, uint32_t v, int count);
    template <int W> void     group3(uint8_t op);      // F6 / F7
    bool cond(int cc) const;

    void interrupt(uint8_t vector);
    bool string_op(uint8_t op);     // one element; false = stop (REPE / REPNE condition)
    void daa(), das(), aaa(), aas();
    EMU_HOT int step();
};

} // namespace i8088
