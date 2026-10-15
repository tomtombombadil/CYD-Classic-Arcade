// Host tests (no ROMs) for the Z80's unprefixed opcode times, the SN76489
// sound chip, Ms. Pac-Man's aux board
// (address / data wiring, patch windows, the decode latch and its trap
// windows, the VBLANK vector fetch that sets it) and the Mr. Do! machine
// (interrupt held until taken, the protection read, inputs, sound
// writes, both tile layers with scroll and cover bits, sprites with flips
// and priority) on hand-made programs and synthetic graphics.
//
//   mspacman_mrdo_tests
#include "../../emu/cpu/z80.h"
#include "../../emu/machines/mrdo.h"
#include "../../emu/machines/mspacman.h"
#include "../../emu/sound/sn76489.h"

#include <cmath>
#include <cstdio>
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

// --- Z80: every unprefixed opcode's T-states ---------------------------------------

// Zilog's documented times; conditions true (flags clear: NZ NC PO P), B = 2
// (DJNZ taken). -1 = a prefix or HALT.
constexpr int8_t kZ80Times[256] = {
     4, 10,  7,  6,  4,  4,  7,  4,  4, 11,  7,  6,  4,  4,  7,  4,
    13, 10,  7,  6,  4,  4,  7,  4, 12, 11,  7,  6,  4,  4,  7,  4,
    12, 10, 16,  6,  4,  4,  7,  4,  7, 11, 16,  6,  4,  4,  7,  4,
    12, 10, 13,  6, 11, 11, 10,  4,  7, 11, 13,  6,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     7,  7,  7,  7,  7,  7, -1,  7,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
     4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
    11, 10, 10, 10, 17, 11,  7, 11,  5, 10, 10, -1, 10, 17,  7, 11,
    11, 10, 10, 11, 17, 11,  7, 11,  5,  4, 10, 11, 10, -1,  7, 11,
    11, 10, 10, 19, 17, 11,  7, 11,  5,  4, 10,  4, 10, -1,  7, 11,
    11, 10, 10,  4, 17, 11,  7, 11,  5,  6, 10,  4, 10, -1,  7, 11,
};

void test_z80_times()
{
    static uint8_t mem[0x10000];
    for (int op = 0; op < 256; ++op) {
        if (kZ80Times[op] < 0) continue;
        std::memset(mem, 0, sizeof mem);
        mem[0] = uint8_t(op);
        z80::Cpu c;
        c.map_ram(0, 0x10000, mem);
        c.reset();
        c.sp.w = 0x8000;
        c.af.b.l = 0;
        c.bc.w = 0x0200;
        c.hl.w = 0x9000;
        const int t = c.run(1);
        if (t != kZ80Times[op]) std::printf("  Z80 opcode %02x: %d T-states, want %d\n", op, t, kZ80Times[op]);
        CHECK_EQ(t, kZ80Times[op]);
    }
}

// --- SN76489 -----------------------------------------------------------------

// A clock of 4.096 MHz = 256,000 ticks a second; at 8,000 samples a second
// a sample is 32 ticks (one noise shift at rate 0), at 24,000 10.67.
constexpr uint32_t kClock = 4096000;

std::vector<int32_t> mix(sn76489::Chip& c, int n)
{
    std::vector<int32_t> v(size_t(n), 0);
    c.mix(v.data(), n);
    return v;
}

void test_sn76489()
{
    sn76489::Chip c;
    c.init(kClock, 24000, 1000);
    CHECK_EQ(c.ticks_per_second(), 256000);
    // Silent after reset.
    for (int32_t s : mix(c, 2400)) CHECK_EQ(s, 0);
    CHECK_EQ(c.attenuation(0), 15);
    CHECK_EQ(c.period(0), 0x400);

    // Tone 0, period 64 = 128 ticks a half wave = 2000 Hz.
    c.write(0x80 | 0x0);            // latch tone 0, low bits 0
    c.write(0x04);                  // high bits: 0x04 << 4 = 64
    CHECK_EQ(c.period(0), 64);
    c.write(0x90);                  // tone 0 attenuation 0
    CHECK_EQ(c.attenuation(0), 0);
    {
        const std::vector<int32_t> v = mix(c, 24000);
        int rises = 0, peak = 0;
        for (size_t i = 1; i < v.size(); ++i) {
            if (v[i] > 500 && v[i - 1] <= 500) ++rises;
            if (v[i] > peak) peak = v[i];
        }
        CHECK(rises >= 1999 && rises <= 2001);
        CHECK_EQ(peak, 1000);
    }
    // Attenuation: 2 dB a step, 15 = off.
    c.write(0x91);
    {
        int peak = 0;
        for (int32_t s : mix(c, 2400)) if (s > peak) peak = s;
        CHECK_EQ(peak, 794);        // 1000 x 10^(-2/20)
    }
    c.write(0x9f);
    for (int32_t s : mix(c, 240)) CHECK_EQ(s, 0);

    // Period 0 counts as 1024: 125 Hz. Low bits from the latch byte.
    c.write(0x80); c.write(0x00); c.write(0x90);
    CHECK_EQ(c.period(0), 0x400);
    {
        const std::vector<int32_t> v = mix(c, 24000);
        int rises = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if (v[i] > 500 && v[i - 1] <= 500) ++rises;
        CHECK(rises >= 124 && rises <= 126);
    }
    c.write(0x8f); c.write(0x3f);
    CHECK_EQ(c.period(0), 0x3ff);
    c.write(0xa5);                  // latch tone 1, low bits 5; a data byte then sets its high bits
    c.write(0x01);
    CHECK_EQ(c.period(1), 0x15);
    CHECK_EQ(c.period(0), 0x3ff);
    // A data byte after an attenuation latch sets the attenuation.
    c.write(0xb0); c.write(0x07);
    CHECK_EQ(c.attenuation(1), 7);
    c.write(0x9f); c.write(0xbf);

    // A tone far above the output rate averages to half its level. (A new
    // period starts when the running count ends: up to 1024 ticks here.)
    c.write(0xc1); c.write(0x00); c.write(0xd0);     // tone 2, period 1: 128 kHz
    {
        const std::vector<int32_t> v = mix(c, 240);
        for (size_t i = 100; i < v.size(); ++i) CHECK(std::abs(v[i] - 500) <= 50);
    }
    c.write(0xdf);

    // Noise: writing its control resets the register to 0x4000.
    sn76489::Chip n;
    n.init(kClock, 8000, 1000);
    n.write(0xe0);                  // periodic, rate 0 (a shift every 32 ticks)
    CHECK_EQ(n.noise_register(), 0x4000);
    CHECK_EQ(n.noise_control(), 0);
    n.write(0xf0);                  // noise attenuation 0
    {
        // One shift a sample: the output is 1 for one shift in every 15.
        const std::vector<int32_t> v = mix(n, 15 * 40);
        int ones = 0;
        for (int32_t s : v) {
            CHECK(s == 0 || s == 1000);
            ones += s == 1000;
        }
        CHECK_EQ(ones, 40);
    }
    // White noise: the register comes back to 0x4000 after 32767 shifts.
    n.write(0xe4);
    CHECK_EQ(n.noise_control(), 4);
    {
        int first = 0;
        for (int i = 1; i <= 40000 && !first; ++i) {
            mix(n, 1);
            if (n.noise_register() == 0x4000) first = i;
        }
        CHECK_EQ(first, 32767);
    }
    // Rate 3: shifts at half tone 2's rate. Tone 2 period 10: a shift every
    // 20 ticks; periodic noise pulses once every 15 shifts = 300 ticks.
    sn76489::Chip q;
    q.init(kClock, 24000, 1000);
    q.write(0xc0 | 0x0a); q.write(0x00);
    q.write(0xe3);
    q.write(0xf0);
    {
        const std::vector<int32_t> v = mix(q, 24000);
        int rises = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if (v[i] > 0 && v[i - 1] == 0) ++rises;
        CHECK(rises >= 852 && rises <= 854);    // 256000 / 300
    }
}

// --- Ms. Pac-Man -----------------------------------------------------------------

// The byte a ROM must hold for the Z80 to read `v` through the aux board.
uint8_t encode_data(uint8_t v)
{
    for (int raw = 0; raw < 256; ++raw)
        if (mspacman::decode_data(uint8_t(raw)) == v) return uint8_t(raw);
    return 0;
}

void test_mspacman_wiring()
{
    // Every wiring is one-to-one.
    {
        std::vector<int> seen(256, 0);
        for (int raw = 0; raw < 256; ++raw) ++seen[mspacman::decode_data(uint8_t(raw))];
        bool ok = true;
        for (int s : seen) ok = ok && s == 1;
        CHECK(ok);
        std::vector<int> a5(0x800, 0), a67(0x1000, 0);
        for (int i = 0; i < 0x800; ++i) ++a5[mspacman::u5_address(uint16_t(i))];
        for (int i = 0; i < 0x1000; ++i) ++a67[mspacman::u67_address(uint16_t(i))];
        ok = true;
        for (int s : a5) ok = ok && s == 1;
        for (int s : a67) ok = ok && s == 1;
        CHECK(ok);
    }
    // Some lines by hand: Z80 D7 reads ROM D0, D4 reads D7, D0 reads D1.
    CHECK_EQ(mspacman::decode_data(0x01), 0x80);
    CHECK_EQ(mspacman::decode_data(0x80), 0x10);
    CHECK_EQ(mspacman::decode_data(0x02), 0x01);
    // Z80 A3 drives U6 / U7's A10 and U5's A4; A10 drives U5's A6.
    CHECK_EQ(mspacman::u67_address(0x008), 0x400);
    CHECK_EQ(mspacman::u5_address(0x008), 0x010);
    CHECK_EQ(mspacman::u5_address(0x400), 0x040);
    CHECK_EQ(mspacman::u67_address(0x800), 0x800);
    CHECK_EQ(mspacman::u67_address(0x007), 0x007);
    // The patch windows: forty, 8 bytes each, inside 0000-2FFF and aux
    // 8000-81EF, no two overlapping.
    {
        bool ok = true;
        for (int i = 0; i < mspacman::kPatchCount; ++i) {
            const mspacman::Patch& p = mspacman::kPatches[i];
            ok = ok && p.addr < 0x3000 && (p.addr & 7) == 0 && p.aux + 8 <= 0x1f0 && (p.aux & 7) == 0;
            for (int j = 0; j < i; ++j) ok = ok && mspacman::kPatches[j].addr != p.addr;
        }
        CHECK(ok);
    }
}

// Pac-Man ROM bytes: a pattern, with a program at 0000.
uint8_t pac_byte(int a) { return uint8_t((a * 7 + (a >> 8)) ^ 0x5a); }
// What the aux ROMs show the Z80 at aux 8000 + i (U5, U6) and 3000 + i (U7).
uint8_t aux_byte(int i) { return uint8_t((i * 13 + (i >> 7)) ^ 0xc3); }
uint8_t u7_byte(int i) { return uint8_t((i * 5 + (i >> 9)) ^ 0x96); }

void test_mspacman_machine()
{
    static uint8_t cpu[0x4000], aux[mspacman::kAuxSize], gfx[0x2000], proms[0x120], sound[0x200];
    for (int a = 0; a < 0x4000; ++a) cpu[a] = pac_byte(a);
    // Program: IM 2 with I = 3F and vector FC, so the VBLANK interrupt
    // fetches its address from 3FFC - a trap window that sets the latch.
    const uint8_t prog[] = {
        0xf3,                   // DI
        0x31, 0xc0, 0x4f,       // LD SP,4FC0
        0xed, 0x5e,             // IM 2
        0x3e, 0x3f, 0xed, 0x47, // LD A,3F; LD I,A
        0x3e, 0xfc, 0xd3, 0x00, // LD A,FC; OUT (0),A
        0x3e, 0x01,             // LD A,1
        0x32, 0x00, 0x50,       // LD (5000),A        - IRQ enable
        0xfb,                   // EI
        0x32, 0xc0, 0x50,       // loop: LD (50C0),A  - watchdog
        0x3a, 0x38, 0x00,       // LD A,(0038)        - trap: latch off
        0x3e, 0x01,             // LD A,1
        0x18, 0xf6,             // JR loop
    };
    std::memcpy(cpu, prog, sizeof prog);
    // ISR at 0200 (latch on) counts at 4C00; at 0300 (latch off) at 4C01.
    auto isr = [&](int at, uint8_t lo) {
        const uint8_t code[] = {
            0xf5, 0xaf, 0x32, 0x00, 0x50,   // PUSH AF; XOR A; LD (5000),A
            0x21, lo, 0x4c, 0x34,           // LD HL,4C0x; INC (HL)
            0x3e, 0x01, 0x32, 0x00, 0x50,   // LD A,1; LD (5000),A
            0xf1, 0xfb, 0xed, 0x4d,         // POP AF; EI; RETI
        };
        std::memcpy(cpu + at, code, sizeof code);
    };
    isr(0x200, 0x00);
    isr(0x300, 0x01);
    cpu[0x3ffc] = 0x00; cpu[0x3ffd] = 0x03;         // Pac-Man's vector -> 0300

    // Aux ROMs encoded so the Z80 reads aux_byte / u7_byte; U7's 0FFC-0FFD
    // (Z80 3FFC) hold the vector 0200.
    for (int i = 0; i < 0x800; ++i) aux[mspacman::u5_address(uint16_t(i))] = encode_data(aux_byte(i));
    for (int i = 0; i < 0x1000; ++i) {
        aux[0x800 + (mspacman::u67_address(uint16_t(i)) ^ 0x800)] = encode_data(aux_byte(0x800 + i));
        uint8_t v = u7_byte(i);
        if (i == 0xffc) v = 0x00;
        if (i == 0xffd) v = 0x02;
        aux[0x1800 + mspacman::u67_address(uint16_t(i))] = encode_data(v);
    }

    static mspacman::Machine m;
    m.init({cpu, gfx, proms, sound}, aux, 24000);
    z80::Cpu& c = m.cpu();

    // Latch on: U7 at 3000, U5 / U6 at 8000, Pac-Man above them.
    c.rd(0x3ff9);
    CHECK(m.decode_on());
    CHECK_EQ(c.rd(0x3000), u7_byte(0));
    CHECK_EQ(c.rd(0x3abc), u7_byte(0xabc));
    CHECK_EQ(c.rd(0x8123), aux_byte(0x123));
    CHECK_EQ(c.rd(0x8800), aux_byte(0x800));
    CHECK_EQ(c.rd(0x9234), aux_byte(0x1234));
    CHECK_EQ(c.rd(0x9800), pac_byte(0x1800));
    CHECK_EQ(c.rd(0xa000), pac_byte(0x2000));
    CHECK_EQ(c.rd(0xbfff), pac_byte(0x3fff));
    CHECK(m.decode_on());
    // Patch windows read the aux ROM, the bytes around them Pac-Man's.
    for (int i = 0; i < mspacman::kPatchCount; ++i) {
        const mspacman::Patch& p = mspacman::kPatches[i];
        for (int k = 0; k < 8; ++k) CHECK_EQ(c.rd(uint16_t(p.addr + k)), aux_byte(p.aux + k));
    }
    CHECK_EQ(c.rd(0x0418), pac_byte(0x418));
    CHECK_EQ(c.rd(0x040f), pac_byte(0x40f));
    CHECK_EQ(c.rd(0x1c00), pac_byte(0x1c00));

    // A read in a clearing window: Pac-Man's ROMs, mirrored at 8000.
    CHECK_EQ(c.rd(0x003a), pac_byte(0x3a));
    CHECK(!m.decode_on());
    CHECK_EQ(c.rd(0x0410), pac_byte(0x410));
    CHECK_EQ(c.rd(0x3000), pac_byte(0x3000));
    CHECK_EQ(c.rd(0x8800), pac_byte(0x0800));
    CHECK_EQ(c.rd(0xb123), pac_byte(0x3123));
    // Writes count too; each window works.
    c.wr(0x3ffe, 0);
    CHECK(m.decode_on());
    const uint16_t clear[] = {0x0038, 0x03b0, 0x1600, 0x3ff0, 0x8000, 0x97f0};
    for (uint16_t a : clear) {
        c.rd(0x3ff8);
        CHECK(m.decode_on());
        c.rd(uint16_t(a + 7));
        CHECK(!m.decode_on());
    }
    c.rd(0x3fff);
    CHECK_EQ(c.rd(0x8005), cpu[0x0005]);            // the window's own read is after the switch
    CHECK(!m.decode_on());
    c.rd(0x3ff8);
    CHECK_EQ(c.rd(0x97f7), pac_byte(0x17f7));
    c.rd(0x3ff8);
    CHECK_EQ(c.rd(0x1607), pac_byte(0x1607));
    CHECK_EQ(c.rd(0x16b0), pac_byte(0x16b0));       // a patch window, but the latch is off now
    c.rd(0x3ff8);
    c.rd(0x2120);                                   // not emulated (see mspacman.cpp)
    CHECK(m.decode_on());
    c.rd(0x3ff8);
    CHECK_EQ(c.rd(0x97ef), aux_byte(0x17ef));       // just before the window: aux
    // The Pac-Man board is still there: RAM (and its mirror), DIPs.
    c.wr(0x4c10, 0x42);
    CHECK_EQ(c.rd(0x4c10), 0x42);
    CHECK_EQ(c.rd(0xcc10), 0x42);
    CHECK_EQ(c.rd(0x5080), pacman::kDipDefault);
    CHECK_EQ(c.rd(0x4800), 0xbf);

    // Running: the main loop clears the latch every pass, and every VBLANK
    // vector fetch (3FFC) sets it again before the vector is read, so the
    // interrupt always goes to the Ms. Pac-Man vector (0200).
    m.reset();
    for (int f = 0; f < 20; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x4c00), 20);
    CHECK_EQ(c.rd(0x4c01), 0);
    CHECK_EQ(m.watchdog_resets(), 0);
}

// --- Mr. Do! -----------------------------------------------------------------------

void test_mrdo_level()
{
    CHECK_EQ(mrdo::level(0), 0);
    CHECK_EQ(mrdo::level(15), 255);
    CHECK_EQ(mrdo::level(1), 0);            // 150 ohm alone stays under the diode's drop
    for (int b = 1; b < 16; ++b) CHECK(mrdo::level(b) >= mrdo::level(b & (b - 1)));
    CHECK(mrdo::level(8) > mrdo::level(4));
}

void test_mrdo_machine()
{
    static uint8_t cpu[0x8000], fg[0x2000], bg[0x2000], spr[0x2000], proms[0x80];
    std::memset(cpu, 0, sizeof cpu);
    // main (0100): SP, interrupts off for a while, protection read, a
    // tone on chip 1, interrupts on, wait in JR $. The interrupt (0038)
    // counts at E000 and starts main again, as the game's does.
    const uint8_t main_code[] = {
        0x31, 0x00, 0xf0,       // LD SP,F000
        0xf3,                   // DI
        0x06, 0x00,             // LD B,0
        0x10, 0xfe,             // DJNZ $ (256 loops, ~3300 T)
        0x21, 0x23, 0x01,       // LD HL,0123
        0x3a, 0x03, 0x98,       // LD A,(9803)
        0x32, 0x01, 0xe0,       // LD (E001),A
        0x3e, 0x83, 0x32, 0x01, 0x98,   // tone 0 low bits 3
        0x3e, 0x02, 0x32, 0x01, 0x98,   // high bits 2 -> period 0x23
        0x3e, 0x90, 0x32, 0x01, 0x98,   // attenuation 0
        0xfb,                   // EI
        0x18, 0xfe,             // JR $
    };
    std::memcpy(cpu + 0x100, main_code, sizeof main_code);
    const uint8_t start[] = {0xed, 0x56, 0xc3, 0x00, 0x01};   // IM 1; JP 0100
    std::memcpy(cpu, start, sizeof start);
    const uint8_t isr[] = {0x21, 0x00, 0xe0, 0x34, 0xc3, 0x00, 0x01};   // LD HL,E000; INC (HL); JP 0100
    std::memcpy(cpu + 0x38, isr, sizeof isr);
    cpu[0x123] = 0x5a;

    // Colours: tile colour 1 pixel 3 (entry 7) red; tile colour 3 pixel 0
    // (entry 12) green only; sprite colour 2 pixel 3 -> lookup 1 -> entry 1
    // blue; entry 0 black.
    std::memset(proms, 0, sizeof proms);
    proms[0x00 + 7] = 0x03; proms[0x20 + 3] = 0x03;     // entry 7: red bits all on
    proms[0x00 + 12] = 0x0c;                            // entry 12: green high bits
    proms[0x00 + 1] = 0x30; proms[0x20 + 1] = 0x30;     // entry 1: blue bits all on
    proms[0x40 + 11] = 0x01;                            // sprite colour 2 pixel 3 -> entry 1
    proms[0x40 + 7] = 0x07;                             // sprite colour 1 pixel 3 -> entry 39: some red
    // Tiles: front tile 1 and tile 0x101 all pixel value 3; back tile 2
    // only board pixel (0, 0), value 3.
    std::memset(fg, 0, sizeof fg); std::memset(bg, 0, sizeof bg); std::memset(spr, 0, sizeof spr);
    std::memset(fg + 8, 0xff, 8); std::memset(fg + 0x1008, 0xff, 8);
    std::memset(fg + 0x808, 0xff, 8); std::memset(fg + 0x1808, 0xff, 8);
    bg[16] = 0x01; bg[0x1000 + 16] = 0x01;
    std::memset(bg + 8, 0xff, 8); std::memset(bg + 0x1008, 0xff, 8);   // back tile 1: all 3
    // Sprites: 1 all pixel value 3; 2 only (0, 0).
    std::memset(spr + 64, 0xff, 64);
    spr[128] = 0x11;

    static mrdo::Machine m;
    m.init({cpu, fg, bg, spr, proms}, 24000);
    int16_t audio[1024];
    int samples = 0, loud = 0;
    for (int f = 0; f < 30; ++f) {
        const int n = m.run_frame(audio, 1024);
        samples += n;
        for (int i = 0; i < n; ++i) loud = std::max(loud, std::abs(int(audio[i])));
    }
    z80::Cpu& c = m.cpu();
    CHECK(samples >= 30 * 400 && samples <= 30 * 401);  // 24000 Hz / 59.94 Hz
    // One interrupt a frame: the line drops once taken, though main turns
    // interrupts back on long before the frame ends.
    CHECK_EQ(c.rd(0xe000), 30);
    CHECK_EQ(c.rd(0xe001), 0x5a);                       // the protection read: (HL)
    CHECK_EQ(m.protection_reads(), 31);                 // main runs twice in the first frame
    CHECK(m.idle_cycles() > 30 * 50000);                // the JR $ wait was skipped
    CHECK_EQ(m.sound(0).period(0), 0x23);
    CHECK_EQ(m.sound(0).attenuation(0), 0);
    CHECK_EQ(m.sound(1).attenuation(0), 15);
    CHECK(loud > 2000);

    // Inputs and DIPs.
    m.set_inputs(mrdo::kLeft | mrdo::kFire | mrdo::kStart1);
    CHECK_EQ(c.rd(0xa000), 0xff & ~0x31);
    m.set_inputs(mrdo::kCoin | mrdo::kUp);
    CHECK_EQ(c.rd(0xa000), 0xff & ~0x08);
    CHECK_EQ(c.rd(0xa001), 0xff & ~0x40);
    m.set_inputs(0);
    CHECK_EQ(c.rd(0xa002), mrdo::kDipDefault);
    CHECK_EQ(c.rd(0xa003), 0xff);

    // Video. Display line y = board column 247 - y, pixel x = board row 32 + x.
    const uint16_t red = 0xf800, blue = 0x001f, black = 0;
    // Front tile 1 colour 1 at row 4, column 30: board (240-247, 32-39).
    c.wr(0x8800 + 4 * 32 + 30, 0x01); c.wr(0x8c00 + 4 * 32 + 30, 1);
    // Front tile 0x101 (attribute bit 7) at row 27, column 1: board (8-15, 216-223).
    c.wr(0x8800 + 27 * 32 + 1, 0x81); c.wr(0x8c00 + 27 * 32 + 1, 1);
    // Back tile 0 colour 3 covering at row 10, column 20 (green); the same
    // without the cover bit at row 11 shows the black behind it.
    c.wr(0x8000 + 10 * 32 + 20, 0x43); c.wr(0x8400 + 10 * 32 + 20, 0);
    c.wr(0x8000 + 11 * 32 + 20, 0x03); c.wr(0x8400 + 11 * 32 + 20, 0);
    // Back tile 2 colour 1 at row 6, column 25: one red pixel at board (200, 48).
    c.wr(0x8000 + 6 * 32 + 25, 0x01); c.wr(0x8400 + 6 * 32 + 25, 2);
    // Sprite 0: code 1 colour 2 at board (100, 100); sprite 1 under it 4
    // pixels on, code 1 colour 1; sprite 2: code 2 at (60, 150) flipped
    // both ways.
    c.wr(0x9000, 1); c.wr(0x9001, 156); c.wr(0x9002, 0x02); c.wr(0x9003, 100);
    c.wr(0x9004, 1); c.wr(0x9005, 156); c.wr(0x9006, 0x01); c.wr(0x9007, 104);
    c.wr(0x9008, 2); c.wr(0x9009, 106); c.wr(0x900a, 0x32); c.wr(0x900b, 60);
    m.run_frame(nullptr, 0);

    uint16_t line[mrdo::kScreenW];
    m.render_line(0, line);                 // board column 247
    CHECK_EQ(line[0], red);
    CHECK_EQ(line[7], red);
    CHECK_EQ(line[8], black);
    m.render_line(7, line);
    CHECK_EQ(line[0], red);
    m.render_line(8, line);
    CHECK_EQ(line[0], black);
    m.render_line(239, line);               // board column 8
    CHECK_EQ(line[191], red);
    CHECK_EQ(line[183], black);
    m.render_line(232, line);
    CHECK_EQ(line[184], red);
    // Cover bit: pixel value 0 drawn in colour 3's pen 0 (green only).
    m.render_line(247 - 160, line);         // board column 160 = tile column 20
    CHECK((line[80 - 32] & 0x07e0) != 0 && (line[80 - 32] & 0xf81f) == 0);
    CHECK_EQ(line[88 - 32], black);
    // The back tile's single pixel.
    m.render_line(247 - 200, line);
    CHECK_EQ(line[48 - 32], red);
    CHECK_EQ(line[49 - 32], black);
    m.render_line(247 - 201, line);
    CHECK_EQ(line[48 - 32], black);
    // Sprites: sprite 0 (blue, board x 100-115, y 100-115) on top of
    // sprite 1 (red only, x 104-119).
    m.render_line(247 - 110, line);
    CHECK_EQ(line[100 - 32], blue);
    m.render_line(247 - 117, line);
    CHECK((line[100 - 32] & 0xf800) != 0 && (line[100 - 32] & 0x07ff) == 0);
    m.render_line(247 - 100, line);
    CHECK_EQ(line[100 - 32], blue);
    CHECK_EQ(line[115 - 32], blue);
    CHECK_EQ(line[99 - 32], black);
    CHECK_EQ(line[116 - 32], black);
    m.render_line(247 - 115, line);
    CHECK_EQ(line[100 - 32], blue);
    m.render_line(247 - 99, line);
    CHECK_EQ(line[100 - 32], black);
    // Flipped both ways, sprite 2's (0, 0) pixel lands at (60 + 15, 150 + 15).
    c.wr(0x900a, 0x02);                     // colour 2 to see it: no flips first
    m.run_frame(nullptr, 0);
    m.render_line(247 - 60, line);
    CHECK_EQ(line[150 - 32], blue);
    CHECK_EQ(line[151 - 32], black);
    c.wr(0x900a, 0x32);
    m.run_frame(nullptr, 0);
    m.render_line(247 - 60, line);
    CHECK_EQ(line[150 - 32], black);
    m.render_line(247 - 75, line);
    CHECK_EQ(line[165 - 32], blue);
    CHECK_EQ(line[164 - 32], black);
    // y = 0: the sprite is off.
    c.wr(0x9001, 0); c.wr(0x9005, 0);
    m.run_frame(nullptr, 0);
    m.render_line(247 - 100, line);
    CHECK_EQ(line[100 - 32], black);

    // Back layer scroll: X 8 brings tile column 26 to where 25 was; Y 8
    // moves rows up 8 pixels.
    c.wr(0x8000 + 6 * 32 + 26, 0x01); c.wr(0x8400 + 6 * 32 + 26, 1);   // all red, board x 208-215
    c.wr(0xf000, 8);
    m.run_frame(nullptr, 0);
    m.render_line(247 - 200, line);         // board column 200 shows tilemap column 208
    CHECK_EQ(line[48 - 32], red);
    CHECK_EQ(line[55 - 32], red);
    CHECK_EQ(line[56 - 32], black);
    c.wr(0xf800, 8);
    m.run_frame(nullptr, 0);
    m.render_line(247 - 200, line);
    CHECK_EQ(line[40 - 32], red);           // rows 48-55 now at 40-47
    CHECK_EQ(line[47 - 32], red);
    CHECK_EQ(line[48 - 32], black);
    c.wr(0xf800, 3);                        // not a multiple of 8
    m.run_frame(nullptr, 0);
    m.render_line(247 - 200, line);
    CHECK_EQ(line[45 - 32], red);
    CHECK_EQ(line[52 - 32], red);
    CHECK_EQ(line[44 - 32], black);
    CHECK_EQ(line[53 - 32], black);
    // The front layer doesn't scroll.
    m.render_line(0, line);
    CHECK_EQ(line[0], red);
}

} // namespace

int main()
{
    test_z80_times();
    test_sn76489();
    test_mspacman_wiring();
    test_mspacman_machine();
    test_mrdo_level();
    test_mrdo_machine();
    std::printf("Ms. Pac-Man / Mr. Do! tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
