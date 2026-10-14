// Unit tests for the Namco Galaga-family parts that can be checked without
// ROMs: the 06XX bus timing, the 51XX / 53XX behaviour models, the 05XX
// starfield against a step-by-step LFSR, the 54XX noise model, and the
// three-CPU board (interrupts, resets, watchdog, a 06XX transfer to the
// 51XX, idle-loop skipping) on tiny hand-made Z80 programs, plus the
// Galaga and Dig Dug video on synthetic graphics.
//
//   namco_tests
#include "../../emu/machines/digdug.h"
#include "../../emu/machines/galaga.h"
#include "../../emu/machines/namco_io.h"
#include "../../emu/machines/starfield05.h"
#include "../../emu/sound/namco54.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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

void put(uint8_t* rom, int at, std::initializer_list<uint8_t> bytes)
{
    for (uint8_t b : bytes) rom[at++] = b;
}

// --- 06XX ----------------------------------------------------------------------

void test_bus06()
{
    namco_io::Bus06 b;
    b.reset();
    CHECK_EQ(b.control(), 0);
    CHECK(b.next_nmi() == namco_io::Bus06::kNever);
    // Read 3 from chip 0 at divider 3: a period of 64 << 3 CPU cycles; the
    // first pulse is skipped, so the first NMI is a period after the next
    // 06XX clock (every 64 cycles).
    b.set_control(0x71, 1000);
    CHECK(b.reading());
    CHECK_EQ(b.selected(), 1);
    CHECK_EQ(b.period(), 512);
    CHECK_EQ(b.next_nmi(), 1024 + 512);
    b.nmi_taken();
    CHECK_EQ(b.next_nmi(), 1024 + 1024);
    // Write to chip 3 at divider 5: the first NMI on the next clock.
    b.set_control(0xa8, 1024);
    CHECK(!b.reading());
    CHECK_EQ(b.selected(), 8);
    CHECK_EQ(b.period(), 2048);
    CHECK_EQ(b.next_nmi(), 1088);
    // A zero divider stops it.
    b.set_control(0x10, 3000);
    CHECK(b.next_nmi() == namco_io::Bus06::kNever);
    CHECK_EQ(b.control(), 0x10);
}

// --- 51XX / 53XX -------------------------------------------------------------

// Reads the three-byte cycle once.
void cycle51(namco_io::Chip51& c, uint8_t out[3])
{
    for (int i = 0; i < 3; ++i) out[i] = c.read();
}

void test_chip51()
{
    namco_io::Chip51 c;
    c.reset();
    uint8_t r[3];
    // Switch mode (after reset): buttons, sticks, 0.
    c.set_inputs(0xa5, 0x3c);
    cycle51(c, r);
    CHECK_EQ(r[0], 0x3c);
    CHECK_EQ(r[1], 0xa5);
    CHECK_EQ(r[2], 0);

    // Coinage 1 coin 1 credit, 2 coins 1 credit on slot 2; credit mode.
    for (uint8_t b : {1, 1, 1, 2, 1, 2}) c.write(b);
    CHECK_EQ(c.mode(), 1);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    CHECK_EQ(r[0], 0x00);
    CHECK_EQ(r[1], 0x3f);                       // centred stick (raw), fire up
    CHECK_EQ(r[2], 0x3f);
    c.set_inputs(0xff, uint8_t(~0x10));         // coin 1 in
    cycle51(c, r);
    CHECK_EQ(r[0], 0x01);
    cycle51(c, r);                              // still held: no more
    CHECK_EQ(r[0], 0x01);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    c.set_inputs(0xff, uint8_t(~0x20));         // coin 2: half a credit
    cycle51(c, r);
    CHECK_EQ(r[0], 0x01);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    c.set_inputs(0xff, uint8_t(~0x20));
    cycle51(c, r);
    CHECK_EQ(r[0], 0x02);
    for (int i = 0; i < 10; ++i) {              // service credits, in BCD
        c.set_inputs(0xff, 0xff);
        cycle51(c, r);
        c.set_inputs(0xff, uint8_t(~0x40));
        cycle51(c, r);
    }
    CHECK_EQ(r[0], 0x12);
    CHECK_EQ(c.credits(), 12);

    // Start 1 takes a credit and ends credit mode; start 2 then does nothing.
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    c.set_inputs(0xff, uint8_t(~0x04));
    cycle51(c, r);
    CHECK_EQ(r[0], 0x11);
    CHECK_EQ(c.mode(), 2);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    c.set_inputs(0xff, uint8_t(~0x08));
    cycle51(c, r);
    CHECK_EQ(r[0], 0x11);
    // Back in credit mode, start 2 takes two.
    c.write(2);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    c.set_inputs(0xff, uint8_t(~0x08));
    cycle51(c, r);
    CHECK_EQ(r[0], 0x09);

    // Fire: bit 4 low on the read it was first seen, bit 5 low while held.
    c.set_inputs(uint8_t(~0x02), uint8_t(~0x01));      // player 1 right + fire
    cycle51(c, r);
    CHECK_EQ(r[1], 0x0d);                       // stick 1101, fresh, held
    cycle51(c, r);
    CHECK_EQ(r[1], 0x1d);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    CHECK_EQ(r[1], 0x3f);

    // Joystick remapping: direction numbers, 8 = centred.
    c.write(4);
    const struct { uint8_t stick; uint8_t dir; } dirs[] = {
        {0x0f, 8}, {0x0e, 0}, {0x0c, 1}, {0x0d, 2}, {0x09, 3}, {0x0b, 4}, {0x03, 5}, {0x07, 6}, {0x06, 7},
    };
    for (const auto& d : dirs) {
        c.set_inputs(uint8_t(0xf0 | d.stick), 0xff);
        cycle51(c, r);
        CHECK_EQ(r[1] & 0x0f, d.dir);
    }
    c.write(3);
    c.set_inputs(0xfe, 0xff);
    cycle51(c, r);
    CHECK_EQ(r[1] & 0x0f, 0x0e);

    // The test switch shows as BB; free play (0 coins per credit) as A0.
    c.set_inputs(0xff, uint8_t(~0x80));
    cycle51(c, r);
    CHECK_EQ(r[0], 0xbb);
    for (uint8_t b : {1, 0, 0, 0, 0, 2}) c.write(b);
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    CHECK_EQ(r[0], 0xa0);

    // The credits stop at 99.
    c.reset();
    for (uint8_t b : {1, 1, 7, 1, 1, 2}) c.write(b);
    for (int i = 0; i < 20; ++i) {
        c.set_inputs(0xff, 0xff);
        cycle51(c, r);
        c.set_inputs(0xff, uint8_t(~0x10));
        cycle51(c, r);
    }
    CHECK_EQ(c.credits(), 105);                 // 98 + 7: the last coin still counts
    c.set_inputs(0xff, 0xff);
    cycle51(c, r);
    c.set_inputs(0xff, uint8_t(~0x10));
    cycle51(c, r);
    CHECK_EQ(c.credits(), 105);

    namco_io::Chip53 d;
    d.set_dips(0x99, 0x24);
    d.reset();
    CHECK_EQ(d.read(), 0x99);
    CHECK_EQ(d.read(), 0x24);
    CHECK_EQ(d.read(), 0x99);
}

// --- 05XX ------------------------------------------------------------------------

void test_starfield()
{
    using namespace starfield05;
    static Generator g;
    g.init();
    // The period's stars, found one step at a time: 256 of them, 64 a set.
    uint16_t s = kSeed;
    int n = 0, per_set[4] = {};
    bool same = true;
    for (int i = 0; i < kPeriod; ++i) {
        if (is_star(s)) {
            if (n < kStars) {
                const Hit& h = g.hits()[n];
                same = same && h.index == i && h.set == star_set(s) && h.colour == star_colour(s);
            }
            ++per_set[star_set(s)];
            ++n;
        }
        s = step(s);
    }
    CHECK_EQ(s, kSeed);                         // maximal length
    CHECK_EQ(n, kStars);
    CHECK(same);
    for (int k = 0; k < 4; ++k) CHECK_EQ(per_set[k], 64);
    // Colour: the inverted bits 3 1 0 7 6 5 as BBGGRR (bits 2, 4, 9 and
    // 15 are 0 in a star, 11-14 are 1).
    CHECK_EQ(star_colour(0x7800), 0x3f);
    CHECK_EQ(star_colour(0x7800 | 0x0008), 0x1f);
    CHECK_EQ(star_colour(0x7800 | 0x0001), 0x37);
    CHECK_EQ(star_colour(0x7800 | 0x0020), 0x3e);
    CHECK_EQ(star_colour(0x7800 | 0x0080), 0x3b);
    CHECK_EQ(star_set(0x7800 | 0x0400), 2);
    CHECK_EQ(star_set(0x7800 | 0x0100), 1);

    // Frames against the chip run step by step: 22 lines before the 224
    // visible ones, 10 after, the scroll adding or dropping steps.
    struct Frame { uint8_t scroll, a, b; bool on; };
    const Frame frames[] = {
        {0, 0, 2, true}, {0, 0, 2, true}, {7, 1, 3, true}, {3, 1, 2, true}, {4, 0, 3, true},
        {6, 0, 2, false}, {6, 0, 2, true}, {1, 1, 3, true}, {5, 0, 2, true},
    };
    uint16_t lfsr = kSeed;
    static const int kScroll[8] = {0, 1, 2, 3, -4, -3, -2, -1};
    int frames_ok = 0, shown = 0;
    for (const Frame& f : frames) {
        g.set_controls(f.scroll, f.a, f.b, f.on);
        Star got[kMaxShown];
        const int count = g.frame(got);
        if (!f.on) {
            lfsr = kSeed;
            frames_ok += count == 0;
            continue;
        }
        std::vector<Star> want;
        for (int i = 0; i < 22 * 256 + kScroll[f.scroll]; ++i) lfsr = step(lfsr);
        for (int y = 0; y < kVisibleLines; ++y)
            for (int x = 0; x < 256; ++x) {
                if (is_star(lfsr) && star_colour(lfsr) && (star_set(lfsr) == f.a || star_set(lfsr) == f.b))
                    want.push_back({uint8_t(x), uint8_t(y), star_colour(lfsr)});
                lfsr = step(lfsr);
            }
        for (int i = 0; i < 10 * 256; ++i) lfsr = step(lfsr);
        bool ok = int(want.size()) == count;
        for (int i = 0; ok && i < count; ++i) {
            bool found = false;
            for (const Star& w : want) found = found || (w.x == got[i].x && w.y == got[i].y && w.colour == got[i].colour);
            ok = found;
        }
        frames_ok += ok;
        shown += count;
    }
    CHECK_EQ(frames_ok, int(sizeof frames / sizeof frames[0]));
    std::printf("starfield: %d stars over %d frames match the step-by-step LFSR\n", shown,
                int(sizeof frames / sizeof frames[0]));
}

// --- 54XX ----------------------------------------------------------------------------

void test_namco54()
{
    namco54::Chip c;
    c.init(24000);
    // Galaga's set-up: sound A and B parameters.
    for (uint8_t b : {0x30, 0x40, 0x00, 0x02, 0xdf, 0x40, 0x30, 0x30, 0x03, 0xdf}) c.write(b);
    CHECK_EQ(c.commands(), 10);
    CHECK_EQ(c.params(namco54::Chip::kA)[0], 0x40);
    CHECK_EQ(c.params(namco54::Chip::kA)[3], 0xdf);
    CHECK_EQ(c.params(namco54::Chip::kB)[2], 0x03);
    CHECK(!c.playing());
    std::vector<int16_t> out(24000, 0);
    c.mix(out.data(), 1000);                    // silent: nothing added
    bool zero = true;
    for (int i = 0; i < 1000; ++i) zero = zero && out[size_t(i)] == 0;
    CHECK(zero);
    // The explosion: 1x 1x 2x 2x.
    for (uint8_t b : {0x10, 0x10, 0x20, 0x20}) c.write(b);
    CHECK_EQ(c.starts(namco54::Chip::kA), 2);
    CHECK_EQ(c.starts(namco54::Chip::kB), 2);
    CHECK(c.playing());
    out.assign(24000 * 3, 0);
    c.mix(out.data(), int(out.size()));
    double early = 0, late = 0;
    int peak = 0;
    for (int i = 0; i < 2400; ++i) early += double(out[size_t(i)]) * out[size_t(i)];
    for (int i = 24000; i < 26400; ++i) late += double(out[size_t(i)]) * out[size_t(i)];
    for (int16_t v : out) peak = std::max(peak, std::abs(int(v)));
    CHECK(early > 100.0 * late);                // it dies away
    CHECK(peak > 4000 && peak < 32767);
    CHECK(!c.playing());
    // 6x takes 5 bytes, 7x sets C's volume, 5x plays C.
    for (uint8_t b : {0x60, 1, 2, 3, 4, 5, 0x73, 0x50}) c.write(b);
    CHECK_EQ(c.params(namco54::Chip::kC)[4], 5);
    CHECK_EQ(c.starts(namco54::Chip::kC), 1);
    c.reset();
    CHECK(!c.playing());
}

// --- the board, on hand-made programs -----------------------------------------------

struct GalagaRoms {
    uint8_t main[0x4000], sub[0x1000], sound[0x1000], chars[0x1000], sprites[0x2000], proms[0x220], wave[0x200];

    GalagaRoms() { std::memset(this, 0, sizeof *this); }
    galaga::Roms roms() const { return {main, sub, sound, chars, sprites, proms, wave}; }
};

// Main CPU: releases the sub CPUs, turns on both IRQs and the sound CPU's
// NMIs, then runs its test code at 0100. VBLANK IRQ: kicks the watchdog
// and counts in 8800. NMI: the 06XX transfer handler (LDI through the
// alternate registers, 0x10 to the control register after the last byte).
// Sub CPU: counts its IRQs in 8801 and sets 9201 = 2 on the 10th; idles in
// Galaga's main loop at 05B1. Sound CPU: counts its NMIs in 8802; idles in
// a JR $ at 00B9.
void load_board_programs(GalagaRoms& r)
{
    put(r.main, 0x0000, {0x31, 0x00, 0x8a,          // LD SP,8A00
                         0xed, 0x56,                // IM 1
                         0x3e, 0x01,                // LD A,1
                         0x32, 0x23, 0x68,          // LD (6823),A  - sub CPUs run
                         0x32, 0x20, 0x68,          // LD (6820),A  - main IRQ on
                         0x32, 0x21, 0x68,          // LD (6821),A  - sub IRQ on
                         0xaf,                      // XOR A
                         0x32, 0x22, 0x68,          // LD (6822),A  - sound NMI on
                         0xfb,                      // EI
                         0xc3, 0x00, 0x01});        // JP 0100
    put(r.main, 0x0038, {0xf5, 0xaf, 0x32, 0x20, 0x68,         // PUSH AF; XOR A; LD (6820),A
                         0x32, 0x30, 0x68,                     // LD (6830),A - watchdog
                         0x3a, 0x00, 0x88, 0x3c, 0x32, 0x00, 0x88,   // INC (8800)
                         0x3e, 0x01, 0x32, 0x20, 0x68,         // LD A,1; LD (6820),A
                         0xf1, 0xfb, 0xc9});                   // POP AF; EI; RET
    put(r.main, 0x0066, {0xf5, 0xd9,                // PUSH AF; EXX
                         0xed, 0xa0,                // LDI
                         0xea, 0x72, 0x00,          // JP PE,0072
                         0x3e, 0x10,                // LD A,10
                         0x32, 0x00, 0x71,          // LD (7100),A
                         0xd9, 0xf1, 0xed, 0x45});  // 0072: EXX; POP AF; RETN
    put(r.sub, 0x0000, {0x31, 0x00, 0x92, 0xed, 0x56, 0xfb, 0xc3, 0xb1, 0x05});
    put(r.sub, 0x0038, {0xf5, 0xaf, 0x32, 0x21, 0x68,          // PUSH AF; XOR A; LD (6821),A
                        0x3a, 0x01, 0x88, 0x3c, 0x32, 0x01, 0x88,    // INC (8801)
                        0xfe, 0x0a, 0x20, 0x05,                // CP 10; JR NZ,+5
                        0x3e, 0x02, 0x32, 0x01, 0x92,          // LD A,2; LD (9201),A
                        0x3e, 0x01, 0x32, 0x21, 0x68,          // LD A,1; LD (6821),A
                        0xf1, 0xfb, 0xc9});                    // POP AF; EI; RET
    put(r.sub, 0x05b1, {0x31, 0x00, 0x91, 0xc3, 0xb1, 0x05});  // LD SP,9100; JP 05B1
    put(r.sound, 0x0000, {0x31, 0x00, 0x9a, 0xc3, 0xb9, 0x00});
    put(r.sound, 0x0066, {0xf5, 0x3a, 0x02, 0x88, 0x3c, 0x32, 0x02, 0x88, 0xf1, 0xed, 0x45});
    put(r.sound, 0x00b9, {0x18, 0xfe});
}

void test_board_interrupts()
{
    static GalagaRoms r;
    load_board_programs(r);
    put(r.main, 0x0100, {0x18, 0xfe});          // JR $
    for (int speed = 0; speed < 2; ++speed) {
        static galaga::Machine m;
        m.init(r.roms(), 24000);
        m.set_speedups(speed != 0);
        int16_t audio[1024];
        int samples = 0;
        uint32_t skipped[3] = {}, run[3] = {};
        for (int f = 0; f < 30; ++f) {
            samples += m.run_frame(audio, 1024);
            for (int k = 0; k < 3; ++k) {
                skipped[k] += m.stats(k).skipped;
                run[k] += m.stats(k).run;
            }
        }
        CHECK_EQ(samples, 30 * 396);
        CHECK_EQ(m.watchdog_resets(), 0);
        CHECK_EQ(m.peek(0x8800), 30);           // one main IRQ a frame
        CHECK_EQ(m.peek(0x8801), 30);           // one sub IRQ a frame
        CHECK_EQ(m.peek(0x8802), 60);           // two sound NMIs a frame
        CHECK_EQ(m.latch() & 0x0f, 0x0b);
        if (speed) {
            // The sub and sound CPUs sleep in their main loops; the main
            // CPU's JR $ isn't a named loop.
            CHECK(skipped[1] > 30 * 45000u);
            CHECK(skipped[2] > 30 * 45000u);
            CHECK_EQ(skipped[0], 0);
            std::printf("board: cycles run with idle skipping %u / %u / %u in 30 frames\n", unsigned(run[0]),
                        unsigned(run[1]), unsigned(run[2]));
        } else {
            CHECK_EQ(skipped[0] + skipped[1] + skipped[2], 0);
        }
    }

    // The sub CPUs stay in reset until latch bit 3 is set, and go back
    // into it when it is cleared.
    static GalagaRoms h;
    load_board_programs(h);
    h.main[0x08] = 0x22;                        // LD (6822),A instead of (6823)
    put(h.main, 0x0100, {0x18, 0xfe});
    static galaga::Machine m;
    m.init(h.roms(), 24000);
    for (int f = 0; f < 5; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(m.peek(0x8800), 5);
    CHECK_EQ(m.peek(0x8801), 0);
    CHECK_EQ(m.peek(0x8802), 0);
    CHECK_EQ(m.stats(1).run + m.stats(2).run, 0);

    // Without watchdog writes the board resets after 8 frames.
    h.main[0x3d] = 0x00; h.main[0x3e] = 0x00; h.main[0x3f] = 0x00;   // no LD (6830),A
    m.init(h.roms(), 24000);
    for (int f = 0; f < 20; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(m.watchdog_resets(), 2);
}

// The 06XX through the board: the main CPU sends the 51XX its coinage and
// credit mode (6 bytes, write at divider 5), then reads the 3-byte cycle
// into 8820 over and over (divider 3), counting reads in 8810.
void test_board_06xx()
{
    static GalagaRoms r;
    load_board_programs(r);
    put(r.main, 0x0100, {0xd9, 0x21, 0x00, 0x02,     // EXX; LD HL,0200
                         0x11, 0x00, 0x70,           // LD DE,7000
                         0x01, 0x06, 0x00, 0xd9,     // LD BC,6; EXX
                         0x3e, 0xa1, 0x32, 0x00, 0x71,   // LD A,A1; LD (7100),A
                         0x3a, 0x00, 0x71, 0xfe, 0x10, 0x20, 0xf9,   // wait for 10
                         0xd9, 0x21, 0x00, 0x70,     // 0117: EXX; LD HL,7000
                         0x11, 0x20, 0x88,           // LD DE,8820
                         0x01, 0x03, 0x00, 0xd9,     // LD BC,3; EXX
                         0x3e, 0x71, 0x32, 0x00, 0x71,   // LD A,71; LD (7100),A
                         0x3a, 0x00, 0x71, 0xfe, 0x10, 0x20, 0xf9,   // wait for 10
                         0x3a, 0x10, 0x88, 0x3c, 0x32, 0x10, 0x88,   // INC (8810)
                         0x18, 0xe0});               // JR 0117
    put(r.main, 0x0200, {0x01, 0x01, 0x01, 0x01, 0x01, 0x02});
    static galaga::Machine m;
    m.init(r.roms(), 24000);
    for (int f = 0; f < 4; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(m.chip51().mode(), 1);
    CHECK(m.peek(0x8810) > 20);                 // reads go round (3 NMIs ~1.6K cycles each)
    CHECK_EQ(m.peek(0x8820), 0x00);
    CHECK_EQ(m.peek(0x8821), 0x3f);
    m.set_inputs(namco_galaga::kCoin);
    m.run_frame(nullptr, 0);
    m.set_inputs(namco_galaga::kFire | namco_galaga::kLeft);
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.peek(0x8820), 0x01);
    CHECK_EQ(m.peek(0x8821) & 0x2f, 0x07);      // left (0111), fire held
    m.set_inputs(0);
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.peek(0x8821), 0x3f);
    CHECK_EQ(m.watchdog_resets(), 0);
}

// A polling loop: the main CPU waits in Galaga's attract-mode loop at 038D
// for 9201 to leave 1 (the sub CPU sets it on its 10th IRQ), then notes its
// own IRQ count. The same with and without skipping, and skipped meanwhile.
void test_board_speedup()
{
    static GalagaRoms r;
    load_board_programs(r);
    put(r.main, 0x0100, {0x3e, 0x01, 0x32, 0x01, 0x92, 0xc3, 0x8d, 0x03});   // LD (9201),1; JP 038D
    put(r.main, 0x038d, {0x3a, 0x01, 0x92, 0x3d, 0x28, 0xfa,                 // LD A,(9201); DEC A; JR Z
                         0x3a, 0x00, 0x88, 0x32, 0x10, 0x88, 0x18, 0xfe});   // (8810) = (8800); JR $
    uint8_t seen[2] = {};
    uint32_t skipped_main[2] = {};
    for (int speed = 0; speed < 2; ++speed) {
        static galaga::Machine m;
        m.init(r.roms(), 24000);
        m.set_speedups(speed != 0);
        for (int f = 0; f < 16; ++f) {
            m.run_frame(nullptr, 0);
            if (f < 8) skipped_main[speed] += m.stats(0).skipped;
        }
        seen[speed] = m.peek(0x8810);
        CHECK_EQ(m.watchdog_resets(), 0);
    }
    CHECK_EQ(seen[0], 10);
    CHECK_EQ(seen[1], seen[0]);
    CHECK_EQ(skipped_main[0], 0);
    CHECK(skipped_main[1] > 8 * 40000u);
}

// --- video ------------------------------------------------------------------------------

constexpr uint16_t kBlue = 0x001f, kGreen = 0x07e0;

void test_galaga_video()
{
    static GalagaRoms r;
    load_board_programs(r);
    put(r.main, 0x0100, {0x18, 0xfe});
    // Palette: 1 red, 3 blue (sprites, entries 0-15), 19 blue (characters).
    r.proms[1] = 0x07; r.proms[3] = 0xc0; r.proms[19] = 0xc0;
    // Colour 0 transparent everywhere; colour 1 = pens 15 (clear) 1 2 3.
    for (int p = 0; p < 4; ++p) {
        r.proms[0x20 + p] = r.proms[0x120 + p] = 0x0f;
        r.proms[0x20 + 4 + p] = r.proms[0x120 + 4 + p] = uint8_t(p ? p : 0x0f);
    }
    std::memset(r.chars + 16, 0xff, 16);        // character 1: all pixel 3
    std::memset(r.sprites, 0xff, 4 * 64);       // sprites 0-3: all pixel 3
    static galaga::Machine m;
    m.init(r.roms(), 24000);
    m.run_frame(nullptr, 0);
    auto& c = m.cpu(namco_galaga::kMain);
    c.wr(0x8000 + 0x3a0, 1); c.wr(0x8400 + 0x3a0, 1);   // row 2, column 0
    // Sprite 0: code 0, colour 1, hardware x 100, y 105: 2 x 2 (tall + wide).
    c.wr(0x8b80, 0); c.wr(0x8b81, 1);
    c.wr(0x9380, 120); c.wr(0x9381, 140);
    c.wr(0x9b80, 0x0c); c.wr(0x9b81, 0);
    m.run_frame(nullptr, 0);
    uint16_t line[224];
    m.render_line(16, line);
    CHECK_EQ(line[0], kBlue);
    CHECK_EQ(line[7], kBlue);
    CHECK_EQ(line[8], 0);
    m.render_line(15, line);
    CHECK_EQ(line[0], 0);
    // y = 256 - 120 + 1 - 16 - 32 = 89: display x 224 - 16 - 89 = 119 for
    // the top row of tiles, 103 for the bottom; lines 100-131.
    m.render_line(100, line);
    CHECK_EQ(line[103], kBlue);
    CHECK_EQ(line[134], kBlue);
    CHECK_EQ(line[102], 0);
    CHECK_EQ(line[135], 0);
    m.render_line(131, line);
    CHECK_EQ(line[110], kBlue);
    m.render_line(99, line);
    CHECK_EQ(line[110], 0);
    m.render_line(132, line);
    CHECK_EQ(line[110], 0);
    CHECK_EQ(m.video().sprite_count, 1);

    // Stars: on with latch bit 5, from the next frame, never over the
    // score lines.
    CHECK_EQ(m.video().star_first[galaga::kScreenH], 0);
    c.wr(0xa005, 1);
    m.run_frame(nullptr, 0);
    m.run_frame(nullptr, 0);
    const int stars = m.video().star_first[galaga::kScreenH];
    CHECK(stars > 40 && stars <= starfield05::kMaxShown);
    CHECK_EQ(m.video().star_first[16], 0);
    CHECK_EQ(m.video().star_first[272], stars);
    int lit = 0;
    for (int y = 16; y < 272; ++y) {
        m.render_line(y, line);
        for (int x = 0; x < 224; ++x) lit += line[x] != 0 && !(y >= 100 && y < 132 && x >= 103 && x < 135) &&
                                              !(y >= 16 && y < 24 && x < 8);
    }
    CHECK(lit > 0 && lit <= stars);
}

struct DigdugRoms {
    uint8_t main[0x4000], sub[0x2000], sound[0x1000], text[0x800], sprites[0x4000], tiles[0x1000], map[0x1000],
        proms[0x220], wave[0x200];

    DigdugRoms() { std::memset(this, 0, sizeof *this); }
    digdug::Roms roms() const { return {main, sub, sound, text, sprites, tiles, map, proms, wave}; }
};

void test_digdug_video()
{
    static DigdugRoms r;
    put(r.main, 0, {0x3e, 0x01, 0x32, 0x30, 0x68, 0x18, 0xf9});  // LD A,1; LD (6830),A; JR 0
    // Palette: 1 red, 2 green, 3 blue; 19 blue for the sprites.
    r.proms[1] = 0x07; r.proms[2] = 0x38; r.proms[3] = 0xc0; r.proms[19] = 0xc0;
    // Playfield colour 1 = pens 0 1 2 3; colour 0 all pen 0 (black).
    for (int p = 0; p < 4; ++p) r.proms[0x120 + 4 + p] = uint8_t(p);
    // Sprite colour 1 = pens 15 (clear) 1 2 3 -> entries 16 + n.
    for (int p = 0; p < 4; ++p) r.proms[0x20 + p] = 0x0f;
    for (int p = 0; p < 4; ++p) r.proms[0x20 + 4 + p] = uint8_t(p ? p : 0x0f);
    std::memset(r.tiles + 0x11 * 16, 0xff, 16); // playfield tile 11: all pixel 3
    std::memset(r.text + 0x21 * 8, 0xff, 8);    // text character 21: solid
    std::memset(r.sprites + 64, 0xff, 64);      // sprite 1: all pixel 3
    // Picture 0: tile 11 (colour 1 from its top bits) at row 2, column 0
    // (offset 3A0).
    r.map[0x3a0] = 0x11;
    static digdug::Machine m;
    m.init(r.roms(), 24000);
    auto& c = m.cpu(namco_galaga::kMain);
    // Text character 21 at row 3, column 0 (offset 3A1); code bits 5-7 / 4
    // give the colour: 0x21 -> colour 2 (green).
    c.wr(0x8000 + 0x3a1, 0x21);
    // Sprite 0: code 1, colour 1, hardware x 250 (wraps), y 105.
    c.wr(0x8b80, 1); c.wr(0x8b81, 1);
    c.wr(0x9380, 120); c.wr(0x9381, uint8_t(250 + 40 - 1));
    m.run_frame(nullptr, 0);
    uint16_t line[224];
    m.render_line(16, line);
    CHECK_EQ(line[0], kBlue);
    CHECK_EQ(line[8], 0);
    m.render_line(24, line);
    CHECK_EQ(line[0], kGreen);                  // green text
    CHECK_EQ(line[8], 0);
    // Sprite at y 105 -> display x 103-118, lines 250-265.
    m.render_line(250, line);
    CHECK_EQ(line[103], kBlue);
    CHECK_EQ(line[118], kBlue);
    CHECK_EQ(line[119], 0);
    m.render_line(265, line);
    CHECK_EQ(line[110], kBlue);
    m.render_line(249, line);
    CHECK_EQ(line[110], 0);
    // At x 10 + 256 it shows on lines 266-271 and, wrapped, on 10-25 -
    // of which only 16-25: sprites stay off the score lines.
    c.wr(0x9381, uint8_t(10 + 40 - 1));
    m.run_frame(nullptr, 0);
    m.render_line(10, line);
    CHECK_EQ(line[110], 0);
    m.render_line(16, line);
    CHECK_EQ(line[110], kBlue);
    m.render_line(25, line);
    CHECK_EQ(line[110], kBlue);
    m.render_line(26, line);
    CHECK_EQ(line[110], 0);
    m.render_line(266, line);
    CHECK_EQ(line[110], kBlue);
    m.render_line(271, line);
    CHECK_EQ(line[110], kBlue);
    m.render_line(272, line);
    CHECK_EQ(line[110], 0);

    // The playfield "off" bit draws it in colour 15.
    c.wr(0xa003, 1);
    m.run_frame(nullptr, 0);
    m.render_line(16, line);
    CHECK_EQ(line[0], 0);
    CHECK_EQ(m.video_latch(), 0x08);

    // EAROM (control: bit 0 clock, bit 1 set = write / erase, bit 2 =
    // erase, bit 3 = select): erase, write, then read back on a falling
    // clock in read mode.
    CHECK_EQ(m.earom()[5], 0xff);
    c.wr(0xb805, 0xa5);                         // address 5, data A5
    c.wr(0xb840, 0x08 | 0x02 | 0x04);           // erase
    CHECK_EQ(m.earom()[5], 0xff);
    c.wr(0xb840, 0x08 | 0x02);                  // write
    CHECK_EQ(m.earom()[5], 0xa5);
    c.wr(0xb805, 0x0f);                         // unerased: the cell takes the AND
    c.wr(0xb840, 0x08 | 0x02 | 0x01);
    c.wr(0xb840, 0x08 | 0x02);
    CHECK_EQ(m.earom()[5], 0x05);
    c.wr(0xb840, 0x08 | 0x01);                  // read mode, clock high
    c.wr(0xb840, 0x08);                         // clock falls
    CHECK_EQ(c.rd(0xb800), 0x05);
    c.wr(0xb840, 0x00);                         // deselected: nothing changes
    c.wr(0xb840, 0x02 | 0x04);
    CHECK_EQ(m.earom()[5], 0x05);
}

} // namespace

int main()
{
    test_bus06();
    test_chip51();
    test_starfield();
    test_namco54();
    test_board_interrupts();
    test_board_06xx();
    test_board_speedup();
    test_galaga_video();
    test_digdug_video();
    std::printf("namco tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
