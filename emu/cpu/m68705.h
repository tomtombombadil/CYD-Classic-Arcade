// Motorola MC68705P3 microcontroller (the HMOS 6805 family) interpreter,
// written for this project (MIT). Plain C++17, no allocation.
//
// Hardware facts from Motorola's MC68705P3 / M6805 family data (and MAME's
// m6805 / m68705 cores, read for facts only):
// - Registers A, X (8 bits), PC (11 bits on the P parts), SP (0x60-0x7F:
//   a push stores at SP and decrements, wrapping 0x60 -> 0x7F), CC =
//   111HINZC.
// - The address space is 0x000-0x7FF: 0x00 / 0x01 / 0x02 ports A / B / C
//   (C four bits wide: its top bits read 1), 0x04-0x06 their data
//   direction registers (write only, read 0xFF), 0x08 the timer's data
//   register (TDR), 0x09 its control register (TCR), 0x0B the EPROM
//   programming register (no programming here: reads 0xFF), 0x10-0x7F
//   RAM, 0x80-0x7FF the user EPROM / bootstrap / vectors (a dump of the
//   whole 2 KB space is what's loaded), vectors at 0x7F8 timer, 0x7FA
//   /INT, 0x7FC SWI, 0x7FE reset (big-endian).
// - A port pin reads its latch where its DDR bit is 1, else the pin
//   (input). Port A has pull-ups (open drain): it drives latch | ~DDR.
// - The timer: an 8-bit down counter (TDR) clocked through a 7-bit
//   prescaler (divide by 2^TCR0-2) from the internal clock (the machine
//   cycle) when TCR bits 5-4 (TIN, TIE) are 00 (01: the clock gated by the
//   TIMER pin - held high here, so the same; 10: off; 11: the TIMER pin's
//   edges - none here). Counting down through zero sets TCR bit 7 (TIR);
//   the timer interrupt is pending while TIR = 1 and TCR bit 6 (TIM) = 0.
//   Reset: TDR FF, TCR 7F, prescaler 7F; the source / divisor keep the
//   values they had (power on: internal clock / 128), as MAME's model. The
//   MOR (0x784) can fix the timer's setup (its bit 6); this program's MOR
//   leaves it programmable, and that's all that is modelled.
// - /INT is edge sensitive: its falling edge (set_int(true)) latches a
//   request, taken when CC.I = 0 (before the timer's); BIL / BIH test the
//   pin's level. An interrupt pushes PC, X, A, CC (11 cycles) and sets I.
// - Times are in machine cycles (the crystal / 4), the HMOS table.
// Undocumented opcodes run as 2-cycle, 1-byte no-ops and are counted.
#pragma once

#include <cstdint>

namespace m68705 {

// Condition code bits (bits 5-7 read as 1).
enum : uint8_t { CC_C = 0x01, CC_Z = 0x02, CC_N = 0x04, CC_I = 0x08, CC_H = 0x10 };

enum : uint16_t { kVecTimer = 0x7f8, kVecInt = 0x7fa, kVecSwi = 0x7fc, kVecReset = 0x7fe };

class Cpu {
public:
    // The pins: port_in() gives a port's input pins (0 = A, 1 = B, 2 = C);
    // port_out() is called after a write to a port's latch or DDR with the
    // latch and the DDR (the machine decides what undriven pins show).
    using port_in_fn  = uint8_t (*)(void* ctx, int port);
    using port_out_fn = void (*)(void* ctx, int port, uint8_t latch, uint8_t ddr);

    void*       ctx      = nullptr;
    port_in_fn  port_in  = nullptr;     // unset: inputs read 0xFF
    port_out_fn port_out = nullptr;

    // `rom`: a dump of the whole 2 KB address space (only 0x80-0x7FF is
    // used); kept by pointer.
    void init(const uint8_t* rom);
    void reset();

    // Runs at least `cycles` machine cycles (finishing the last
    // instruction); returns the cycles run. stop() from a callback ends the
    // run after the current instruction.
    int  run(int cycles);
    void stop() { target_ = 0; }
    int  elapsed() const { return done_; }

    // The /INT pin: true = asserted (low). Its assertion latches a request.
    void set_int(bool asserted)
    {
        if (asserted && !int_pin_) pending_ |= kPendInt;
        int_pin_ = asserted;
    }
    bool int_pin() const { return int_pin_; }

    // --- registers (public for tests and debuggers) ------------------------
    uint8_t  a = 0, x = 0, cc = 0xe0 | CC_I, sp = 0x7f;
    uint16_t pc = 0;
    uint32_t illegal_count = 0;

    // The ports' latches and DDRs, the timer.
    uint8_t  latch[3] = {0xff, 0xff, 0xff};
    uint8_t  ddr[3] = {0, 0, 0};
    uint8_t  tdr = 0xff, tcr = 0x7f, prescale = 0x7f;
    uint8_t  ram[0x70] = {};            // 0x10-0x7F

    // Memory as the CPU sees it (registers included); for tests too.
    uint8_t rd(uint16_t a);
    void    wr(uint16_t a, uint8_t v);

private:
    static constexpr uint8_t kPendInt = 1, kPendTimer = 2;

    const uint8_t* rom_ = nullptr;
    bool    int_pin_ = false;
    uint8_t pending_ = 0;
    // The timer's source (TCR bits 5-4 when last written; 0 at power on)
    // and divisor (TCR bits 2-0; 7 at power on).
    uint8_t tsrc_ = 0, tdiv_ = 7;
    int     done_ = 0, target_ = 0;

    uint8_t port_read(int p);
    void    tcr_write(uint8_t v);
public:
    // The timer counts `cycles` machine cycles (run() does this after each
    // instruction; a machine skipping an idle loop calls it for the loop's
    // time). The prescaler is a free-running 7-bit counter: the counter
    // steps each time its divisor's bit carries out.
    void    timer(int cycles)
    {
        if (tsrc_ >= 2) return;                         // off, or the TIMER pin (no edges)
        const unsigned dec = ((prescale & ((1u << tdiv_) - 1)) + unsigned(cycles)) >> tdiv_;
        prescale = uint8_t((prescale + unsigned(cycles)) & 0x7f);
        if (!dec) return;
        const bool under = (tdr ? unsigned(tdr) : 256u) <= dec;
        tdr = uint8_t(tdr - dec);
        if (under) {
            tcr |= 0x80;
            if (!(tcr & 0x40)) pending_ |= kPendTimer;
        }
    }
    // Machine cycles until the timer next asks for an interrupt (sets a
    // pending request), or -1 if it can't (stopped, or masked).
    int     cycles_to_timer_irq() const
    {
        if (tsrc_ >= 2 || (tcr & 0x40)) return -1;
        const unsigned t = tdr ? tdr : 256u;
        return int((t << tdiv_) - (prescale & ((1u << tdiv_) - 1)));
    }
    // An interrupt would be taken before the next instruction.
    bool    irq_due() const { return (pending_ & (kPendInt | kPendTimer)) && !(cc & CC_I); }
    bool    timer_pending() const { return pending_ & kPendTimer; }

private:
    void    push(uint8_t v) { wr(sp, v); sp = sp == 0x60 ? 0x7f : uint8_t(sp - 1); }
    uint8_t pull() { sp = sp == 0x7f ? 0x60 : uint8_t(sp + 1); return rd(sp); }
    uint16_t vec(uint16_t at) { return uint16_t(((rd(at) << 8) | rd(uint16_t(at + 1))) & 0x7ff); }
    int  interrupt();
    void nz(uint8_t v) { cc = uint8_t((cc & ~(CC_N | CC_Z)) | (v & 0x80 ? CC_N : 0) | (v ? 0 : CC_Z)); }
    void nzc(unsigned r)
    {
        cc = uint8_t((cc & ~(CC_N | CC_Z | CC_C)) | (r & 0x80 ? CC_N : 0) | ((r & 0xff) ? 0 : CC_Z) |
                     (r & 0x100 ? CC_C : 0));
    }
    uint8_t rmw(uint8_t op, uint8_t v);
    int     exec(uint8_t op);
};

} // namespace m68705
