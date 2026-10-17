// Unit tests for the Atari boards' parts, with hand-made programs and
// synthetic data - no ROMs: the 6502 core's timing and interrupts (the
// instruction set is checked by Klaus Dormann's tests in m6502_test), the
// POKEY (dividers, joined / fast clocks, polynomial counters, RANDOM,
// volume-only, the high-pass flip-flop), the vector display's rasteriser,
// the DVG's instructions on hand-made display lists, the Asteroids sound
// models and the Asteroids machine's wiring on a tiny 6502 program, the
// AVG's instructions and normalisation, the math box, and the Tempest
// machine's wiring.
//
//   atari_tests
#include "../../emu/cpu/m6502.h"
#include "../../emu/machines/asteroids.h"
#include "../../emu/machines/atari_avg.h"
#include "../../emu/machines/atari_dvg.h"
#include "../../emu/machines/atari_mathbox.h"
#include "../../emu/machines/tempest.h"
#include "../../emu/machines/vector_display.h"
#include "../../emu/sound/asteroids_sound.h"
#include "../../emu/sound/pokey.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// --- POKEY ----------------------------------------------------------------------

int popcount(uint32_t v)
{
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}

// Rising edges of a rendered signal (crossing its mid level).
int edges(const std::vector<int16_t>& v)
{
    int lo = 32767, hi = -32768;
    for (int16_t s : v) { if (s < lo) lo = s; if (s > hi) hi = s; }
    const int mid = (lo + hi) / 2;
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i) n += v[i - 1] <= mid && v[i] > mid;
    return n;
}

void test_pokey()
{
    // The polynomial counters are maximal-length: 2^(n-1) ones in a period
    // (the XNOR-fed 4 and 5 bit ones give the inverse: 2^(n-1) - 1).
    CHECK_EQ(popcount(pokey::poly4_bits()), 7);
    CHECK_EQ(popcount(pokey::poly5_bits()), 15);
    int ones = 0;
    for (uint32_t p = 0; p < 131071; ++p) ones += pokey::poly17_bit8(p);
    CHECK_EQ(ones, 65536);
    // RANDOM four clocks later: the old high nibble in the low one (the
    // check Tempest makes).
    bool shift_ok = true;
    for (uint32_t p = 0; p < 5000; p += 7)
        shift_ok = shift_ok && ((pokey::random17(p + 4) & 0x0f) == (pokey::random17(p) >> 4));
    CHECK(shift_ok);

    const uint32_t clock = 1512000, rate = 24000;
    pokey::Pokey pk;
    pk.init(clock, rate);
    std::vector<int16_t> out(rate);
    // In reset (SKCTL 0): nothing counts.
    pk.write(pokey::kAudf1, 99, 0);
    pk.write(pokey::kAudc1, 0xa8, 0);           // pure tone, volume 8
    pk.render(out.data(), 1000);
    CHECK_EQ(pk.underflows(0), 0);
    // Out of reset: 64 kHz base, AUDF 99 -> 2800 clocks a half period = 270 Hz.
    pk.write(pokey::kSkctl, 3, 0);
    pk.render(out.data(), int(rate));
    CHECK(std::abs(edges(out) - 270) <= 1);
    int hi = 0;
    for (int16_t s : out) if (s > hi) hi = s;
    CHECK_EQ(hi, 8 * pokey::kLevelScale);
    // 15 kHz base: 100 x 114 clocks -> 66 Hz.
    pk.write(pokey::kAudctl, 0x01, 0);
    pk.render(out.data(), int(rate));
    CHECK(std::abs(edges(out) - 66) <= 1);
    // Channel 1 at the full clock: AUDF + 4 = 103 clocks -> 7340 Hz.
    pk.write(pokey::kAudctl, 0x40, 0);
    pk.render(out.data(), int(rate));
    const uint32_t u0 = pk.underflows(0);
    pk.render(out.data(), int(rate));
    CHECK(std::abs(int(pk.underflows(0) - u0) - int(clock / 103)) <= 2);
    // Channels 1 + 2 joined, channel 2 sounding: (0x0123 + 1) x 28 clocks.
    pk.write(pokey::kAudc1, 0, 0);
    pk.write(pokey::kAudf1, 0x23, 0);
    pk.write(pokey::kAudf2, 0x01, 0);
    pk.write(pokey::kAudc2, 0xaf, 0);
    pk.write(pokey::kAudctl, 0x10, 0);
    pk.render(out.data(), int(rate));
    const uint32_t u2 = pk.underflows(1);
    pk.render(out.data(), int(rate));
    CHECK(std::abs(int(pk.underflows(1) - u2) - int(clock / (0x124 * 28))) <= 1);
    CHECK(std::abs(edges(out) - int(clock / (0x124 * 28) / 2)) <= 1);
    // Volume only: a constant level.
    pk.write(pokey::kAudc2, 0x15, 0);
    pk.render(out.data(), 100);
    CHECK_EQ(out[50], 5 * pokey::kLevelScale);
    CHECK_EQ(out[99], 5 * pokey::kLevelScale);
    // Noise (17-bit poly) from channel 3: both levels, irregular.
    pk.write(pokey::kAudc2, 0, 0);
    pk.write(pokey::kAudctl, 0, 0);
    pk.write(pokey::kAudf3, 3, 0);
    pk.write(pokey::kAudc3, 0x0f, 0);
    pk.render(out.data(), int(rate));
    int zeros = 0, fulls = 0;
    for (int16_t s : out) { zeros += s == 0; fulls += s == 15 * pokey::kLevelScale; }
    CHECK(zeros > 1000 && fulls > 1000);
    // High-pass: channel 1 sampled by channel 3 at the same rate stays put.
    pk.write(pokey::kAudc3, 0, 0);
    pk.write(pokey::kAudf1, 50, 0);
    pk.write(pokey::kAudf3, 50, 0);
    pk.write(pokey::kAudc1, 0xaf, 0);
    pk.write(pokey::kAudctl, 0x04, 0);
    pk.write(pokey::kStimer, 0, 0);
    pk.render(out.data(), int(rate));
    CHECK(edges(out) < 5);
    // RANDOM from the chip: timed by the clock passed in.
    pk.write(pokey::kSkctl, 0, 1000);
    pk.write(pokey::kSkctl, 3, 1000);
    CHECK_EQ(pk.read(pokey::kRandom, 1000), pokey::random17(0));
    CHECK_EQ(pk.read(pokey::kRandom, 1000 + 12345), pokey::random17(12345));
    pk.set_allpot(0x5a);
    CHECK_EQ(pk.read(pokey::kAllpot, 0), 0x5a);
}

// --- vector display ---------------------------------------------------------------

constexpr int S = vector_display::kSub;

std::vector<uint16_t> row(vector_display::Display& d, int y)
{
    std::vector<uint16_t> r(size_t(d.width()), 0xdead);
    d.render_line(y, r.data());
    return r;
}

// The x of every pixel set in a row.
std::vector<int> lit(const std::vector<uint16_t>& r)
{
    std::vector<int> x;
    for (size_t i = 0; i < r.size(); ++i) if (r[i]) x.push_back(int(i));
    return x;
}

void test_vector_display()
{
    static vector_display::Display d;       // ~20 KB: not on the stack
    d.init(32, 16);
    CHECK_EQ(lit(row(d, 3)).size(), 0);
    d.begin();
    d.add(2 * S, 3 * S + 8, 10 * S + 8, 3 * S + 8, 1);          // flat
    d.add(5 * S + 8, 6 * S + 8, 5 * S + 8, 12 * S + 8, 2);      // upright
    d.add(20 * S + 8, 10 * S + 8, 20 * S + 8, 10 * S + 8, 3);   // dot
    d.add(-100 * S, 14 * S + 8, 100 * S, 14 * S + 8, 4);        // clipped
    d.add(0, 0, 15 * S + 15, 15 * S + 15, 5);                   // 45 degrees
    d.finish();
    CHECK_EQ(d.count(), 5);
    std::vector<uint16_t> r = row(d, 3);
    CHECK_EQ(lit(r).size(), 9);                                 // x 2-10 (the diagonal at x 3)
    CHECK_EQ(r[2], 1);
    CHECK_EQ(r[10], 1);
    CHECK_EQ(r[3], 5);                                          // added later: on top
    for (int y = 6; y <= 12; ++y) CHECK_EQ(row(d, y)[5], 2);
    CHECK_EQ(row(d, 13)[5], 0);
    r = row(d, 10);
    CHECK_EQ(r[20], 3);
    CHECK_EQ(r[21], 3);
    CHECK_EQ(row(d, 11)[20], 3);
    CHECK_EQ(row(d, 12)[21], 0);
    CHECK_EQ(lit(row(d, 14)).size(), 32);
    // The diagonal: one pixel a row, at x = y.
    bool diag = true;
    for (int y = 0; y < 16; ++y) {
        const std::vector<uint16_t> q = row(d, y);
        diag = diag && q[size_t(y)] == 5 && (y == 0 || q[size_t(y - 1)] != 5) && (y == 15 || q[size_t(y + 1)] != 5);
    }
    CHECK(diag);
    // Rows out of order give the same as in order.
    const std::vector<uint16_t> a = row(d, 9);
    row(d, 2);
    CHECK(row(d, 9) == a);
    CHECK(row(d, 10) == row(d, 10));
    // Off the screen entirely: dropped.
    d.begin();
    d.add(40 * S, 2 * S, 50 * S, 3 * S, 1);
    d.finish();
    CHECK_EQ(d.count(), 0);
    // Too many: counted.
    d.begin();
    for (int i = 0; i < vector_display::Display::kMaxSegments + 5; ++i) d.add(0, 0, S, S, 1);
    d.finish();
    CHECK_EQ(d.count(), vector_display::Display::kMaxSegments);
    CHECK_EQ(d.dropped(), 5);
}

// --- DVG ------------------------------------------------------------------------

struct DvgRig {
    uint8_t mem[0x2000] = {};
    struct Line { int x0, y0, x1, y1, i; };
    std::vector<Line> lines;
    int at = 0;                         // next word written
    void w(uint16_t v) { mem[at * 2] = uint8_t(v); mem[at * 2 + 1] = uint8_t(v >> 8); ++at; }
    static uint8_t rd(void* c, uint16_t a) { return static_cast<DvgRig*>(c)->mem[(a - 0x4000) & 0x1fff]; }
    static void line(void* c, int x0, int y0, int x1, int y1, int i)
    {
        static_cast<DvgRig*>(c)->lines.push_back({x0, y0, x1, y1, i});
    }
};

void test_dvg()
{
    DvgRig r;
    atari_dvg::Dvg dvg;
    dvg.init(DvgRig::rd, DvgRig::line, &r, 0x4000);
    r.w(0xa000 | 500);                  // LABS y 500
    r.w(0x0000 | 300);                  //      x 300, scale 0
    r.w(0x9000 | 50);                   // VCTR scale 9: dy +50
    r.w(0x7000 | 100);                  //      intensity 7, dx +100
    r.w(0x9000 | 0x400 | 20);           // VCTR dy -20
    r.w(0x0000 | 0x400 | 30);           //      intensity 0 (a move), dx -30
    r.w(0xc000 | 0x100);                // JSRL 100
    r.w(0x0000 | 1023);                 // VCTR scale 0: dy (1023 x 2) >> 10 = 1
    r.w(0xf000 | 1023);                 //      intensity 15, dx 1
    r.w(0xa000 | 100);                  // LABS y 100
    r.w(0x7000 | 50);                   //      x 50, global scale 7
    r.w(0xfdd0);                        // SVEC: y -1 (x 256), scale bit 0; intensity 13
    r.w(0xb000);                        // HALT
    r.at = 0x100;
    r.w(0x8000 | 0x400 | 512);          // VCTR scale 8: dy -(512 x 512 >> 10) = -256
    r.w(0xf000 | 0);                    //      dx 0
    r.w(0xd000);                        // RTSL
    const int clocks = dvg.run();
    CHECK_EQ(dvg.ops(), 10);
    CHECK_EQ(int(r.lines.size()), 4);
    if (r.lines.size() == 4) {
        CHECK(r.lines[0].x0 == 300 && r.lines[0].y0 == 500 && r.lines[0].x1 == 400 && r.lines[0].y1 == 550);
        CHECK_EQ(r.lines[0].i, 7);
        // After the move (-30, -20): 370, 530; the subroutine's line down 256.
        CHECK(r.lines[1].x0 == 370 && r.lines[1].y0 == 530 && r.lines[1].x1 == 370 && r.lines[1].y1 == 274);
        CHECK(r.lines[2].x1 == 371 && r.lines[2].y1 == 275);
        // SVEC: scale 7 + 2 + 1 = 10: no movement at all (the multipliers' input is 0).
        CHECK(r.lines[3].x0 == 50 && r.lines[3].y0 == 100 && r.lines[3].x1 == 50 && r.lines[3].y1 == 100);
        CHECK_EQ(r.lines[3].i, 13);
    }
    // Clocks: 2 a word (16 words read) + 2^(s+1) per vector: 1024 x 2 + 512 + 2 + 0.
    CHECK_EQ(clocks, 16 * 2 + 1024 * 2 + 512 + 2);

    // SVEC lengths: scale 2 + bits, magnitude (0-3) x 256.
    DvgRig q;
    dvg.init(DvgRig::rd, DvgRig::line, &q, 0x4000);
    q.w(0xa000 | 400);
    q.w(0x6000 | 400);                  // global scale 6
    q.w(0xf000 | 0x0300 | 0x00f1);      // SVEC y +3 (768), x +1 (256), intensity 15; scale 6 + 2
    q.w(0xf000 | 0x0c00 | 0x008b);      // SVEC y -0, scale bit 0; x +3, scale bit 1: 6 + 5 = 11 -> none
    q.w(0xb000);
    dvg.run();
    CHECK_EQ(int(q.lines.size()), 2);
    if (q.lines.size() == 2) {
        // scale 8: 2^9 clocks: 768 x 512 >> 10 = 384, 256 x 512 >> 10 = 128.
        CHECK(q.lines[0].x1 == 528 && q.lines[0].y1 == 784);
        CHECK(q.lines[1].x1 == 528 && q.lines[1].y1 == 784);
    }
    // Lines entirely off the screen aren't sent; half off ones are.
    DvgRig o;
    dvg.init(DvgRig::rd, DvgRig::line, &o, 0x4000);
    o.w(0xa000 | 500);
    o.w(0x0000 | 1000);                 // LABS (1000, 500), global scale 0
    o.w(0x9000 | 0);
    o.w(0xf000 | 200);                  // x 1000 -> 1200: half off
    o.w(0x9000 | 0);
    o.w(0xf000 | 200);                  // 1200 -> 1400: off
    o.w(0xb000);
    dvg.run();
    CHECK_EQ(int(o.lines.size()), 1);
    if (!o.lines.empty()) CHECK(o.lines[0].x0 == 1000 && o.lines[0].x1 == 1200);
}

// --- Asteroids ----------------------------------------------------------------------

void test_asteroids_sound()
{
    asteroids_sound::Board b;
    b.init(24000);
    std::vector<int16_t> v(24000);
    b.render(v.data(), int(v.size()));
    int pk = 0;
    for (int16_t s : v) pk = std::max(pk, std::abs(int(s)));
    CHECK_EQ(pk, 0);                    // all off: silence
    // The thump's pitch falls as its data rises (the resistor DAC feeds a
    // current source): ~207 Hz at 0, ~65 Hz at 15.
    b.thump(0x10);
    CHECK(std::abs(b.thump_hz() - 207) < 10);
    b.thump(0x1f);
    CHECK(std::abs(b.thump_hz() - 65) < 5);
    // Each sound alone stays under full scale.
    const struct { int bit; } bits[] = {{0}, {1}, {3}, {4}, {5}};
    bool quiet_enough = true, heard = true;
    for (const auto& e : bits) {
        b.reset();
        b.latch(e.bit, true);
        b.render(v.data(), int(v.size()));
        int p = 0;
        for (int16_t s : v) p = std::max(p, std::abs(int(s)));
        quiet_enough = quiet_enough && p < 24000;
        heard = heard && p > 3000;
    }
    CHECK(quiet_enough);
    CHECK(heard);
    b.reset();
    b.explode(0x3c);
    b.render(v.data(), int(v.size()));
    pk = 0;
    for (int16_t s : v) pk = std::max(pk, std::abs(int(s)));
    CHECK(pk > 10000 && pk < 26000);
    // The noise register: reset to 0, XNOR feedback brings ones in.
    b.noise_reset();
    CHECK_EQ(b.noise(), 0);
    b.render(v.data(), 10);
    CHECK(b.noise() != 0);
}

void test_asteroids()
{
    static uint8_t cpu[0x1800], vec[0x800];
    std::memset(cpu, 0xea, sizeof cpu);
    const uint8_t prog[] = {
        0xa2, 0xff, 0x9a,                       // 6800 LDX #FF; TXS
        0xa9, 0x00, 0x8d, 0x00, 0x40,           //      vector RAM word 0: JMPL 800 (5000)
        0xa9, 0xe8, 0x8d, 0x01, 0x40,
        0x8d, 0x00, 0x30,                       //      VGGO
        0xad, 0x03, 0x28, 0x85, 0x11,           //      DIP SW2,1 -> 11
        0xad, 0x02, 0x28, 0x85, 0x12,           //      DIP SW4,3 -> 12
        0xa9, 0x80, 0x8d, 0x03, 0x3c,           //      thrust sound on
        0xa9, 0x55, 0x8d, 0x00, 0x02,           //      0200 = 55
        0xa9, 0x04, 0x8d, 0x00, 0x32,           //      RAMSEL
        0xa9, 0xaa, 0x8d, 0x00, 0x02,           //      0200 = AA (the other page now)
        0xad, 0x07, 0x24, 0x85, 0x10,           // loop: rotate left -> 10
        0x4c, 0x2e, 0x68,                       //      JMP loop (682E)
    };
    std::memcpy(cpu, prog, sizeof prog);
    const uint8_t nmi[] = {0xe6, 0x20, 0x8d, 0x00, 0x34, 0x40};     // INC 20; watchdog; RTI
    std::memcpy(cpu + 0x1000, nmi, sizeof nmi);                     // 7800
    cpu[0x17fa] = 0x00; cpu[0x17fb] = 0x78;     // NMI 7800
    cpu[0x17fc] = 0x00; cpu[0x17fd] = 0x68;     // reset 6800
    // Vector ROM: LABS (300, 500); VCTR scale 9 dx +100, intensity 15; HALT.
    const uint16_t list[] = {0xa000 | 500, 300, 0x9000, 0xf000 | 100, 0xb000};
    for (int i = 0; i < 5; ++i) { vec[i * 2] = uint8_t(list[i]); vec[i * 2 + 1] = uint8_t(list[i] >> 8); }

    static asteroids::Machine m;
    m.init({cpu, vec}, 24000, false);
    m.set_inputs(asteroids::kLeft);
    int16_t audio[1024];
    for (int f = 0; f < 3; ++f) CHECK(std::abs(m.run_frame(audio, 1024) - 390) <= 1);
    CHECK_EQ(m.cpu().rd(0x0020), 11);           // 4 NMIs a frame (the 12th taken next frame)
    CHECK_EQ(m.cpu().rd(0x0010), 0x80);         // rotate left pressed
    CHECK_EQ(m.cpu().rd(0x0011), 0xfc);         // English: SW2,1 off
    CHECK_EQ(m.cpu().rd(0x0012), 0xfd);         // 3 ships: SW3 on
    CHECK_EQ(m.cpu().rd(0x0200), 0xaa);         // RAMSEL: the pages swapped
    CHECK_EQ(m.cpu().rd(0x0300), 0x55);
    CHECK_EQ(m.cpu().rd(0x8300), 0x55);         // A15 not decoded
    CHECK(m.sound().on(asteroids_sound::kThrust));
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(m.vg_runs(), 1u);
    CHECK_EQ(m.display().count(), 1);
    // The line: DVG y 500 -> row (935 - 500) x 240 / 848 = 123.
    const std::vector<int> px = lit([&] {
        std::vector<uint16_t> r(asteroids::kScreenW);
        m.render_line(123, r.data());
        return r;
    }());
    CHECK(!px.empty());
    if (!px.empty()) {
        CHECK(std::abs(px.front() - (15 + 300 * 240 / 848)) <= 1);
        CHECK(std::abs(px.back() - (15 + 400 * 240 / 848)) <= 1);
    }
    m.set_inputs(0);
    m.run_frame(audio, 1024);
    CHECK_EQ(m.cpu().rd(0x0010), 0x7f);
    // No watchdog writes (NMIs off in self-test) for long enough: a reset.
    m.set_inputs(asteroids::kTest);
    for (int f = 0; f < 8; ++f) m.run_frame(audio, 1024);
    CHECK(m.watchdog_resets() >= 1);
}

// --- AVG ---------------------------------------------------------------------------

// The hardware's normalisation, step by step (the reference for the
// closed form in atari_avg).
int normalise_steps(int& dvx, int& dvy, int bin_scale, bool svec)
{
    const int fill = 0x4000 | (svec ? 0x80 : 0);
    int timer = 0;
    for (int i = 0; i < 16 && !((dvy ^ (dvy << 1)) & 0x1000) && !((dvx ^ (dvx << 1)) & 0x1000); ++i) {
        dvy = (dvy & 0x1000) | ((dvy << 1) & 0x1fff);
        dvx = (dvx & 0x1000) | ((dvx << 1) & 0x1fff);
        timer = (timer >> 1) | fill;
    }
    if (svec) timer &= 0xff;
    for (int i = bin_scale; i > 0; --i) timer = (timer >> 1) | fill;
    if (svec) timer &= 0xff;
    return svec ? 0x100 - (timer & 0xff) : 0x8000 - timer;
}

struct AvgRig {
    uint8_t mem[0x4000] = {};
    struct Line { int32_t x0, y0, x1, y1; int color, level; };
    std::vector<Line> lines;
    int at = 0;
    void w(uint16_t v) { mem[at * 2] = uint8_t(v); mem[at * 2 + 1] = uint8_t(v >> 8); ++at; }
    static uint8_t rd(void* c, uint16_t a) { return static_cast<AvgRig*>(c)->mem[(a - 0x2000) & 0x3fff]; }
    static void line(void* c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level)
    {
        static_cast<AvgRig*>(c)->lines.push_back({x0, y0, x1, y1, color, level});
    }
};

void test_avg()
{
    // The closed-form normalisation matches the step-by-step one.
    bool same = true;
    for (int x = 0; x < 0x2000 && same; x += 7) {
        for (int y : {0, 1, 2, 3, 0x7f, 0x100, 0x7ff, 0xfff, 0x1000, 0x1800, 0x1f00, 0x1ffe, 0x1fff}) {
            for (int b = 0; b < 8; b += 3) {
                for (int sv = 0; sv < 2; ++sv) {
                    int x1 = x, y1 = y, x2 = x, y2 = y;
                    const int c1 = atari_avg::Avg::normalise(x1, y1, b, sv);
                    const int c2 = normalise_steps(x2, y2, b, sv);
                    same = same && c1 == c2 && x1 == x2 && y1 == y2;
                }
            }
        }
    }
    CHECK(same);

    AvgRig r;
    atari_avg::Avg avg;
    const int32_t cx = 290 << 16, cy = 285 << 16;
    avg.init(AvgRig::rd, AvgRig::line, &r, 0x2000, cx, cy);
    r.w(0x8000);                        // CNTR
    r.w(0x7000 | 0x0000);               // SCAL binary 0, linear 0
    r.w(0x6800 | 0x0005);               // STAT colour 5
    r.w(0x6000 | 0x00c0);               // STAT intensity 12
    r.w(0x0000 | 100);                  // VCTR y +100
    r.w(0x2000 | 50);                   //      z 1 (the STAT intensity), x +50
    r.w(0xa000 | 0x100);                // JSRL 100
    r.w(0x4000 | 0x0100 | 0x00e2);      // SVEC y +1, z 7, x +2
    r.w(0xe000);                        // JMPL 0: the picture ends
    r.at = 0x100;
    r.w(0x7100);                        // SCAL binary 1 (half size)
    r.w(0x0000 | 0x1000 | (0x1000 - 40));   // VCTR y -40
    r.w(0x8000 | 0);                    //      z 4 (intensity 8), x 0
    r.w(0xc000);                        // RTSL
    avg.run();
    CHECK(!avg.halted());
    CHECK_EQ(avg.color(), 5);
    CHECK_EQ(int(r.lines.size()), 3);
    if (r.lines.size() == 3) {
        // 1 unit of a long vector ~ 1 screen unit x 255 / 256 at linear
        // scale 0; Y up on the AVG = down the screen.
        const AvgRig::Line& a = r.lines[0];
        CHECK(a.x0 == cx && a.y0 == cy);
        CHECK(std::abs((a.x1 - cx) - 50 * 65280) < 65536 * 2);
        CHECK(std::abs((cy - a.y1) - 100 * 65280) < 65536 * 2);
        CHECK_EQ(a.color, 5);
        CHECK_EQ(a.level, 12);
        const AvgRig::Line& b = r.lines[1];      // half of 40, downwards
        CHECK(std::abs((b.y1 - b.y0) - 20 * 65280) < 65536 * 2);
        CHECK_EQ(b.x1, b.x0);
        CHECK_EQ(b.level, 8);
        // SVEC: twice a long vector's unit, binary scale 1 still: (2, 1).
        const AvgRig::Line& c = r.lines[2];
        CHECK(std::abs((c.x1 - c.x0) - 2 * 65280) < 65536);
        CHECK(std::abs((c.y0 - c.y1) - 1 * 65280) < 65536);
        CHECK_EQ(c.level, 14);
    }
    // HALT stops; the VG reset clears the scales and colour.
    AvgRig h;
    avg.init(AvgRig::rd, AvgRig::line, &h, 0x2000, cx, cy);
    h.w(0x7000 | 0x0080);               // linear scale 0x80: half speed
    h.w(0x0000 | 64);
    h.w(0xe000 | 0);                    // z 7, x 0
    h.w(0x2000);                        // HALT
    avg.run();
    CHECK(avg.halted());
    CHECK_EQ(avg.ops(), 3);
    CHECK_EQ(avg.scale(), 0x80);
    if (h.lines.size() == 1) CHECK(std::abs((h.lines[0].y0 - h.lines[0].y1) - 64 * 127 * 256) < 65536);
    avg.reset();
    CHECK_EQ(avg.scale(), 0);
}

void test_mathbox()
{
    atari_mathbox::Mathbox m;
    m.reset();
    // Loads read back; the result is the register loaded.
    m.go(0x15, 0x34);
    m.go(0x16, 0x12);
    CHECK_EQ(uint16_t(m.reg(7)), 0x1234);
    CHECK_EQ(m.lo(), 0x34);
    CHECK_EQ(m.hi(), 0x12);
    // Divide (14): (Rb:Ra) / R7 with R6 + 1 quotient bits, from 2^15 down.
    auto divide = [&](int hi, int lo, int d, int r6) {
        m.go(0x15, uint8_t(d)); m.go(0x16, uint8_t(d >> 8));
        m.go(0x0c, uint8_t(r6));
        m.go(0x0d, uint8_t(lo)); m.go(0x0e, uint8_t(lo >> 8));
        m.go(0x0f, uint8_t(hi)); m.go(0x10, uint8_t(hi >> 8));
        m.go(0x14, 0);
        return int(m.result());
    };
    CHECK_EQ(divide(0x10, 0x0000, 0x40, 15), (0x100000 / 0x40) >> 1);
    CHECK_EQ(divide(0x03, 0x1234, 0x100, 15), (0x31234 / 0x100) >> 1);
    CHECK_EQ(divide(0x10, 0x0000, 0x40, 7), (0x100000 / 0x40) >> 9);
    CHECK_EQ(divide(0x10, 0x0000, -0x40 & 0xffff, 15), -((0x100000 / 0x40) >> 1));
    // Distance (1D): max + 3/8 min of |x1 - x0|, |y1 - y0|.
    m.go(0x00, 10); m.go(0x01, 0);      // R0 = 10
    m.go(0x02, 20); m.go(0x03, 0);      // R1 = 20
    m.go(0x04, 110); m.go(0x05, 0);     // R2 = 110: dx 100
    m.go(0x06, 60);                     // R3 low = 60
    m.go(0x1d, 0);                      // R3 high = 0: dy 40
    CHECK_EQ(m.result(), 100 + 10 + 5);     // 100 + 40/4 + 40/8
}

// --- Tempest ------------------------------------------------------------------------

void test_tempest()
{
    static uint8_t cpu[0x5000], vec[0x1000];
    std::memset(cpu, 0xea, sizeof cpu);
    const uint8_t prog[] = {
        0xa2, 0xff, 0x9a,                       // 9000 LDX #FF; TXS
        0xa9, 0x0f, 0x8d, 0xc8, 0x60,           //      POKEY 1 AUDCTL (keeps it busy)
        0xa9, 0x03, 0x8d, 0xcf, 0x60,           //      POKEY 1 SKCTL: out of reset
        0xa9, 0xf5, 0x8d, 0x05, 0x08,           //      colour 5 = F5 (bits 1 and 3 low: red + green)
        0xa9, 0x00, 0x8d, 0x00, 0x48,           //      AVG go
        0xa9, 0x5a, 0x8d, 0x07, 0x60,           //      EAROM address 7 = 5A
        0xa9, 0x0c, 0x8d, 0x40, 0x60,           //      EAROM control: write (C1, C2 low, selected)
        0xa9, 0x09, 0x8d, 0x40, 0x60,           //      read mode, clock high
        0xa9, 0x08, 0x8d, 0x40, 0x60,           //      clock low: latch
        0xad, 0x50, 0x60, 0x85, 0x12,           //      EAROM data -> 12
        0xa9, 0x40, 0x8d, 0x8e, 0x60,           //      math box: Ra high = 40
        0xad, 0x70, 0x60, 0x85, 0x13,           //      result high -> 13
        0x58,                                   //      CLI
        0xad, 0xc8, 0x60, 0x85, 0x10,           // loop: POKEY 1 ALLPOT (spinner) -> 10
        0xad, 0xd8, 0x60, 0x85, 0x11,           //      POKEY 2 ALLPOT (buttons) -> 11
        0x4c, 0x3b, 0x90,                       //      JMP loop (903B)
    };
    std::memcpy(cpu, prog, sizeof prog);
    const uint8_t irq[] = {0xe6, 0x20, 0x8d, 0x00, 0x50, 0x40};     // INC 20; ack + watchdog; RTI
    std::memcpy(cpu + 0x3000, irq, sizeof irq);                     // C000
    cpu[0x4ffe] = 0x00; cpu[0x4fff] = 0xc0;     // IRQ C000 (DFFE, seen at FFFE)
    cpu[0x4ffc] = 0x00; cpu[0x4ffd] = 0x90;     // reset 9000
    // Vector ROM (word 0x800 = 3000): CNTR; STAT colour 5; STAT intensity
    // 15; VCTR x +100 z 1; JMPL 0. Vector RAM word 0: JMPL 800.
    const uint16_t list[] = {0x8000, 0x6805, 0x60f0, 0x0000, 0x2000 | 100, 0xe000};
    for (int i = 0; i < 6; ++i) { vec[i * 2] = uint8_t(list[i]); vec[i * 2 + 1] = uint8_t(list[i] >> 8); }

    static tempest::Machine m;
    m.init({cpu, vec}, 24000, false);
    m.cpu().wr(0x2000, 0x00);
    m.cpu().wr(0x2001, 0xe8);
    m.set_inputs(tempest::kFire | tempest::kSpinRight);
    int16_t audio[1024];
    for (int f = 0; f < 4; ++f) m.run_frame(audio, 1024);
    CHECK(m.cpu().rd(0x0020) >= 14);            // 4 IRQs a frame, each acknowledged
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(m.cpu().rd(0x0012), 0x5a);         // the EAROM kept the byte
    CHECK_EQ(m.earom()[7], 0x5a);
    CHECK_EQ(m.cpu().rd(0x0013), 0x40);         // math box result
    CHECK_EQ(m.cpu().rd(0x0011), 0x17);         // fire: bit 4 HIGH (the game takes 1 = held)
    // Clockwise counts the knob down: 4 frames x 1.5 counts.
    CHECK_EQ(m.knob(), (16 - 6) & 15);
    CHECK_EQ(m.cpu().rd(0x0010) & 0x0f, m.knob());
    CHECK_EQ(m.cpu().rd(0x0010) & 0xf0, 0xf0);
    // The picture: one line from the centre, red + green = yellow. Square
    // units: 570 over 240 rows, the 580-unit window centred in the 320.
    CHECK(m.vg_runs() >= 1);
    CHECK_EQ(m.display().count(), 1);
    const int y = 285 * 240 / 570;
    const int left = (320 - 580 * 240 / 570) / 2;
    std::vector<uint16_t> row(tempest::kScreenW);
    m.render_line(y, row.data());
    const std::vector<int> px = lit(row);
    CHECK(!px.empty());
    if (!px.empty()) {
        CHECK(std::abs(px.front() - (left + 290 * 240 / 570)) <= 1);
        CHECK(std::abs(px.front() - 160) <= 1);         // the window's centre = the frame's
        CHECK(std::abs(px.back() - (left + 390 * 240 / 570)) <= 1);
        const uint16_t c = row[size_t(px.front())];
        CHECK((c >> 11) > 20 && ((c >> 5) & 63) > 40 && (c & 31) == 0);
    }
    // Nothing pressed: fire, Superzapper and the starts read low (an idle
    // fire button read high made the game fire on its own); each reads high.
    m.set_inputs(0);
    m.run_frame(audio, 1024);
    CHECK_EQ(m.cpu().rd(0x0011), 0x07);
    const uint16_t keys[] = {tempest::kZap, tempest::kFire, tempest::kStart1, tempest::kStart2};
    for (int i = 0; i < 4; ++i) {
        m.set_inputs(keys[i]);
        m.run_frame(audio, 1024);
        CHECK_EQ(m.cpu().rd(0x0011), 0x07 | (0x08 << i));
    }
    // Without the IRQ acknowledged, the watchdog resets the board.
    m.cpu().wr(0x0000, 0);
    m.set_inputs(0);
    static uint8_t cpu2[0x5000];
    std::memcpy(cpu2, cpu, sizeof cpu2);
    cpu2[0x3002] = 0xea; cpu2[0x3003] = 0xea; cpu2[0x3004] = 0xea;   // no write to 5000
    static tempest::Machine w;
    w.init({cpu2, vec}, 24000, false);
    for (int f = 0; f < 8; ++f) w.run_frame(audio, 1024);
    CHECK(w.watchdog_resets() >= 1);
}

} // namespace

int main()
{
    test_6502_timing();
    test_6502_interrupts();
    test_6502_hook();
    test_pokey();
    test_vector_display();
    test_dvg();
    test_asteroids_sound();
    test_asteroids();
    test_avg();
    test_mathbox();
    test_tempest();
    std::printf("atari tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
