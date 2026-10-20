// Unit tests for the Williams parts that need no ROMs: the 6821 PIA, the
// SC1 blitter on synthetic data, the sound board (6808 + PIA + DAC) on a
// hand-made program, and both boards (Robotron, Defender) running small
// hand-assembled 6809 programs: interrupts from the line counter, the
// video counter, ROM banking, CMOS, palette and video, the sound command,
// the watchdog and Defender's stick.
//
//   williams_tests
#include "../../emu/machines/pia6821.h"
#include "../../emu/machines/williams.h"
#include "../../emu/machines/williams_blitter.h"
#include "../../emu/sound/williams_sound.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
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

// A tiny code buffer for hand-assembled programs: bytes at an origin.
struct Code {
    uint8_t* mem;
    uint16_t base, at;
    // `region` holds `base` onwards; assembling starts at `origin`.
    Code(uint8_t* region, uint16_t base_, uint16_t origin) : mem(region), base(base_), at(origin) {}
    Code& b(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t v : bytes) mem[at++ - base] = v;
        return *this;
    }
    // Common 6809 forms.
    Code& lda(uint8_t v) { return b({0x86, v}); }
    Code& sta(uint16_t a) { return b({0xb7, uint8_t(a >> 8), uint8_t(a)}); }
    Code& lda_ext(uint16_t a) { return b({0xb6, uint8_t(a >> 8), uint8_t(a)}); }
    Code& inc(uint16_t a) { return b({0x7c, uint8_t(a >> 8), uint8_t(a)}); }
    Code& lds(uint16_t v) { return b({0x10, 0xce, uint8_t(v >> 8), uint8_t(v)}); }
    Code& here_loop() { return b({0x20, 0xfe}); }
    void vec(uint16_t vector, uint16_t to)
    {
        mem[vector - base] = uint8_t(to >> 8);
        mem[vector - base + 1] = uint8_t(to);
    }
};

// --- PIA -----------------------------------------------------------------------------

struct PiaRig {
    Pia6821 p;
    int irq_calls = 0, outs_a = 0, outs_b = 0;
    uint8_t last_a = 0, last_b = 0;
    PiaRig()
    {
        p.ctx = this;
        p.irq_changed = [](void* c) { ++static_cast<PiaRig*>(c)->irq_calls; };
        p.out_a = [](void* c, uint8_t v) { auto& r = *static_cast<PiaRig*>(c); ++r.outs_a; r.last_a = v; };
        p.out_b = [](void* c, uint8_t v) { auto& r = *static_cast<PiaRig*>(c); ++r.outs_b; r.last_b = v; };
        p.reset();
    }
};

void test_pia()
{
    PiaRig r;
    Pia6821& p = r.p;
    // After reset register 0 is the DDR.
    p.write(0, 0x0f);
    CHECK_EQ(p.read(0), 0x0f);
    CHECK_EQ(p.ddr_a(), 0x0f);
    p.write(1, 0x04);                   // data register
    p.in_a = 0x50;
    p.write(0, 0xaa);
    CHECK_EQ(p.read(0), 0x5a);          // outputs from the register, inputs from the pins
    CHECK_EQ(r.last_a, 0x5a);
    // Port B: input pins read the input; the output callback sees out & DDR.
    p.write(2, 0xf0);
    p.write(3, 0x04);
    p.write(2, 0x3c);
    CHECK_EQ(r.last_b, 0x30);
    p.in_b = 0x05;
    CHECK_EQ(p.read(2), 0x35);
    // CA1: falling edge by default (CA1 rests high).
    p.ca1(true);
    CHECK_EQ(p.read(1) & 0x80, 0);
    p.ca1(false);
    CHECK_EQ(p.read(1) & 0x80, 0x80);
    CHECK(!p.irq_a());                  // not enabled
    const int calls = r.irq_calls;
    p.write(1, 0x05);                   // enable: the waiting flag raises IRQ at once
    CHECK(p.irq_a());
    CHECK_EQ(r.irq_calls, calls + 1);
    p.read(0);                          // reading the data clears the flags
    CHECK(!p.irq_a());
    CHECK_EQ(p.read(1) & 0xc0, 0);
    // A rising edge with bit 1 clear does nothing.
    p.ca1(true);
    CHECK(!p.irq_a());
    // CB1 on the rising edge (bit 1), enabled.
    p.write(3, 0x07);
    p.cb1(true);
    CHECK(p.irq_b());
    CHECK(p.irq());
    p.cb1(false);                       // falling: no new flag, the old one stays
    CHECK(p.irq_b());
    p.read(2);
    CHECK(!p.irq_b());
    // CA2 as an input: rising edge (bit 4) with its interrupt enabled (bit 3).
    p.write(1, 0x1c);
    p.ca2(false);
    p.ca2(true);
    CHECK_EQ(p.read(1) & 0x40, 0x40);
    CHECK(p.irq_a());
    p.read(0);
    CHECK(!p.irq_a());
    // CA2 as an output (set mode): level = bit 3.
    p.write(1, 0x3c);
    CHECK(p.ca2_out());
    p.write(1, 0x34);
    CHECK(!p.ca2_out());
}

// --- blitter -------------------------------------------------------------------------------

struct BlitRig {
    uint8_t ram[0xc000] = {};
    uint8_t rom[0x9000] = {};
    uint8_t high[0x4000] = {};          // C000-FFFF as plain memory here
    m6809::Cpu cpu;
    WilliamsBlitter blit;
    BlitRig()
    {
        cpu.map_ram(0x0000, 0xc000, ram);
        cpu.map_ram(0xc000, 0x4000, high);
    }
    // Registers 1-7, then the control byte; returns the cycles held.
    int run(uint8_t control, uint8_t solid, uint16_t src, uint16_t dst, int w, int h)
    {
        blit.write(1, solid, cpu, ram);
        blit.write(2, uint8_t(src >> 8), cpu, ram);
        blit.write(3, uint8_t(src), cpu, ram);
        blit.write(4, uint8_t(dst >> 8), cpu, ram);
        blit.write(5, uint8_t(dst), cpu, ram);
        blit.write(6, uint8_t(w ^ 4), cpu, ram);   // the SC1 takes them XOR 4
        blit.write(7, uint8_t(h ^ 4), cpu, ram);
        return blit.write(0, control, cpu, ram);
    }
};

void test_blitter()
{
    {
        // A 3 x 2 image stored linearly at 9000 to the screen at 0x1020
        // (column 0x10, line 0x20): byte (x, y) lands at dst + x * 256 + y.
        BlitRig r;
        const uint8_t img[6] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};   // row 0: 12 34 56, row 1: 78 9a bc
        std::memcpy(r.ram + 0x9000, img, 6);
        const int held = r.run(0x02, 0, 0x9000, 0x1020, 3, 2);
        CHECK_EQ(r.ram[0x1020], 0x12);
        CHECK_EQ(r.ram[0x1120], 0x34);
        CHECK_EQ(r.ram[0x1220], 0x56);
        CHECK_EQ(r.ram[0x1021], 0x78);
        CHECK_EQ(r.ram[0x1221], 0xbc);
        CHECK_EQ(r.ram[0x1022], 0x00);
        CHECK_EQ(r.blit.bytes(), 6u);
        // 6 bytes = 12 accesses: (4 + 2 * 15 + 3) / 4 = 9 cycles; slow: (4 + 4 * 14 + 3) / 4 = 15.
        CHECK_EQ(held, 9);
        CHECK_EQ(r.run(0x06, 0, 0x9000, 0x1020, 3, 2), 15);
        // Sizes come XOR 4: writing 4 means 0, taken as 1.
        BlitRig q;
        q.ram[0x9000] = 0x77;
        q.blit.write(6, 4, q.cpu, q.ram);
        q.blit.write(7, 4, q.cpu, q.ram);
        q.blit.write(2, 0x90, q.cpu, q.ram);
        q.blit.write(3, 0x00, q.cpu, q.ram);
        q.blit.write(4, 0x20, q.cpu, q.ram);
        q.blit.write(5, 0x00, q.cpu, q.ram);
        q.blit.write(0, 0x00, q.cpu, q.ram);
        CHECK_EQ(q.ram[0x2000], 0x77);
        CHECK_EQ(q.blit.bytes(), 1u);
    }
    {
        // Foreground only: pixels 0 in the source leave the destination.
        BlitRig r;
        r.ram[0x9000] = 0x50;           // left pixel 5, right pixel 0
        r.ram[0x9001] = 0x0a;
        r.ram[0x2000] = 0x11;
        r.ram[0x2100] = 0x22;
        r.run(0x0a, 0, 0x9000, 0x2000, 2, 1);
        CHECK_EQ(r.ram[0x2000], 0x51);
        CHECK_EQ(r.ram[0x2100], 0x2a);
        // Solid colour + foreground only: the shape in one colour.
        r.ram[0x2000] = 0x11;
        r.ram[0x2100] = 0x22;
        r.run(0x1a, 0xcc, 0x9000, 0x2000, 2, 1);
        CHECK_EQ(r.ram[0x2000], 0xc1);
        CHECK_EQ(r.ram[0x2100], 0x2c);
        // Skip even pixels (bit 7): only the right halves change.
        r.ram[0x2000] = 0x11;
        r.run(0x82, 0, 0x9000, 0x2000, 1, 1);
        CHECK_EQ(r.ram[0x2000], 0x10);
        // Skip odd (bit 6) with foreground only: the chip then writes the
        // transparent odd pixel (and leaves the even one).
        r.ram[0x2000] = 0x11;
        r.run(0x4a, 0, 0x9000, 0x2000, 1, 1);
        CHECK_EQ(r.ram[0x2000], 0x50);
        // Shift right one pixel: each byte gets the low pixel of the one before.
        r.ram[0x9010] = 0x12;
        r.ram[0x9011] = 0x34;
        r.run(0x22, 0, 0x9010, 0x3000, 2, 1);
        CHECK_EQ(r.ram[0x3000], 0x01);
        CHECK_EQ(r.ram[0x3100], 0x23);
    }
    {
        // Source in screen layout, destination linear; the source read goes
        // through the CPU's map (ROM banked over the video RAM), the old
        // destination byte always from RAM.
        BlitRig r;
        r.rom[0x0500] = 0xab;
        r.rom[0x0501] = 0xcd;
        r.rom[0x0600] = 0xef;
        r.cpu.map_read(0x0000, 0x9000, r.rom);
        r.ram[0x0500] = 0x99;           // hidden by the ROM for the source
        r.run(0x01, 0, 0x0500, 0x9800, 2, 2);
        CHECK_EQ(r.ram[0x9800], 0xab);  // (0, 0)
        CHECK_EQ(r.ram[0x9801], 0xef);  // (1, 0): next column
        CHECK_EQ(r.ram[0x9802], 0xcd);  // (0, 1): next line
        // Screen-layout rows step the line byte only (no carry into the column).
        BlitRig q;
        q.ram[0x9000] = 0x11;
        q.ram[0x9001] = 0x22;
        q.run(0x02, 0, 0x9000, 0x10ff, 1, 2);
        CHECK_EQ(q.ram[0x10ff], 0x11);
        CHECK_EQ(q.ram[0x1000], 0x22);
    }
}

// --- sound board -------------------------------------------------------------------------

void test_sound_board()
{
    // 2 KB ROM at F800: set the PIA up, DAC = 20, wait; the IRQ handler
    // copies the command to the DAC.
    std::vector<uint8_t> rom(0x800, 0x01);
    auto put = [&](uint16_t at, std::initializer_list<uint8_t> bytes) {
        for (uint8_t v : bytes) rom[at++ - 0xf800] = v;
    };
    put(0xf800, {
        0x8e, 0x00, 0x7f,       // LDS #$007F
        0x86, 0xff, 0xb7, 0x04, 0x00,   // DDR A = FF (CRA is 0 after reset)
        0x86, 0x04, 0xb7, 0x04, 0x01,   // CRA: data
        0x86, 0x07, 0xb7, 0x04, 0x03,   // CRB: data, CB1 rising, IRQ on (DDR B stays 0)
        0x86, 0x20, 0xb7, 0x04, 0x00,   // DAC = 20
        0x0e,                   // CLI
        0x20, 0xfe,             // BRA *
    });
    put(0xf900, {0xb6, 0x04, 0x02, 0xb7, 0x04, 0x00, 0x3b});  // LDAA $0402 ; STAA $0400 ; RTI
    put(0xfff8, {0xf9, 0x00});
    put(0xfffe, {0xf8, 0x00});
    williams_sound::Board b;
    b.init(rom.data(), 0x800, 24000);
    int16_t audio[400];
    b.begin_frame(audio, 400, 14891);
    b.end_frame();
    CHECK_EQ(b.dac(), 0x20);
    CHECK(audio[399] < -1000);          // well below the middle (DAC 20 of 255)
    // The command: CB1 rises, the handler copies port B (command | C0).
    b.begin_frame(audio, 400, 14891);
    b.run_to(1000);
    b.command(0x15);
    b.end_frame();
    CHECK_EQ(b.dac(), 0xd5);
    CHECK_EQ(b.pia().in_b, 0xd5);
    // FF (command 3F) brings CB1 low: no new interrupt; another command does.
    b.begin_frame(nullptr, 0, 14891);
    b.command(0x3f);
    b.end_frame();
    CHECK_EQ(b.dac(), 0xd5);
    b.begin_frame(nullptr, 0, 14891);
    b.command(0x01);
    b.end_frame();
    CHECK_EQ(b.dac(), 0xc1);
    // A steady DAC level fades to 0 through the DC blocker.
    for (int f = 0; f < 120; ++f) {
        b.begin_frame(audio, 400, 14891);
        b.end_frame();
    }
    CHECK(audio[399] > -300 && audio[399] < 300);
    CHECK(b.dac_writes() >= 3u);
}

// --- boards --------------------------------------------------------------------------------

struct Board {
    std::vector<std::vector<uint8_t>> regions;
    std::unique_ptr<williams::Machine> m{new williams::Machine};
    williams::Board kind;
    explicit Board(williams::Board k) : kind(k)
    {
        const romset::Set& set = k == williams::Board::Robotron ? williams::robotron::kRomSet
                                                                : williams::defender::kRomSet;
        for (int r = 0; r < set.region_count; ++r) regions.emplace_back(set.regions[r].size, 0x12);  // NOPs
    }
    uint8_t* fixed()
    {
        const int r = kind == williams::Board::Robotron ? int(williams::robotron::kRegionFixed)
                                                        : int(williams::defender::kRegionFixed);
        return regions[size_t(r)].data();
    }
    void start()
    {
        std::vector<const uint8_t*> p;
        for (auto& r : regions) p.push_back(r.data());
        m->init(kind, p.data(), 24000);
    }
    int frames(int n, uint16_t buttons = 0)
    {
        int16_t audio[1024];
        int total = 0;
        for (int i = 0; i < n; ++i) {
            m->set_inputs(buttons);
            total += m->run_frame(audio, 1024);
        }
        return total;
    }
};

void test_robotron_board()
{
    Board b(williams::Board::Robotron);
    Code c(b.fixed(), 0xd000, 0xd000);
    c.lds(0xbf00)
        .lda(0x00).sta(0xc80f)          // ROM PIA: CRB -> DDR B
        .lda(0xff).sta(0xc80e)          // port B outputs (the sound command)
        .lda(0x07).sta(0xc80f)          // CRB: data, CB1 rising edge, IRQ on
        .lda(0x04).sta(0xc80d)          // CRA: data, no IRQ
        .lda(0x01).sta(0xc900)          // ROM over the video RAM
        .lda_ext(0x0000).sta(0x9801)
        .lda(0x00).sta(0xc900)          // video RAM again
        .lda_ext(0x0000).sta(0x9802)
        .lda(0x2a).sta(0xc80e)          // sound command 2A
        .lda(0x07).sta(0xc001)          // palette 1 = full red
        .lda(0xc0).sta(0xc002)          // palette 2 = full blue
        .lda(0x05).sta(0xcc10)          // CMOS: 4 bits
        .lda_ext(0xcc10).sta(0x9803)
        .b({0x1c, 0xef})                // ANDCC #$EF
        .here_loop();
    Code irq(b.fixed(), 0xd000, 0xd100);
    irq.lda_ext(0xc80e)                 // clears the flag
        .inc(0x9800)
        .b({0xf6, 0x98, 0x00})          // LDB $9800
        .b({0x8e, 0x98, 0x10})          // LDX #$9810
        .b({0x3a})                      // ABX
        .lda_ext(0xcb00)                // video counter
        .b({0xa7, 0x84})                // STA ,X
        .lda(0x39).sta(0xcbff)          // watchdog
        .b({0x3b});                     // RTI
    c.vec(0xfff8, 0xd100);
    c.vec(0xfffe, 0xd000);
    b.regions[williams::robotron::kRegionBanked][0] = 0x5a;
    b.start();
    const int samples = b.frames(10);

    CHECK(samples >= 3990 && samples <= 4000);  // 24 kHz at 60.1 frames a second
    const uint8_t* ram = b.m->ram();
    // CB1 rises at lines 32, 96, 160, 224: four interrupts a frame.
    CHECK(ram[0x9800] >= 39 && ram[0x9800] <= 40);
    // The video counter read inside each: the line & FC. The frame starts at
    // line 128, so the first is line 160.
    CHECK_EQ(ram[0x9811], 0xa0);
    CHECK_EQ(ram[0x9812], 0xe0);
    CHECK_EQ(ram[0x9813], 0x20);
    CHECK_EQ(ram[0x9814], 0x60);
    CHECK_EQ(ram[0x9801], 0x5a);        // ROM read
    CHECK_EQ(ram[0x9802], 0x00);        // video RAM read
    CHECK_EQ(ram[0x9803], 0xf5);        // CMOS: high nibble reads 1s
    CHECK_EQ(b.m->sound().pia().in_b, 0xea);    // command | C0
    CHECK_EQ(b.m->watchdog_resets(), 0);
    // Video: byte = two pixels; visible column 0 is hardware x 6 (byte 3),
    // line 0 is hardware line 7.
    b.m->ram()[3 * 256 + 7] = 0x12;
    b.m->ram()[4 * 256 + 8] = 0x20;
    b.frames(1);
    uint16_t line[williams::kScreenW];
    b.m->render_line(0, line);
    CHECK_EQ(line[0], 0xf800);          // pen 1 red
    CHECK_EQ(line[1], 0x001f);          // pen 2 blue
    CHECK_EQ(line[2], 0x0000);
    b.m->render_line(1, line);
    CHECK_EQ(line[2], 0x001f);
}

void test_watchdog()
{
    Board b(williams::Board::Robotron);
    Code c(b.fixed(), 0xd000, 0xd000);
    c.lds(0xbf00).inc(0x9800).here_loop();      // counts its starts, never feeds the dog
    c.vec(0xfffe, 0xd000);
    b.start();
    b.frames(59);
    CHECK_EQ(b.m->watchdog_resets(), 0);
    b.frames(2);
    CHECK_EQ(b.m->watchdog_resets(), 1);
    CHECK_EQ(b.m->ram()[0x9800], 2);            // RAM kept, the program ran again
}

void test_defender_board()
{
    Board b(williams::Board::Defender);
    Code c(b.fixed(), 0xd000, 0xd000);
    c.lds(0xbf00)
        .lda(0x01).sta(0xd000).lda_ext(0xc000).sta(0x9801)     // bank 1
        .lda(0x07).sta(0xd000).lda_ext(0xc000).sta(0x9802)     // bank 7
        .lda(0x05).sta(0xd000).lda_ext(0xc000).sta(0x9803)     // bank 5: empty
        .lda(0x00).sta(0xd000)                                  // I/O
        .lda(0x09).sta(0xc410).lda_ext(0xc410).sta(0x9804)      // CMOS
        .lda(0x04).sta(0xcc05)          // widget PIA: CRA data
        .lda(0x07).sta(0xcc03)          // ROM PIA: CRB data, CB1 rising, IRQ
        .b({0x1c, 0xef})
        .here_loop();
    Code irq(b.fixed(), 0xd000, 0xd100);
    irq.lda_ext(0xcc02)                 // clear the flag
        .lda_ext(0xcc04).sta(0x9810)    // IN0
        .lda(0x38).sta(0xc3fc)          // watchdog
        .b({0x3b});
    c.vec(0xfff8, 0xd100);
    c.vec(0xfffe, 0xd000);
    uint8_t* banked = b.regions[williams::defender::kRegionBanked].data();
    banked[0x0000] = 0x11;              // bank 1
    banked[0x3000] = 0x77;              // bank 7
    b.start();
    b.frames(3);
    const uint8_t* ram = b.m->ram();
    CHECK_EQ(ram[0x9801], 0x11);
    CHECK_EQ(ram[0x9802], 0x77);
    CHECK_EQ(ram[0x9803], 0x00);
    CHECK_EQ(ram[0x9804], 0xf9);
    CHECK_EQ(b.m->watchdog_resets(), 0);
    b.frames(100);
    CHECK_EQ(b.m->watchdog_resets(), 0);        // fed by 38 at C3FC
    // The stick: facing right (A098 = 03), right = thrust (IN0 bit 1).
    using namespace williams::defender;
    b.m->ram()[0xa098] = 0x03;
    b.frames(1, kRight | kFire);
    CHECK_EQ(ram[0x9810], 0x03);                // fire + thrust
    // Left while facing right: Reverse (bit 6) in 3-frame presses.
    uint8_t seen[6];
    for (int i = 0; i < 6; ++i) {
        b.frames(1, kLeft);
        seen[i] = ram[0x9810];
    }
    CHECK_EQ(seen[0], 0x40);
    CHECK_EQ(seen[2], 0x40);
    CHECK_EQ(seen[3], 0x00);
    CHECK_EQ(seen[5], 0x00);
    // Facing left (FD): left thrusts.
    b.m->ram()[0xa098] = 0xfd;
    b.frames(1, kLeft);
    CHECK_EQ(ram[0x9810], 0x02);
    // Not playing (another value): the stick does nothing.
    b.m->ram()[0xa098] = 0x00;
    b.frames(1, kLeft);
    CHECK_EQ(ram[0x9810], 0x00);
    // The cabinet's buttons pass straight through.
    b.frames(1, kReverse | kSmartBomb | kHyperspace | kDown);
    CHECK_EQ(ram[0x9810], 0x40 | 0x04 | 0x08 | 0x80);
}

} // namespace

int main()
{
    test_pia();
    test_blitter();
    test_sound_board();
    test_robotron_board();
    test_watchdog();
    test_defender_board();
    std::printf("williams tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
