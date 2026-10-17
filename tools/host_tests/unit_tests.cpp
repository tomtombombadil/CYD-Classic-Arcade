// Unit tests for the emulator modules that can be checked without ROMs:
// CRC-32, the ZIP reader and ROM set matcher with variants (on fixtures
// written by make_fixtures.py), the Namco WSG (synthetic waveform PROM),
// Z80 timing / interrupts, the Pac-Man machine wiring on a tiny hand-made
// program and synthetic graphics, 8080 timing / flags / interrupts, 8035
// timing / flags / stack / banks / timer, counter and interrupts / ports,
// the WAV parser and sample player (synthetic WAVs), the Space Invaders
// sound models, the Space Invaders machine wiring (shifter, inputs, video,
// sound) on a tiny hand-made 8080 program, and the Donkey Kong wiring
// (NMI, sprite DMA, inputs, tiles / sprites / flip / palette bank, the
// 8035's latch and DAC, the stomp, the watchdog) on hand-made Z80 and
// 8035 programs, and Rally-X (scroll / radar maths, the crash sound model,
// the machine's wiring and video on a tiny Z80 program and synthetic
// graphics).
//
//   unit_tests <fixture dir>
#include "../../emu/cpu/i8035.h"
#include "../../emu/cpu/i8080.h"
#include "../../emu/cpu/z80.h"
#include "../../emu/machines/dkong.h"
#include "../../emu/machines/invaders.h"
#include "../../emu/machines/pacman.h"
#include "../../emu/machines/rallyx.h"
#include "../../emu/rom/crc32.h"
#include "../../emu/rom/romset.h"
#include "../../emu/rom/zip.h"
#include "../../emu/sound/invaders_sound.h"
#include "../../emu/sound/namco_wsg.h"
#include "../../emu/sound/rallyx_bang.h"
#include "../../emu/sound/samples.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
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

std::vector<uint8_t> gen(size_t n, int k, int j)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = uint8_t(((int(i) * k + j) ^ int(i >> 3)) & 0xff);
    return v;
}

std::vector<uint8_t> read_file(const std::string& path)
{
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return v;
    std::fseek(f, 0, SEEK_END);
    v.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
    std::fclose(f);
    return v;
}

// --- CRC-32 -----------------------------------------------------------------

void test_crc32()
{
    const char* s = "123456789";
    CHECK_EQ(crc32::of(reinterpret_cast<const uint8_t*>(s), 9), 0xcbf43926u);
    CHECK_EQ(crc32::of(nullptr, 0), 0u);
    const auto data = gen(1000, 3, 1);
    const uint32_t whole = crc32::of(data.data(), data.size());
    uint32_t parts = crc32::update(0, data.data(), 333);
    parts = crc32::update(parts, data.data() + 333, data.size() - 333);
    CHECK_EQ(parts, whole);
}

// --- ZIP + romset -------------------------------------------------------------

void test_zip(const std::string& dir)
{
    const auto parent = read_file(dir + "/parent.zip");
    CHECK(!parent.empty());
    zip::Reader z;
    CHECK(zip::looks_like_zip(parent.data(), parent.size()));
    CHECK(z.open(parent.data(), parent.size()));
    CHECK_EQ(z.count(), 3);
    uint32_t it = z.begin();
    zip::Entry e;
    int seen = 0;
    while (z.next(it, e)) {
        std::vector<uint8_t> out(e.size);
        CHECK(z.extract(e, out.data()));
        const std::string name(e.name, e.name_len);
        if (name == "parent_t1.bin") { CHECK_EQ(e.method, 0); CHECK(out == gen(4096, 3, 1)); ++seen; }
        if (name == "p.1")           { CHECK_EQ(e.method, 8); CHECK(out == gen(256, 7, 3)); ++seen; }
    }
    CHECK_EQ(seen, 2);

    const auto clone = read_file(dir + "/clone.zip");      // has a comment after the directory
    CHECK(z.open(clone.data(), clone.size()));
    CHECK_EQ(z.count(), 2);

    const auto broken = read_file(dir + "/broken.zip");
    CHECK(!z.open(broken.data(), broken.size()));
    const uint8_t junk[30] = {'P', 'K', 3, 4};
    CHECK(!z.open(junk, sizeof junk));
}

void test_romset(const std::string& dir)
{
    static const romset::Region regions[] = {{"cpu", 0x3000}, {"prom", 0x200}};
    const uint32_t c1 = crc32::of(gen(4096, 3, 1).data(), 4096);
    const uint32_t c2 = crc32::of(gen(4096, 5, 2).data(), 4096);
    const uint32_t c3 = crc32::of(gen(4096, 1, 0).data(), 4096);
    const uint32_t cp = crc32::of(gen(256, 7, 3).data(), 256);
    const romset::Rom roms[] = {
        {0, 0x0000, 0x1000, c1, "t.1", false},
        {0, 0x1000, 0x1000, c2, "t.2", false},
        {0, 0x2000, 0x1000, c3, "t.3", false},
        {1, 0x0000, 0x0100, cp, "p.1", false},
        {1, 0x0100, 0x0100, 0x12345678, "p.2", true},
    };
    const romset::Set set = {"testgame", "testparent", "Test", regions, 2, roms, 5};

    const auto parent = read_file(dir + "/parent.zip");
    const auto clone = read_file(dir + "/clone.zip");
    const auto loose = read_file(dir + "/anything.rom");
    const auto broken = read_file(dir + "/broken.zip");
    std::vector<uint8_t> r0(0x3000), r1(0x200);
    uint8_t* reg[2] = {r0.data(), r1.data()};
    romset::Status st[5];

    // Clone ZIP first, parent second: order must not matter. t.3 in the
    // clone has the right name but wrong data.
    romset::Source two[] = {{"clone.zip", clone.data(), clone.size()},
                            {"parent.zip", parent.data(), parent.size()},
                            {"broken.zip", broken.data(), broken.size()}};
    CHECK(!romset::load(set, two, 3, reg, st));
    CHECK(st[0] == romset::Status::Ok);
    CHECK(st[1] == romset::Status::Ok);
    CHECK(st[2] == romset::Status::WrongCrc);
    CHECK(st[3] == romset::Status::Ok);
    CHECK(st[4] == romset::Status::Missing);
    char msg[512];
    CHECK_EQ(romset::report(set, st, msg, sizeof msg), 1);
    CHECK(std::strstr(msg, "wrong crc ROM t.3 crc") != nullptr);
    CHECK(std::strstr(msg, "optional") != nullptr);

    // The loose file supplies t.3 by CRC; the optional p.2 may stay missing.
    romset::Source three[] = {{"clone.zip", clone.data(), clone.size()},
                              {"parent.zip", parent.data(), parent.size()},
                              {"x/anything.rom", loose.data(), loose.size()}};
    CHECK(romset::load(set, three, 3, reg, st));
    CHECK(std::memcmp(r0.data(), gen(4096, 3, 1).data(), 4096) == 0);
    CHECK(std::memcmp(r0.data() + 0x1000, gen(4096, 5, 2).data(), 4096) == 0);
    CHECK(std::memcmp(r0.data() + 0x2000, gen(4096, 1, 0).data(), 4096) == 0);
    CHECK(std::memcmp(r1.data(), gen(256, 7, 3).data(), 256) == 0);
    CHECK_EQ(romset::report(set, st, msg, sizeof msg), 0);

    // No sources at all.
    CHECK(!romset::load(set, nullptr, 0, reg, st));
    CHECK_EQ(romset::report(set, st, msg, 8), 4);           // tiny buffer: still counts
    CHECK_EQ(std::strlen(msg) < 8, 1);

    // Variants: two program ROMs at the same place, one per variant ("one"
    // and "two"), and a shared one; loose files as sources.
    static const char* const kVariants[] = {"one", "two"};
    const auto common = gen(4096, 3, 1), v1 = gen(4096, 5, 2), v2 = gen(4096, 1, 0);
    const romset::Rom vroms[] = {
        {0, 0x0000, 0x1000, c1, "common", false},
        {0, 0x1000, 0x1000, c2, "one.2", false, 1},
        {0, 0x1000, 0x1000, c3, "two.2", false, 2},
    };
    const romset::Set vset = {"one", nullptr, "Variants", regions, 2, vroms, 3, kVariants, 2};
    romset::Source only_two[] = {{"c", common.data(), common.size()}, {"t", v2.data(), v2.size()}};
    CHECK(romset::available(vset, only_two, 2));
    CHECK_EQ(romset::pick_variant(vset, only_two, 2), 2);
    CHECK(romset::load(vset, only_two, 2, reg, st));
    CHECK(st[1] == romset::Status::Unused);
    CHECK(std::memcmp(r0.data() + 0x1000, v2.data(), 4096) == 0);
    CHECK_EQ(romset::report(vset, st, msg, sizeof msg), 0);
    CHECK_EQ(msg[0], 0);
    // Both there: the first variant, not overwritten by the second.
    romset::Source both[] = {{"t", v2.data(), v2.size()}, {"o", v1.data(), v1.size()},
                             {"c", common.data(), common.size()}};
    CHECK_EQ(romset::pick_variant(vset, both, 3), 1);
    CHECK(romset::load(vset, both, 3, reg, st));
    CHECK(std::memcmp(r0.data() + 0x1000, v1.data(), 4096) == 0);
    CHECK(st[2] == romset::Status::Unused);
    // Only the shared ROM: not available; the report names the first
    // variant's missing ROM only.
    CHECK(!romset::available(vset, only_two, 1));
    CHECK(!romset::load(vset, only_two, 1, reg, st));
    CHECK_EQ(romset::report(vset, st, msg, sizeof msg), 1);
    CHECK(std::strstr(msg, "one.2") != nullptr);
    CHECK(std::strstr(msg, "two.2") == nullptr);
}

// --- WSG ----------------------------------------------------------------------

// Frequency from the positive-going zero crossings of a rendered tone.
double measure_hz(const std::vector<int16_t>& s, double rate)
{
    int first = -1, last = -1, count = 0;
    for (size_t i = 1; i < s.size(); ++i)
        if (s[i - 1] < 0 && s[i] >= 0) {
            if (first < 0) first = int(i);
            last = int(i);
            ++count;
        }
    if (count < 2) return 0;
    return (count - 1) * rate / (last - first);
}

void test_wsg()
{
    uint8_t prom[256] = {};
    for (int i = 0; i < 32; ++i)                // wave 0: sine, wave 1: square
        prom[i] = uint8_t(std::lround(7.5 + 7.5 * std::sin(2 * M_PI * (i + 0.5) / 32)));
    for (int i = 0; i < 32; ++i) prom[32 + i] = i < 16 ? 15 : 0;

    namco_wsg::Wsg w;
    const int rate = 24000;
    w.init(prom, 3, 96000, rate);
    // Voice 0: 20-bit frequency 4806 = 0x012C6 -> 4806 * 96000 / 2^20 = 440.0 Hz.
    const uint32_t f = 4806;
    for (int k = 0; k < 5; ++k) w.pacman_write(uint8_t(0x10 + k), uint8_t((f >> (4 * k)) & 0x0f));
    w.pacman_write(0x05, 0);                    // waveform 0
    w.pacman_write(0x15, 15);                   // volume
    CHECK_EQ(w.frequency(0), f);
    CHECK_EQ(w.volume(0), 15);
    std::vector<int16_t> s(rate * 2);
    w.render(s.data(), int(s.size()));
    const double hz = measure_hz(s, rate);
    std::printf("wsg: voice 0 f=%u -> %.2f Hz (want 440.00)\n", unsigned(f), hz);
    CHECK(std::fabs(hz - 440.0) < 0.5);
    int peak = 0;
    for (int16_t v : s) peak = std::max(peak, std::abs(int(v)));
    CHECK(peak > 6000 && peak <= 32767);

    // Voice 1 has no low nibble: 16-19 hold frequency bits 4-19.
    w.pacman_write(0x15, 0);
    for (int k = 0; k < 4; ++k) w.pacman_write(uint8_t(0x16 + k), uint8_t(((0x02580 >> 4) >> (4 * k)) & 0x0f));
    w.pacman_write(0x0a, 1);                    // square
    w.pacman_write(0x1a, 10);
    CHECK_EQ(w.frequency(1), 0x02580u);         // 9600 * 96000 / 2^20 = 878.9 Hz
    w.render(s.data(), int(s.size()));
    const double hz1 = measure_hz(s, rate);
    std::printf("wsg: voice 1 f=0x2580 -> %.2f Hz (want 878.91)\n", hz1);
    CHECK(std::fabs(hz1 - 878.91) < 1.0);

    // Voice 2 registers, then sound off = silence.
    w.pacman_write(0x1b, 0x1);
    CHECK_EQ(w.frequency(2), 0x10u);
    w.set_enabled(false);
    w.render(s.data(), 100);
    bool silent = true;
    for (int i = 0; i < 100; ++i) silent = silent && s[size_t(i)] == 0;
    CHECK(silent);
}

// --- Z80 ----------------------------------------------------------------------

struct Bench {
    uint8_t  mem[0x10000] = {};
    z80::Cpu cpu;
    uint8_t  last_out = 0;
    uint16_t last_port = 0;
    Bench()
    {
        cpu.map_ram(0, 0x10000, mem);
        cpu.ctx = this;
        cpu.port_out = [](void* c, uint16_t p, uint8_t v) {
            static_cast<Bench*>(c)->last_port = p;
            static_cast<Bench*>(c)->last_out = v;
        };
        cpu.port_in = [](void*, uint16_t p) { return uint8_t(p >> 8); };
        cpu.reset();
        cpu.sp.w = 0xf000;
    }
    void load(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) mem[at++] = b;
    }
    int step() { return cpu.run(1); }     // one instruction (or interrupt)
};

void test_z80_timing()
{
    struct Case { std::initializer_list<uint8_t> code; int t; const char* what; };
    const Case cases[] = {
        {{0x00}, 4, "NOP"},
        {{0x3e, 0x12}, 7, "LD A,n"},
        {{0x18, 0x00}, 12, "JR e"},
        {{0xc3, 0x00, 0x20}, 10, "JP nn"},
        {{0xdd, 0x36, 0x05, 0x03}, 19, "LD (IX+5),n"},
        {{0xfd, 0x34, 0x02}, 23, "INC (IY+2)"},
        {{0xdd, 0x7e, 0x01}, 19, "LD A,(IX+1)"},
        {{0xdd, 0x86, 0x01}, 19, "ADD A,(IX+1)"},
        {{0xdd, 0xcb, 0x01, 0x5e}, 20, "BIT 3,(IX+1)"},
        {{0xdd, 0xcb, 0x01, 0x00}, 23, "RLC (IX+1),B"},
        {{0xcb, 0x46}, 12, "BIT 0,(HL)"},
        {{0xcb, 0x06}, 15, "RLC (HL)"},
        {{0xcb, 0x11}, 8, "RL C"},
        {{0xdd, 0x26, 0x44}, 11, "LD IXH,n"},
        {{0xdd, 0x09}, 15, "ADD IX,BC"},
        {{0xdd, 0xe5}, 15, "PUSH IX"},
        {{0xdd, 0xe3}, 23, "EX (SP),IX"},
        {{0xdd, 0x00}, 8, "DD NOP"},
        {{0xdd, 0xdd, 0x00}, 4, "DD DD (first prefix alone)"},
        {{0xcd, 0x00, 0x20}, 17, "CALL nn"},
        {{0xc9}, 10, "RET"},
        {{0xed, 0x5f}, 9, "LD A,R"},
        {{0xed, 0x46}, 8, "IM 0"},
        {{0xed, 0x4b, 0, 0}, 20, "LD BC,(nn)"},
        {{0xed, 0x6f}, 18, "RLD"},
        {{0xdb, 0x10}, 11, "IN A,(n)"},
        {{0xe3}, 19, "EX (SP),HL"},
        {{0x34}, 11, "INC (HL)"},
        {{0x36, 0x01}, 10, "LD (HL),n"},
        {{0xf9}, 6, "LD SP,HL"},
    };
    for (const Case& c : cases) {
        Bench b;
        b.cpu.hl.w = 0x8000;
        b.cpu.ix.w = 0x8000;
        b.cpu.iy.w = 0x8000;
        b.load(0, c.code);
        const int t = b.step();
        if (t != c.t) std::printf("  timing %s\n", c.what);
        CHECK_EQ(t, c.t);
    }

    // Conditionals: taken and not taken.
    {
        Bench b;
        b.cpu.af.b.l = 0;                         // NZ, NC
        b.load(0, {0x20, 0x02, 0x28, 0x02, 0x00, 0x00, 0xc8, 0xc0});
        CHECK_EQ(b.step(), 12);                   // JR NZ taken -> 4
        CHECK_EQ(b.cpu.pc.w, 4);
        b.cpu.pc.w = 2;
        CHECK_EQ(b.step(), 7);                    // JR Z not taken
        b.cpu.pc.w = 6;
        CHECK_EQ(b.step(), 5);                    // RET Z not taken
        b.mem[0xf000] = 0x34; b.mem[0xf001] = 0x12;
        CHECK_EQ(b.step(), 11);                   // RET NZ taken
        CHECK_EQ(b.cpu.pc.w, 0x1234);
    }
    {
        Bench b;                                  // DJNZ: 13 while looping, 8 at the end
        b.cpu.bc.b.h = 2;
        b.load(0, {0x10, 0xfe});
        CHECK_EQ(b.step(), 13);
        CHECK_EQ(b.step(), 8);
        CHECK_EQ(b.cpu.pc.w, 2);
    }
    {
        Bench b;                                  // LDIR, 2 bytes: 21 + 16
        b.cpu.hl.w = 0x1000; b.cpu.de.w = 0x2000; b.cpu.bc.w = 2;
        b.mem[0x1000] = 0xaa; b.mem[0x1001] = 0xbb;
        b.load(0, {0xed, 0xb0});
        CHECK_EQ(b.step(), 21);
        CHECK_EQ(b.cpu.pc.w, 0);
        CHECK_EQ(b.step(), 16);
        CHECK_EQ(b.cpu.pc.w, 2);
        CHECK_EQ(b.mem[0x2001], 0xbb);
        CHECK_EQ(b.cpu.bc.w, 0);
    }
}

void test_z80_misc()
{
    {
        Bench b;                                  // R counts M1 cycles (prefixes too)
        b.load(0, {0x00, 0x00, 0xdd, 0x21, 0x00, 0x00, 0xcb, 0x00});
        b.step(); b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.r & 0x7f, 6);
    }
    {
        Bench b;                                  // undocumented: RLC (IX+1),B copies to B
        b.cpu.ix.w = 0x3000;
        b.mem[0x3001] = 0x81;
        b.load(0, {0xdd, 0xcb, 0x01, 0x00});
        b.step();
        CHECK_EQ(b.mem[0x3001], 0x03);
        CHECK_EQ(b.cpu.bc.b.h, 0x03);
        CHECK_EQ(b.cpu.af.b.l & z80::CF, 1);
    }
    {
        Bench b;                                  // SLL B; LD IXL,A; OUT (C),0
        b.cpu.bc.w = 0x4042;
        b.cpu.af.b.h = 0x5a;
        b.load(0, {0xcb, 0x30, 0xdd, 0x6f, 0xed, 0x71});
        b.step();
        CHECK_EQ(b.cpu.bc.b.h, 0x81);
        b.step();
        CHECK_EQ(b.cpu.ix.b.l, 0x5a);
        b.step();
        CHECK_EQ(b.last_port, 0x8142);
        CHECK_EQ(b.last_out, 0);
    }
    {
        Bench b;                                  // OUT (n),A puts A on the high address byte
        b.cpu.af.b.h = 0x12;
        b.load(0, {0xd3, 0x34});
        b.step();
        CHECK_EQ(b.last_port, 0x1234);
    }
}

// LDIR / LDDR run their repeats in one go (Cpu::block_copy): the result of
// run(n) must be what stepping one instruction at a time gives - the same
// registers, R, memory and T-states - whatever the budget, with an
// interrupt due, an unmapped (callback) page in the way, the copy running
// over its own bytes, or a DD prefix.
void test_z80_block_copy()
{
    struct Case {
        std::initializer_list<uint8_t> code;  // at 0x100
        uint16_t hl, de, bc;
        bool     irq;                         // IRQ line up (IM 1, after EI)
        bool     hole;                        // 0x8400-0x87FF goes through callbacks
    };
    const Case cases[] = {
        {{0xed, 0xb0, 0x76}, 0x4000, 0x8000, 40, false, false},             // LDIR
        {{0xed, 0xb8, 0x76}, 0x4027, 0x8027, 40, false, false},             // LDDR
        {{0xfb, 0xed, 0xb0, 0x76}, 0x4000, 0x8000, 40, true, false},        // EI; LDIR, IRQ due
        {{0xed, 0xb0, 0x76}, 0x43f0, 0x83f0, 40, false, true},              // into a callback page
        {{0xed, 0xb0, 0x76}, 0x40f0, 0x00f8, 40, false, false},             // over its own bytes
        {{0xdd, 0xed, 0xb0, 0x76}, 0x4000, 0x8000, 40, false, false},       // DD prefix
        {{0xed, 0xb0, 0x76}, 0x4000, 0x8000, 0, false, false},              // BC = 0: 64 KB
    };
    for (const Case& k : cases)
        for (int budget = 1; budget <= 1500; budget += (budget < 120 ? 1 : 37)) {
            Bench a, b;
            for (Bench* x : {&a, &b}) {
                for (int i = 0; i < 0x10000; ++i) x->mem[i] = uint8_t(i * 7 + (i >> 8));
                x->load(0x100, k.code);
                x->load(0x38, {0xc9});                // RET
                x->cpu.pc.w = 0x100;
                x->cpu.hl.w = k.hl;
                x->cpu.de.w = k.de;
                x->cpu.bc.w = k.bc;
                x->cpu.af.w = 0x12c5;
                x->cpu.im = 1;
                x->cpu.r = 0x75;
                if (k.irq) x->cpu.irq(true);
                if (k.hole) {
                    x->cpu.map_ram(0x8400, 0x400, nullptr);
                    x->cpu.mem_read = [](void* c, uint16_t p) { return static_cast<Bench*>(c)->mem[p]; };
                    x->cpu.mem_write = [](void* c, uint16_t p, uint8_t v) { static_cast<Bench*>(c)->mem[p] = v; };
                }
            }
            const int ran = a.cpu.run(budget);
            int stepped = 0;
            while (stepped < budget) stepped += b.step();
            CHECK_EQ(ran, stepped);
            CHECK_EQ(a.cpu.pc.w, b.cpu.pc.w);
            CHECK_EQ(a.cpu.hl.w, b.cpu.hl.w);
            CHECK_EQ(a.cpu.de.w, b.cpu.de.w);
            CHECK_EQ(a.cpu.bc.w, b.cpu.bc.w);
            CHECK_EQ(a.cpu.af.w, b.cpu.af.w);
            CHECK_EQ(a.cpu.sp.w, b.cpu.sp.w);
            CHECK_EQ(a.cpu.r_value(), b.cpu.r_value());
            CHECK_EQ(int(a.cpu.iff1), int(b.cpu.iff1));
            CHECK(std::memcmp(a.mem, b.mem, sizeof a.mem) == 0);
        }
}

void test_z80_interrupts()
{
    {
        Bench b;                                  // IM 1 out of HALT
        b.load(0, {0xed, 0x56, 0xfb, 0x76, 0x00});
        b.step(); b.step(); b.step();
        CHECK(b.cpu.halted);
        CHECK_EQ(b.cpu.pc.w, 4);
        const int burnt = b.cpu.run(10);          // halted: burns whole NOPs
        CHECK(burnt >= 10 && burnt % 4 == 0);
        b.cpu.irq(true, 0xff);
        CHECK_EQ(b.step(), 13);
        CHECK_EQ(b.cpu.pc.w, 0x38);
        CHECK(!b.cpu.halted);
        CHECK_EQ(b.cpu.iff1, 0);
        CHECK_EQ(b.mem[b.cpu.sp.w], 4);           // returns after the HALT
    }
    {
        Bench b;                                  // EI delays acceptance by one instruction
        b.load(0, {0xed, 0x56, 0xfb, 0x00, 0x00});
        b.cpu.irq(true, 0xff);
        b.step();                                 // IM 1 (interrupts still off)
        b.step();                                 // EI
        CHECK_EQ(b.cpu.pc.w, 3);
        b.step();                                 // the NOP after EI still runs
        CHECK_EQ(b.cpu.pc.w, 4);
        b.step();
        CHECK_EQ(b.cpu.pc.w, 0x38);
    }
    {
        Bench b;                                  // IM 2 vector fetch
        b.load(0, {0xed, 0x5e, 0xfb, 0x00});
        b.cpu.i = 0x12;
        b.mem[0x1234] = 0x78; b.mem[0x1235] = 0x56;
        b.step(); b.step(); b.step();
        b.cpu.irq(true, 0x34);
        CHECK_EQ(b.step(), 19);
        CHECK_EQ(b.cpu.pc.w, 0x5678);
    }
    {
        Bench b;                                  // IM 0 with RST 10h on the bus
        b.load(0, {0xfb, 0x00});
        b.step(); b.step();
        b.cpu.irq(true, 0xd7);
        b.step();
        CHECK_EQ(b.cpu.pc.w, 0x10);
    }
    {
        Bench b;                                  // NMI keeps IFF2; RETN restores IFF1
        b.load(0, {0xfb, 0x00, 0x00});
        b.load(0x66, {0xed, 0x45});
        b.step(); b.step();
        b.cpu.nmi();
        CHECK_EQ(b.step(), 11);
        CHECK_EQ(b.cpu.pc.w, 0x66);
        CHECK_EQ(b.cpu.iff1, 0);
        CHECK_EQ(b.cpu.iff2, 1);
        b.step();
        CHECK_EQ(b.cpu.iff1, 1);
        CHECK_EQ(b.cpu.pc.w, 2);
    }
    {
        Bench b;                                  // DI: the line is ignored
        b.load(0, {0xf3, 0x00, 0x00});
        b.cpu.irq(true, 0xff);
        b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.pc.w, 3);
    }
}

// --- Pac-Man wiring -------------------------------------------------------------

void test_pacman()
{
    static uint8_t cpu_rom[0x4000];
    static uint8_t gfx[0x2000];
    static uint8_t proms[0x120];
    static uint8_t sound[0x200];
    std::memset(cpu_rom, 0, sizeof cpu_rom);
    std::memset(gfx, 0, sizeof gfx);
    std::memset(proms, 0, sizeof proms);
    const uint8_t prog[] = {
        0xf3,                   // DI
        0x31, 0xc0, 0x4f,       // LD SP,4FC0
        0xed, 0x5e,             // IM 2
        0x3e, 0x3f, 0xed, 0x47, // LD A,3F; LD I,A
        0x3e, 0x80, 0xd3, 0x00, // LD A,80; OUT (0),A  - vector
        0x3e, 0x01,             // LD A,1
        0x32, 0x00, 0x50,       // LD (5000),A         - IRQ enable
        0x32, 0x01, 0x50,       // LD (5001),A         - sound enable
        0xfb,                   // EI
        0x32, 0xc0, 0x50,       // loop: LD (50C0),A   - watchdog
        0x18, 0xfb,             // JR loop
    };
    std::memcpy(cpu_rom, prog, sizeof prog);
    const uint8_t isr[] = {
        0xf5, 0xaf, 0x32, 0x00, 0x50,   // PUSH AF; XOR A; LD (5000),A - acknowledge
        0x21, 0x00, 0x4c, 0x34,         // LD HL,4C00; INC (HL)
        0x3e, 0x01, 0x32, 0x00, 0x50,   // LD A,1; LD (5000),A
        0xf1, 0xfb, 0xed, 0x4d,         // POP AF; EI; RETI
    };
    std::memcpy(cpu_rom + 0x100, isr, sizeof isr);
    cpu_rom[0x3f80] = 0x00; cpu_rom[0x3f81] = 0x01;   // vector table -> 0100

    // Palette: 1 red, 2 green, 3 blue; colour 1 = pens 0 1 2 3.
    proms[1] = 0x07; proms[2] = 0x38; proms[3] = 0xc0;
    for (int p = 0; p < 4; ++p) proms[0x20 + 4 + p] = uint8_t(p);
    // Tile 1: all pixels 3. Tile 2: only the display's top-left pixel
    // (hardware pixel x 0, y 7 -> bit 64 + 56 = byte 15, MSB + bit 3).
    std::memset(gfx + 16, 0xff, 16);
    gfx[32 + 15] = 0x88;
    // Sprite 1: all pixels 3.
    std::memset(gfx + 0x1000 + 64, 0xff, 64);

    static pacman::Machine m;
    m.init({cpu_rom, gfx, proms, sound}, 24000);
    int16_t audio[1024];
    int samples = 0;
    for (int f = 0; f < 30; ++f) samples += m.run_frame(audio, 1024);
    CHECK_EQ(samples, 30 * 396);                  // 24000 Hz / 60.606 Hz
    CHECK_EQ(m.cpu().rd(0x4c00), 30);             // one IRQ per VBLANK
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(m.cpu().rd(0x4800), 0xbf);
    CHECK_EQ(m.cpu().rd(0x5080), pacman::kDipDefault);
    m.set_inputs(pacman::kCoin | pacman::kUp | pacman::kStart1);
    CHECK_EQ(m.cpu().rd(0x5000), 0xff & ~0x21);
    CHECK_EQ(m.cpu().rd(0x7040), 0xff & ~0x20);   // mirror
    m.set_inputs(0);

    // Tiles: middle area column 0 row 2 = 3A0, column 27 row 2 = 040,
    // top row column 0 = 3DD, bottom row 34 column 0 = 01D.
    auto& c = m.cpu();
    c.wr(0x4000 + 0x3a0, 2); c.wr(0x4400 + 0x3a0, 1);
    c.wr(0x4000 + 0x040, 1); c.wr(0x4400 + 0x040, 1);
    c.wr(0x4000 + 0x3dd, 2); c.wr(0x4400 + 0x3dd, 1);
    c.wr(0x4000 + 0x01d, 1); c.wr(0x4400 + 0x01d, 1);
    // Sprite 3: code 1, colour 1, at hardware (100, 100).
    c.wr(0x4ff6, 1 << 2); c.wr(0x4ff7, 1);
    c.wr(0x5066, 100); c.wr(0x5067, 100);
    m.run_frame(nullptr, 0);

    const uint16_t blue = 0x001f, black = 0;
    uint16_t line[224];
    m.render_line(16, line);
    CHECK_EQ(line[0], blue);
    CHECK_EQ(line[1], black);
    CHECK_EQ(line[216], blue);                    // tile at column 27
    CHECK_EQ(line[215], black);
    m.render_line(17, line);
    CHECK_EQ(line[0], black);
    m.render_line(0, line);
    CHECK_EQ(line[0], blue);
    m.render_line(272, line);
    CHECK_EQ(line[0], blue);
    CHECK_EQ(line[8], black);
    // Sprite: top = 272 - 100 = 172, left = 239 - 100 = 139, 16 x 16.
    m.render_line(172, line);
    CHECK_EQ(line[139], blue);
    CHECK_EQ(line[154], blue);
    CHECK_EQ(line[138], black);
    CHECK_EQ(line[155], black);
    m.render_line(171, line);
    CHECK_EQ(line[139], black);
    m.render_line(187, line);
    CHECK_EQ(line[150], blue);
    m.render_line(188, line);
    CHECK_EQ(line[150], black);

    // Flip screen turns the tile layer: the top-left tile pixel goes to
    // the bottom-right corner.
    c.wr(0x5003, 1);
    m.run_frame(nullptr, 0);
    m.render_line(287, line);
    CHECK_EQ(line[223], blue);
    CHECK_EQ(line[222], black);

    // Without watchdog writes the board resets after 16 frames.
    std::memset(cpu_rom + 0x17, 0, 3);            // loop no longer kicks the watchdog
    cpu_rom[0x17] = 0x18; cpu_rom[0x18] = 0xfe;   // JR $
    m.init({cpu_rom, gfx, proms, sound}, 24000);
    for (int f = 0; f < 40; ++f) m.run_frame(nullptr, 0);
    CHECK(m.watchdog_resets() >= 2);
}

// --- 8080 ---------------------------------------------------------------------

struct Bench8 {
    uint8_t    mem[0x10000] = {};
    i8080::Cpu cpu;
    uint16_t   last_port = 0, last_in_port = 0;
    uint8_t    last_out = 0;
    int        acks = 0;
    Bench8()
    {
        cpu.map_ram(0, 0x10000, mem);
        cpu.ctx = this;
        cpu.port_out = [](void* c, uint16_t p, uint8_t v) {
            static_cast<Bench8*>(c)->last_port = p;
            static_cast<Bench8*>(c)->last_out = v;
        };
        cpu.port_in = [](void* c, uint16_t p) {
            static_cast<Bench8*>(c)->last_in_port = p;
            return uint8_t(0x5a);
        };
        cpu.irq_ack = [](void* c) {
            Bench8& b = *static_cast<Bench8*>(c);
            ++b.acks;
            b.cpu.irq(false);
        };
        cpu.reset();
        cpu.sp.w = 0xf000;
    }
    void load(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) mem[at++] = b;
    }
    int step() { return cpu.run(1); }
};

void test_i8080_timing()
{
    struct Case { std::initializer_list<uint8_t> code; int t; const char* what; };
    const Case cases[] = {
        {{0x00}, 4, "NOP"},
        {{0x08}, 4, "NOP (08, undocumented)"},
        {{0x41}, 5, "MOV B,C"},
        {{0x77}, 7, "MOV M,A"},
        {{0x7e}, 7, "MOV A,M"},
        {{0x36, 0x12}, 10, "MVI M,n"},
        {{0x06, 0x12}, 7, "MVI B,n"},
        {{0x21, 0, 0}, 10, "LXI H"},
        {{0x34}, 10, "INR M"},
        {{0x04}, 5, "INR B"},
        {{0x03}, 5, "INX B"},
        {{0x09}, 10, "DAD B"},
        {{0x2a, 0, 0}, 16, "LHLD"},
        {{0x32, 0, 0x80}, 13, "STA"},
        {{0xe3}, 18, "XTHL"},
        {{0xcd, 0, 0x20}, 17, "CALL"},
        {{0xdd, 0, 0x20}, 17, "CALL (DD, undocumented)"},
        {{0xc9}, 10, "RET"},
        {{0xd9}, 10, "RET (D9, undocumented)"},
        {{0xcb, 0, 0x20}, 10, "JMP (CB, undocumented)"},
        {{0xcf}, 11, "RST 1"},
        {{0xc5}, 11, "PUSH B"},
        {{0xf1}, 10, "POP PSW"},
        {{0xdb, 0x01}, 10, "IN"},
        {{0xd3, 0x01}, 10, "OUT"},
        {{0xeb}, 4, "XCHG"},
        {{0xe9}, 5, "PCHL"},
        {{0xf9}, 5, "SPHL"},
        {{0x76}, 7, "HLT"},
        {{0xc6, 1}, 7, "ADI"},
        {{0x86}, 7, "ADD M"},
        {{0x80}, 4, "ADD B"},
        {{0x27}, 4, "DAA"},
    };
    for (const Case& c : cases) {
        Bench8 b;
        b.cpu.hl.w = 0x8000;
        b.load(0, c.code);
        const int t = b.step();
        if (t != c.t) std::printf("  8080 timing %s\n", c.what);
        CHECK_EQ(t, c.t);
    }
    {
        Bench8 b;                                 // conditionals: Z clear
        b.load(0, {0xc4, 0x00, 0x10, 0xcc, 0x00, 0x10, 0xc8, 0xc0, 0xca, 0, 0x30});
        b.cpu.af.b.l = i8080::F1;
        b.cpu.pc.w = 3;
        CHECK_EQ(b.step(), 11);                   // CZ not taken
        b.cpu.pc.w = 6;
        CHECK_EQ(b.step(), 5);                    // RZ not taken
        b.cpu.pc.w = 8;
        CHECK_EQ(b.step(), 10);                   // JZ not taken: still 10
        CHECK_EQ(b.cpu.pc.w, 11);
        b.cpu.pc.w = 0;
        CHECK_EQ(b.step(), 17);                   // CNZ taken
        CHECK_EQ(b.cpu.pc.w, 0x1000);
        b.cpu.pc.w = 7;
        CHECK_EQ(b.step(), 11);                   // RNZ taken: back to 3
        CHECK_EQ(b.cpu.pc.w, 3);
    }
}

void test_i8080_flags()
{
    using namespace i8080;
    auto run = [](std::initializer_list<uint8_t> code, uint8_t a, uint8_t f, uint8_t bval) {
        std::unique_ptr<Bench8> b(new Bench8);    // holds its own pointers: not copyable
        b->cpu.af.b.h = a;
        b->cpu.af.b.l = uint8_t(f | F1);
        b->cpu.bc.b.h = bval;
        b->load(0, code);
        while (b->cpu.pc.w < code.size()) b->step();
        return std::make_pair(b->cpu.af.b.h, b->cpu.af.b.l);
    };
    // SUB A: the complement add carries out of bit 3, so AC is set.
    auto r = run({0x97}, 0x3e, 0, 0);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, ZF | PF | AC | F1);
    // ANA: AC = bit 3 of (A | B).
    r = run({0xa0}, 0x0f, CF, 0xf0);
    CHECK_EQ(r.second, ZF | PF | AC | F1);
    r = run({0xa0}, 0x00, 0, 0x00);
    CHECK_EQ(r.second, ZF | PF | F1);
    // ADD: carry, AC, and P = parity (not overflow).
    r = run({0x80}, 0x3a, 0, 0xc6);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, ZF | PF | AC | CF | F1);
    r = run({0xc6, 0x02}, 0x01, 0, 0);
    CHECK_EQ(r.second, PF | F1);
    // DAA: 15 + 27 = 42; 99 + 01 = 00 carry.
    r = run({0xc6, 0x27, 0x27}, 0x15, 0, 0);
    CHECK_EQ(r.first, 0x42);
    CHECK_EQ(r.second, PF | AC | F1);
    r = run({0xc6, 0x01, 0x27}, 0x99, 0, 0);
    CHECK_EQ(r.first, 0x00);
    CHECK_EQ(r.second, ZF | PF | AC | CF | F1);
    // INR keeps CY, AC on a low-nibble carry; DCR AC = no borrow from bit 4.
    r = run({0x3c}, 0x0f, CF, 0);
    CHECK_EQ(r.first, 0x10);
    CHECK_EQ(r.second, AC | CF | F1);
    r = run({0x3d}, 0x10, 0, 0);
    CHECK_EQ(r.first, 0x0f);
    CHECK_EQ(r.second, PF | F1);
    r = run({0x3d}, 0x01, 0, 0);
    CHECK_EQ(r.second, ZF | PF | AC | F1);
    // CPI: A kept, borrow sets CY.
    r = run({0xfe, 0x06}, 0x05, 0, 0);
    CHECK_EQ(r.first, 0x05);
    CHECK_EQ(r.second, SF | PF | CF | F1);
    // RAL: only CY changes.
    r = run({0x17}, 0x80, ZF, 0);
    CHECK_EQ(r.first, 0x00);
    CHECK_EQ(r.second, ZF | CF | F1);
    // POP PSW / PUSH PSW: bits 3 and 5 read 0, bit 1 reads 1.
    {
        Bench8 b;
        b.mem[0xf000] = 0xff; b.mem[0xf001] = 0x12;
        b.load(0, {0xf1, 0xf5});
        b.step();
        CHECK_EQ(b.cpu.af.w, 0x12d7);
        b.cpu.af.b.l = 0x00;
        b.step();
        CHECK_EQ(b.mem[0xf000], 0x00);            // pushed as stored
        CHECK_EQ(b.mem[0xf001], 0x12);
    }
}

void test_i8080_interrupts()
{
    {
        Bench8 b;                                 // EI delays acceptance by one instruction
        b.load(0, {0xfb, 0x00, 0x00});
        b.cpu.irq(true, 0xd7);
        b.step();
        CHECK_EQ(b.cpu.pc.w, 1);
        b.step();                                 // the instruction after EI still runs
        CHECK_EQ(b.cpu.pc.w, 2);
        CHECK_EQ(b.step(), 11);                   // RST 2 from the bus
        CHECK_EQ(b.cpu.pc.w, 0x10);
        CHECK_EQ(b.acks, 1);
        CHECK_EQ(b.cpu.inte, 0);
        CHECK(!b.cpu.irq_line());                 // cleared by the board's ack
        CHECK_EQ(b.mem[0xeffe], 2);               // return address
    }
    {
        Bench8 b;                                 // HLT until an interrupt
        b.load(0, {0xfb, 0x76, 0x00});
        b.step(); b.step();
        CHECK(b.cpu.halted);
        const int burnt = b.cpu.run(10);
        CHECK(burnt >= 10 && burnt % 4 == 0);
        b.cpu.irq(true, 0xcf);
        b.step();
        CHECK(!b.cpu.halted);
        CHECK_EQ(b.cpu.pc.w, 0x08);
        CHECK_EQ(b.mem[0xeffe], 2);               // returns after the HLT
    }
    {
        Bench8 b;                                 // DI: the line is ignored; not an RST = RST 7
        b.load(0, {0xf3, 0x00, 0xfb, 0x00, 0x00});
        b.cpu.irq(true, 0x00);
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc.w, 2);
        b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.pc.w, 0x38);
    }
    {
        Bench8 b;                                 // the port number is on both address halves
        b.cpu.af.b.h = 0x77;
        b.load(0, {0xd3, 0x34, 0xdb, 0x12});
        b.step();
        CHECK_EQ(b.last_port, 0x3434);
        CHECK_EQ(b.last_out, 0x77);
        b.step();
        CHECK_EQ(b.last_in_port, 0x1212);
        CHECK_EQ(b.cpu.af.b.h, 0x5a);
    }
}

// --- 8035 -------------------------------------------------------------------------

struct Bench48 {
    uint8_t    rom[0x1000] = {};
    i8035::Cpu cpu;
    uint8_t    last_port = 0xee, last_out = 0, ext_addr = 0, ext_val = 0, pins[3] = {0xff, 0xff, 0xff};
    bool       t[2] = {};
    int        outs = 0;
    Bench48()
    {
        cpu.rom = rom;
        cpu.ctx = this;
        cpu.port_out = [](void* c, uint8_t p, uint8_t v) {
            Bench48& b = *static_cast<Bench48*>(c);
            b.last_port = p;
            b.last_out = v;
            ++b.outs;
        };
        cpu.port_in = [](void* c, uint8_t p) { return static_cast<Bench48*>(c)->pins[p]; };
        cpu.ext_read = [](void*, uint8_t a) { return uint8_t(a ^ 0xa5); };
        cpu.ext_write = [](void* c, uint8_t a, uint8_t v) {
            static_cast<Bench48*>(c)->ext_addr = a;
            static_cast<Bench48*>(c)->ext_val = v;
        };
        cpu.test_in = [](void* c, uint8_t n) { return uint8_t(static_cast<Bench48*>(c)->t[n & 1]); };
        cpu.reset();
    }
    void load(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) rom[at++ & 0xfff] = b;
    }
    int step() { return cpu.run(1); }
};

void test_i8035_timing()
{
    struct Case { std::initializer_list<uint8_t> code; int cycles; const char* what; };
    const Case cases[] = {
        {{0x00}, 1, "NOP"},             {{0x23, 0x05}, 2, "MOV A,#"},      {{0x68}, 1, "ADD A,R0"},
        {{0x60}, 1, "ADD A,@R0"},       {{0x03, 0x01}, 2, "ADD A,#"},      {{0x04, 0x10}, 2, "JMP"},
        {{0x14, 0x10}, 2, "CALL"},      {{0xe8, 0x00}, 2, "DJNZ"},         {{0xc6, 0x00}, 2, "JZ"},
        {{0x80}, 2, "MOVX A,@R0"},      {{0x90}, 2, "MOVX @R0,A"},         {{0xa3}, 2, "MOVP A,@A"},
        {{0xe3}, 2, "MOVP3"},           {{0x39}, 2, "OUTL P1,A"},          {{0x09}, 2, "IN A,P1"},
        {{0x08}, 2, "INS A,BUS"},       {{0x89, 0x01}, 2, "ORL P1,#"},     {{0x9a, 0x01}, 2, "ANL P2,#"},
        {{0xb8, 0x01}, 2, "MOV R0,#"},  {{0xb0, 0x01}, 2, "MOV @R0,#"},    {{0xa8}, 1, "MOV R0,A"},
        {{0x42}, 1, "MOV A,T"},         {{0x57}, 1, "DA A"},               {{0xb3}, 2, "JMPP @A"},
        {{0x83}, 2, "RET"},             {{0x93}, 2, "RETR"},               {{0xc5}, 1, "SEL RB0"},
        {{0x16, 0x00}, 2, "JTF"},       {{0x12, 0x00}, 2, "JB0"},          {{0x0c}, 2, "MOVD A,P4"},
    };
    for (const Case& k : cases) {
        Bench48 b;
        b.load(0, k.code);
        const int got = b.step();
        if (got != k.cycles) std::printf("8035 %s: %d cycles\n", k.what, got);
        CHECK_EQ(got, k.cycles);
    }
    {
        Bench48 b;                                // DJNZ R2 x 5: 5 x 2 cycles
        b.load(0, {0xba, 0x05, 0xea, 0x02, 0x00});
        b.step();
        CHECK_EQ(b.cpu.run(10), 10);
        CHECK_EQ(b.cpu.pc, 4);
    }
}

void test_i8035_alu()
{
    using namespace i8035;
    {
        Bench48 b;                                // BCD: 19 + 28 = 47
        b.load(0, {0x23, 0x19, 0x03, 0x28, 0x57});
        b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x41);
        CHECK_EQ(b.cpu.psw & (CY | AC), AC);
        b.step();
        CHECK_EQ(b.cpu.a, 0x47);
        CHECK_EQ(b.cpu.psw & CY, 0);
    }
    {
        Bench48 b;                                // BCD: 99 + 1 = (1)00
        b.load(0, {0x23, 0x99, 0x03, 0x01, 0x57});
        b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x00);
        CHECK_EQ(b.cpu.psw & CY, CY);
    }
    {
        Bench48 b;                                // F0 + 20 = (1)10; ADDC adds the carry
        b.load(0, {0x23, 0xf0, 0x03, 0x20, 0x13, 0x01});
        b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x10);
        CHECK_EQ(b.cpu.psw & (CY | AC), CY);
        b.step();
        CHECK_EQ(b.cpu.a, 0x12);
        CHECK_EQ(b.cpu.psw & CY, 0);
    }
    {
        Bench48 b;                                // RLC, RRC through the carry; RL, RR; SWAP; CPL A
        b.load(0, {0x23, 0x80, 0xf7, 0x67, 0x67, 0xe7, 0x77, 0x77, 0x47, 0x37});
        b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x00);
        CHECK_EQ(b.cpu.psw & CY, CY);
        b.step();
        CHECK_EQ(b.cpu.a, 0x80);
        CHECK_EQ(b.cpu.psw & CY, 0);
        b.step();
        CHECK_EQ(b.cpu.a, 0x40);
        b.step(); b.step(); b.step();             // RL, RR, RR: 80, 40, 20
        CHECK_EQ(b.cpu.a, 0x20);
        b.step();
        CHECK_EQ(b.cpu.a, 0x02);
        b.step();
        CHECK_EQ(b.cpu.a, 0xfd);
    }
    {
        Bench48 b;                                // XCHD, XCH, INC / DEC @R0, register banks
        b.cpu.ram[0x20] = 0x34;
        b.load(0, {0xb8, 0x20, 0x23, 0x12, 0x30, 0x10, 0xd5, 0xb8, 0x55, 0xc5, 0x20});
        b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x14);
        CHECK_EQ(b.cpu.ram[0x20], 0x32);
        b.step();
        CHECK_EQ(b.cpu.ram[0x20], 0x33);
        b.step(); b.step();                       // SEL RB1; MOV R0,#55
        CHECK_EQ(b.cpu.ram[0x18], 0x55);
        CHECK_EQ(b.cpu.ram[0x00], 0x20);
        b.step(); b.step();                       // SEL RB0; XCH A,@R0
        CHECK_EQ(b.cpu.a, 0x33);
        CHECK_EQ(b.cpu.ram[0x20], 0x14);
    }
    {
        Bench48 b;                                // MOV A,PSW reads bit 3 set; flags F0 / F1
        b.load(0, {0x97, 0xa7, 0xc7, 0x95, 0xb6, 0x10, 0x00});
        b.load(0x10, {0xb5, 0x76, 0x20});
        b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x88);
        b.step(); b.step();                       // CPL F0; JF0 -> 10
        CHECK_EQ(b.cpu.pc, 0x10);
        b.step(); b.step();                       // CPL F1; JF1 -> 20
        CHECK_EQ(b.cpu.pc, 0x20);
    }
}

void test_i8035_flow()
{
    using namespace i8035;
    {
        Bench48 b;                                // CALL / RETR restores the PSW's top bits, RET doesn't
        b.load(0, {0x14, 0x40, 0x14, 0x50, 0x00});
        b.load(0x40, {0xa7, 0x93});
        b.load(0x50, {0xa7, 0x83});
        b.step();
        CHECK_EQ(b.cpu.pc, 0x40);
        CHECK_EQ(b.cpu.psw & 7, 1);
        CHECK_EQ(b.cpu.ram[8], 0x02);             // return address, PSW bits
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x02);
        CHECK_EQ(b.cpu.psw, 0);                   // the carry set in the routine is gone
        b.step(); b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x04);
        CHECK_EQ(b.cpu.psw, CY);
    }
    {
        Bench48 b;                                // eight levels, then the stack pointer wraps
        b.load(0, {0x14, 0x00});
        for (int i = 0; i < 9; ++i) b.step();
        CHECK_EQ(b.cpu.psw & 7, 1);
    }
    {
        Bench48 b;                                // MOVP / MOVP3 / JMPP read program memory
        b.load(0x020, {0x77});
        b.load(0x320, {0x88});
        b.load(0x040, {0x60});
        b.load(0, {0x23, 0x20, 0xa3, 0x23, 0x20, 0xe3, 0x23, 0x40, 0xb3});
        b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x77);
        b.step(); b.step();
        CHECK_EQ(b.cpu.a, 0x88);
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x60);
    }
    {
        Bench48 b;                                // a conditional jump stays in its address byte's page
        b.load(0, {0x04, 0xfe});
        b.load(0xfe, {0xc6, 0x30});               // JZ at 0FE: its address byte in page 0
        b.load(0x2ff, {0xc6, 0x40});              // JZ at 2FF: in page 3
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x030);
        b.cpu.pc = 0x2ff;
        b.step();
        CHECK_EQ(b.cpu.pc, 0x340);
    }
    {
        Bench48 b;                                // SEL MB1: JMP / CALL go to the upper 2 KB
        b.load(0, {0xf5, 0x04, 0x10});
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x810);
        b.load(0x810, {0x04, 0x20});              // the bank stays for the next jump
        b.step();
        CHECK_EQ(b.cpu.pc, 0x820);
        b.load(0x820, {0x07, 0xff});              // and a PC count wraps inside the bank
        b.cpu.pc = 0xfff;
        b.step();
        CHECK_EQ(b.cpu.pc, 0x800);
    }
}

void test_i8035_timer_irq()
{
    {
        Bench48 b;                                // timer: +1 every 32 cycles; JTF sees the overflow once
        b.load(0, {0x55, 0x04, 0x01});
        b.step();
        CHECK_EQ(b.cpu.run(8190), 8190);
        CHECK_EQ(b.cpu.timer, 255);
        CHECK(!b.cpu.timer_flag);
        b.cpu.run(2);
        CHECK_EQ(b.cpu.timer, 0);
        CHECK(b.cpu.timer_flag);
        CHECK(!b.cpu.timer_overflow);             // no interrupt pending while disabled
        b.load(1, {0x16, 0x10});
        b.cpu.pc = 1;
        b.step();
        CHECK_EQ(b.cpu.pc, 0x10);
        CHECK(!b.cpu.timer_flag);
    }
    {
        Bench48 b;                                // the timer interrupt: a CALL to 7, once per overflow
        b.load(0, {0x04, 0x09});
        b.load(3, {0x1e, 0x93});
        b.load(7, {0x1f, 0x93});
        b.load(9, {0x25, 0x55, 0x04, 0x0b});
        b.cpu.run(8192 * 2 + 64);
        CHECK_EQ(b.cpu.ram[7], 2);
        CHECK(!b.cpu.in_irq);
    }
    {
        Bench48 b;                                // the INT pin: level-sensitive, held off until RETR
        b.load(0, {0x04, 0x09});
        b.load(3, {0x1e, 0x93});
        b.load(9, {0x86, 0x20, 0x05, 0x00, 0x04, 0x0b});
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x0b);                 // JNI not taken
        b.cpu.pc = 9;
        b.cpu.irq(true);
        b.step();
        CHECK_EQ(b.cpu.pc, 0x20);                 // JNI taken (interrupts still disabled)
        b.cpu.pc = 0x0b;
        b.step();                                 // EN I
        CHECK_EQ(b.step(), 2);                    // taken: 2 cycles
        CHECK_EQ(b.cpu.pc, 3);
        CHECK(b.cpu.in_irq);
        b.step();
        CHECK_EQ(b.cpu.ram[6], 1);
        b.cpu.timer_overflow = true;              // nothing else gets in until RETR
        b.cpu.tirq_enabled = true;
        CHECK_EQ(b.cpu.pc, 4);
        b.step();                                 // RETR
        CHECK(!b.cpu.in_irq);
        CHECK_EQ(b.cpu.pc, 0x0c);
        b.step();
        CHECK_EQ(b.cpu.pc, 3);                    // still asserted: in again (external first)
        b.cpu.irq(false);
        b.step(); b.step(); b.step();             // INC R6, RETR, then the timer's
        CHECK_EQ(b.cpu.pc, 7);
        CHECK_EQ(b.cpu.ram[6], 2);
    }
    {
        Bench48 b;                                // inside an interrupt, JMP ignores SEL MB1
        b.load(0, {0xf5, 0x05, 0x00});
        b.load(3, {0x04, 0x30});
        b.step(); b.step();
        b.cpu.irq(true);
        b.step(); b.step();
        CHECK_EQ(b.cpu.pc, 0x030);
    }
    {
        Bench48 b;                                // event counter: T1's falling edges
        b.load(0, {0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42});
        b.t[1] = true;
        b.step();
        for (int i = 0; i < 6; ++i) {
            b.t[1] = (i & 1) != 0;
            b.step();
        }
        b.step();
        CHECK_EQ(b.cpu.a, 3);
    }
}

void test_i8035_ports()
{
    Bench48 b;
    b.pins[1] = 0x3c;
    b.pins[0] = 0x5a;
    b.load(0, {0x23, 0x81, 0x39, 0x99, 0xf0, 0x09, 0x8a, 0x0f, 0x08, 0xb8, 0x12, 0x80, 0xb9, 0x34, 0x91,
               0x02, 0x36, 0x40, 0x26, 0x40});
    b.step(); b.step();
    CHECK_EQ(b.last_port, i8035::kP1);
    CHECK_EQ(b.last_out, 0x81);
    b.step();                                     // ANL P1,#F0
    CHECK_EQ(b.cpu.p1, 0x80);
    CHECK_EQ(b.last_out, 0x80);
    b.step();                                     // IN A,P1: the pins ANDed with the latch
    CHECK_EQ(b.cpu.a, 0x00);
    b.step();                                     // ORL P2,#0F (P2 was FF)
    CHECK_EQ(b.last_port, i8035::kP2);
    CHECK_EQ(b.last_out, 0xff);
    b.step();
    CHECK_EQ(b.cpu.a, 0x5a);                      // INS A,BUS
    b.step(); b.step();
    CHECK_EQ(b.cpu.a, 0x12 ^ 0xa5);               // MOVX A,@R0
    b.step(); b.step();
    CHECK_EQ(b.ext_addr, 0x34);                   // MOVX @R1,A
    CHECK_EQ(b.ext_val, 0x12 ^ 0xa5);
    b.step();
    CHECK_EQ(b.last_port, i8035::kBus);           // OUTL BUS,A
    b.step();
    CHECK_EQ(b.cpu.pc, 0x12);                     // JT0 not taken (T0 low)
    b.t[0] = true;
    b.step();
    CHECK_EQ(b.cpu.pc, 0x14);                     // JNT0 not taken
}

// --- WAV parser + sample player -------------------------------------------------

void put_le(std::vector<uint8_t>& v, uint32_t x, int bytes)
{
    for (int i = 0; i < bytes; ++i) v.push_back(uint8_t(x >> (8 * i)));
}

// A WAV file in memory: fmt (16 bytes, or 40 for EXTENSIBLE), an odd-sized
// LIST chunk before the data to test padding, then the data.
std::vector<uint8_t> make_wav(int format, int channels, uint32_t rate, int bits,
                              const std::vector<uint8_t>& data, bool extensible = false)
{
    std::vector<uint8_t> v = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    v.insert(v.end(), {'f', 'm', 't', ' '});
    put_le(v, extensible ? 40 : 16, 4);
    put_le(v, extensible ? 0xfffe : uint32_t(format), 2);
    put_le(v, uint32_t(channels), 2);
    put_le(v, rate, 4);
    put_le(v, rate * uint32_t(channels * bits / 8), 4);
    put_le(v, uint32_t(channels * bits / 8), 2);
    put_le(v, uint32_t(bits), 2);
    if (extensible) {
        put_le(v, 22, 2);                         // cbSize
        put_le(v, uint32_t(bits), 2);
        put_le(v, 0, 4);                          // channel mask
        put_le(v, uint32_t(format), 2);           // sub-format GUID (first 2 bytes count)
        for (int i = 0; i < 14; ++i) v.push_back(0);
    }
    v.insert(v.end(), {'L', 'I', 'S', 'T', 3, 0, 0, 0, 'a', 'b', 'c', 0});
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    put_le(v, uint32_t(data.size()), 4);
    v.insert(v.end(), data.begin(), data.end());
    const uint32_t riff = uint32_t(v.size() - 8);
    for (int i = 0; i < 4; ++i) v[4 + size_t(i)] = uint8_t(riff >> (8 * i));
    return v;
}

std::vector<uint8_t> sine16(double hz, uint32_t rate, int frames, int amp)
{
    std::vector<uint8_t> d;
    for (int i = 0; i < frames; ++i)
        put_le(d, uint16_t(int16_t(std::lround(amp * std::sin(2 * M_PI * hz * i / rate)))), 2);
    return d;
}

void test_wav()
{
    samples::Wav w;
    const auto mono16 = make_wav(1, 1, 11025, 16, sine16(1000, 11025, 500, 20000));
    CHECK(samples::parse_wav(mono16.data(), mono16.size(), w));
    CHECK_EQ(w.frames, 500u);
    CHECK_EQ(w.rate, 11025u);
    CHECK_EQ(w.bits, 16);
    CHECK_EQ(w.at(0), 0);
    CHECK(w.at(3) > 15000);                       // ~1/4 of a 1 kHz period at 11025 Hz

    // 8-bit stereo: unsigned, the two channels averaged.
    const std::vector<uint8_t> st8 = {192, 128, 64, 64, 128, 255};
    const auto stereo8 = make_wav(1, 2, 8000, 8, st8);
    CHECK(samples::parse_wav(stereo8.data(), stereo8.size(), w));
    CHECK_EQ(w.frames, 3u);
    CHECK_EQ(w.at(0), 8192);
    CHECK_EQ(w.at(1), -16384);
    CHECK_EQ(w.at(2), (0 + 127 * 256) / 2);

    // WAVE_FORMAT_EXTENSIBLE with the PCM sub-format.
    const auto ext = make_wav(1, 1, 22050, 16, sine16(440, 22050, 100, 1000), true);
    CHECK(samples::parse_wav(ext.data(), ext.size(), w));
    CHECK_EQ(w.frames, 100u);

    // A cut-short file plays what it holds; an odd last byte is dropped.
    auto cut = mono16;
    cut.resize(cut.size() - 101);
    CHECK(samples::parse_wav(cut.data(), cut.size(), w));
    CHECK_EQ(w.frames, 449u);

    // Rejected: float data, 24-bit, not RIFF, empty.
    const auto flt = make_wav(3, 1, 8000, 32, std::vector<uint8_t>(16, 0));
    CHECK(!samples::parse_wav(flt.data(), flt.size(), w));
    const auto b24 = make_wav(1, 1, 8000, 24, std::vector<uint8_t>(12, 0));
    CHECK(!samples::parse_wav(b24.data(), b24.size(), w));
    auto junk = mono16;
    junk[0] = 'X';
    CHECK(!samples::parse_wav(junk.data(), junk.size(), w));
    CHECK(!samples::parse_wav(nullptr, 0, w));
    CHECK_EQ(w.frames, 0u);
}

void test_sample_player()
{
    const int rate = 24000;
    // 1 kHz recorded at 11025 Hz, 1 s: plays at 1 kHz for 1 s at 24 kHz.
    const auto file = make_wav(1, 1, 11025, 16, sine16(1000, 11025, 11025, 16000));
    samples::Wav w;
    CHECK(samples::parse_wav(file.data(), file.size(), w));
    samples::Player p;
    p.init(rate);
    std::vector<int16_t> out(size_t(rate) * 3 / 2, 0);
    p.start(0, &w);
    CHECK(p.playing(0));
    p.mix(out.data(), int(out.size()));
    CHECK(!p.playing(0));
    int last = 0;
    for (size_t i = 0; i < out.size(); ++i) if (out[i]) last = int(i);
    std::printf("player: 11025 Hz recording, %d output samples (want 24000)\n", last + 1);
    CHECK(std::abs(last + 1 - rate) <= 3);
    const std::vector<int16_t> played(out.begin(), out.begin() + rate);
    const double hz = measure_hz(played, rate);
    std::printf("player: 1 kHz at 11025 Hz -> %.2f Hz at 24000\n", hz);
    CHECK(std::fabs(hz - 1000.0) < 1.0);
    int peak = 0;
    for (int16_t v : played) peak = std::max(peak, std::abs(int(v)));
    CHECK(peak > 15000 && peak <= 16000);         // interpolation doesn't overshoot

    // Looping, volume, stop, and mixing two channels into what is there.
    std::vector<uint8_t> flat;
    for (int i = 0; i < 100; ++i) put_le(flat, 8000, 2);
    const auto ff = make_wav(1, 1, 8000, 16, flat);
    samples::Wav fw;
    CHECK(samples::parse_wav(ff.data(), ff.size(), fw));
    p.start(1, &fw, true);
    p.set_volume(1, 128);
    std::vector<int16_t> o(10000, 100);
    p.mix(o.data(), int(o.size()));
    CHECK(p.playing(1));
    CHECK_EQ(o[5000], 100 + 4000);
    p.start(2, &fw, true);
    p.mix(o.data(), int(o.size()));
    CHECK_EQ(o[7000], 100 + 4000 + 4000 + 8000);
    p.stop(1);
    p.stop(2);
    CHECK(!p.playing(1));
    std::vector<int16_t> z(100, 0);
    p.mix(z.data(), 100);
    CHECK_EQ(z[50], 0);
    p.start(3, &fw, true);                        // saturates instead of wrapping
    std::vector<int16_t> hi(10, 30000);
    p.mix(hi.data(), 10);
    CHECK_EQ(hi[5], 32767);
}

// --- Space Invaders sound models -----------------------------------------------

double rms(const std::vector<int16_t>& s, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i) sum += double(s[i]) * s[i];
    return to > from ? std::sqrt(sum / double(to - from)) : 0;
}

// Magnitude of the `hz` component (single-bin DFT).
double tone_level(const std::vector<int16_t>& s, double hz, double rate)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        re += s[i] * std::cos(2 * M_PI * hz * double(i) / rate);
        im += s[i] * std::sin(2 * M_PI * hz * double(i) / rate);
    }
    return std::sqrt(re * re + im * im) / double(s.size());
}

void test_invaders_sound()
{
    using namespace invaders_sound;
    const int rate = 24000;
    Synth s;
    s.init(rate);
    std::vector<int16_t> out(rate);

    // Silence when nothing is switched on.
    s.render(out.data(), rate);
    CHECK(rms(out, 0, out.size()) < 1);

    // Fleet notes: the 555 with R1 = 40k (bit 0) and 100k (bit 3).
    s.write_port5(0x01);
    s.render(out.data(), rate);
    const double f1 = measure_hz(std::vector<int16_t>(out.begin() + rate / 4, out.end()), rate);
    s.write_port5(0x08);
    s.render(out.data(), rate);
    const double f4 = measure_hz(std::vector<int16_t>(out.begin() + rate / 4, out.end()), rate);
    std::printf("invaders sound: fleet notes %.1f / %.1f Hz (want ~75.9 / ~57.7)\n", f1, f4);
    CHECK(std::fabs(f1 - 75.9) < 1.5);
    CHECK(std::fabs(f4 - 57.7) < 1.5);
    CHECK(rms(out, 0, out.size()) > 1000);
    s.write_port5(0x00);                          // the thump rings out
    s.render(out.data(), rate);
    CHECK(rms(out, rate / 2, out.size()) < 50);

    // Extra ship: 480 Hz, switched on and off ~7.4 times a second.
    s.write_port3(0x10);
    s.render(out.data(), rate);
    const double at480 = tone_level(out, 480, rate), at380 = tone_level(out, 380, rate);
    int bursts = 0;
    bool on = false;
    for (int w = 0; w < rate; w += rate / 200) {
        const bool now = rms(out, size_t(w), size_t(w + rate / 200)) > 500;
        if (now && !on) ++bursts;
        on = now;
    }
    std::printf("invaders sound: extra ship 480 Hz level %.0f (380 Hz: %.0f), %d bursts a second\n",
                at480, at380, bursts);
    CHECK(at480 > 10 * at380);
    CHECK(bursts >= 7 && bursts <= 8);
    s.write_port3(0x00);
    s.render(out.data(), rate / 10);

    // Player hit: a long rumble from one edge, gone after ~2.5 s.
    s.write_port3(0x04);
    std::vector<int16_t> bh(size_t(rate) * 3);
    s.render(bh.data(), int(bh.size()));
    const double bh_start = rms(bh, 0, rate / 2), bh_end = rms(bh, size_t(rate) * 5 / 2, bh.size());
    std::printf("invaders sound: player hit rms %.0f, after 2.5 s %.0f\n", bh_start, bh_end);
    CHECK(bh_start > 1500);
    CHECK(bh_end < bh_start / 20);

    // Invader hit and shot are one-shots: short even while the bit stays set.
    s.write_port3(0x08);
    s.render(out.data(), rate);
    CHECK(rms(out, 0, rate / 10) > 1000);
    CHECK(rms(out, rate * 3 / 4, out.size()) < 50);
    s.write_port3(0x02);
    s.render(out.data(), rate);
    const double shot_start = rms(out, 0, rate / 10), shot_end = rms(out, rate * 3 / 4, out.size());
    std::printf("invaders sound: shot rms %.0f, after 0.75 s %.0f\n", shot_start, shot_end);
    CHECK(shot_start > 1000);
    CHECK(shot_end < shot_start / 5);
    s.write_port3(0x00);

    // UFO: sounds while set; skipped when a sample plays it.
    s.write_port3(0x01);
    s.render(out.data(), rate);
    CHECK(rms(out, 0, out.size()) > 1000);
    s.set_skip(1 << kUfo);
    s.render(out.data(), rate);
    CHECK(rms(out, rate / 10, out.size()) < 1);

    // UFO hit follows its bit.
    s.write_port5(0x10);
    s.render(out.data(), rate);
    const double uh_on = rms(out, rate / 2, out.size());
    CHECK(uh_on > 1000);
    s.write_port5(0x00);
    s.render(out.data(), rate * 3 / 4);
    s.render(out.data(), rate);
    CHECK(rms(out, 0, out.size()) < uh_on / 10);
}

// --- Space Invaders wiring -------------------------------------------------------

void test_invaders()
{
    static uint8_t rom[0x2000];
    std::memset(rom, 0, sizeof rom);
    const uint8_t prog[] = {
        0xf3,                   // DI
        0x31, 0x00, 0x24,       // LXI SP,2400
        0xc3, 0x40, 0x00,       // JMP 0040
        0x00,
        0xf5, 0x21, 0x00, 0x20, 0x34, 0xf1, 0xfb, 0xc9,   // 0008 RST 1: count at 2000
        0xf5, 0x21, 0x01, 0x20, 0x34, 0xf1, 0xfb, 0xc9,   // 0010 RST 2: count at 2001
    };
    std::memcpy(rom, prog, sizeof prog);
    const uint8_t loop[] = {
        0xfb,                   // 0040 EI
        0x3a, 0x10, 0x20,       // 0041 LDA 2010
        0xd3, 0x03,             //      OUT 3     - sound bits from RAM
        0xd3, 0x06,             //      OUT 6     - watchdog
        0xc3, 0x41, 0x00,       //      JMP 0041
    };
    std::memcpy(rom + 0x40, loop, sizeof loop);

    static invaders::Machine m;
    m.init({rom}, 24000);
    int16_t audio[1024];
    int samples = 0;
    for (int f = 0; f < 60; ++f) samples += m.run_frame(audio, 1024);
    CHECK_EQ(samples, 24184);                     // 24000 Hz / 59.54 Hz x 60
    auto& c = m.cpu();
    CHECK_EQ(c.rd(0x2000), 60);                   // one RST 1 and one RST 2 a frame
    CHECK_EQ(c.rd(0x2001), 60);
    CHECK_EQ(c.rd(0x6000), 60);                   // RAM mirror
    CHECK_EQ(c.rd(0xa001), 60);                   // A15 not decoded
    CHECK_EQ(c.rd(0x0001), 0x31);
    CHECK_EQ(c.rd(0x8001), 0x31);
    CHECK_EQ(c.rd(0x4000), 0x00);                 // empty sockets
    CHECK_EQ(m.watchdog_resets(), 0);

    // Inputs.
    auto in = [&](int port) { return c.port_in(c.ctx, uint16_t(port | (port << 8))); };
    CHECK_EQ(in(0), 0x08);
    CHECK_EQ(in(1), 0x09);
    CHECK_EQ(in(2), 0x00);                        // default DIPs: 3 ships, 1500, coin info on
    m.set_inputs(invaders::kCoin | invaders::kStart1 | invaders::kFire | invaders::kLeft);
    CHECK_EQ(in(1), 0x08 | 0x04 | 0x10 | 0x20);
    CHECK_EQ(in(5), 0x08 | 0x04 | 0x10 | 0x20);   // mirror
    CHECK_EQ(in(2), 0x30);                        // upright: the same panel on port 2
    m.set_inputs(invaders::kStart2 | invaders::kRight2);
    CHECK_EQ(in(1), 0x09 | 0x02);
    CHECK_EQ(in(2), 0x40);
    m.set_inputs(0);
    m.set_dips(invaders::kDipShips5 | invaders::kDipBonus1000 | invaders::kDipNoCoinInfo);
    CHECK_EQ(in(6), 0x8a);
    m.set_dips(invaders::kDipDefault);

    // The shifter: two data bytes, then the result at shifts 0, 5, 7.
    auto out = [&](int port, uint8_t v) { c.port_out(c.ctx, uint16_t(port | (port << 8)), v); };
    out(4, 0xab);
    out(4, 0xcd);
    out(2, 0);
    CHECK_EQ(in(3), 0xcd);
    out(2, 5);                                    // CDAB >> 3
    CHECK_EQ(in(3), 0xb5);
    CHECK_EQ(in(7), 0xb5);
    out(2, 0x0f);                                 // only 3 bits count
    CHECK_EQ(in(3), 0xd5);
    invaders::Machine::Shifter sh;
    sh.data(0x81);
    sh.data(0x03);                                // 0381
    sh.count = 7;
    CHECK_EQ(sh.result(), 0xc0);
    sh.count = 2;
    CHECK_EQ(sh.result(), 0x0e);

    // Video: hardware row r, bit b of byte k = display (r, 255 - 8k - b), in
    // the overlay's colours.
    const uint16_t white = 0xffff, red = 0xf904, green = 0x27e4, black = 0;
    c.wr(0x2400 + 20 * 32 + 0, 0x01);             // (20, 255): reserve-cannon strip, green
    c.wr(0x2400 + 5 * 32 + 0, 0x01);              // (5, 255): left of that strip, white
    c.wr(0x2400 + 100 * 32 + 26, 0x80);           // (100, 40): UFO strip, red
    c.wr(0x2400 + 223 * 32 + 19, 0x08);           // (223, 100): white
    c.wr(0x2400 + 0 * 32 + 8, 0x01);              // (0, 191): bases strip, green
    uint16_t line[224];
    m.render_line(255, line);
    CHECK_EQ(line[20], green);
    CHECK_EQ(line[5], white);
    CHECK_EQ(line[6], black);
    m.render_line(40, line);
    CHECK_EQ(line[100], red);
    CHECK_EQ(line[101], black);
    CHECK_EQ(line[99], black);
    m.render_line(41, line);
    CHECK_EQ(line[100], black);
    m.render_line(100, line);
    CHECK_EQ(line[223], white);
    m.render_line(191, line);
    CHECK_EQ(line[0], green);
    m.render_line(0, line);
    for (int x = 0; x < 224; ++x) if (line[x]) { CHECK_EQ(line[x], black); break; }

    // Sound: a recorded shot (a flat 0.1 s at 12 kHz) replaces the model.
    std::vector<uint8_t> flat;
    for (int i = 0; i < 1200; ++i) put_le(flat, 10000, 2);
    const auto file = make_wav(1, 1, 12000, 16, flat);
    samples::Wav shot;
    CHECK(samples::parse_wav(file.data(), file.size(), shot));
    const samples::Wav* wavs[invaders::kSampleCount] = {};
    wavs[invaders_sound::kShot] = &shot;
    m.set_samples(wavs);
    c.wr(0x2010, 0x22);                           // amplifier on + shot
    int at_level = 0, total = 0;
    for (int f = 0; f < 10; ++f) {
        const int n = m.run_frame(audio, 1024);
        for (int i = 0; i < n; ++i) at_level += audio[i] == 10000 * 160 / 256;
        total += n;
    }
    std::printf("invaders: shot sample held for %d samples (want ~2400)\n", at_level);
    CHECK(std::abs(at_level - 2400) <= 5);
    CHECK_EQ(m.sound_port(3), 0x22);

    // The modelled UFO (no sample), then the amplifier off = silence.
    c.wr(0x2010, 0x21);
    std::vector<int16_t> ufo;
    for (int f = 0; f < 30; ++f) {
        const int n = m.run_frame(audio, 1024);
        ufo.insert(ufo.end(), audio, audio + n);
    }
    CHECK(rms(ufo, 0, ufo.size()) > 1000);
    c.wr(0x2010, 0x01);
    m.run_frame(audio, 1024);
    const int n = m.run_frame(audio, 1024);
    bool silent = true;
    for (int i = 0; i < n; ++i) silent = silent && audio[i] == 0;
    CHECK(silent);

    // Without port 6 writes the board resets after 255 frames.
    rom[0x46] = 0x00; rom[0x47] = 0x00;           // OUT 6 -> NOPs
    m.init({rom}, 24000);
    for (int f = 0; f < 600; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(m.watchdog_resets(), 2);
}

// --- Donkey Kong wiring -------------------------------------------------------------

void test_dkong()
{
    static uint8_t cpu_rom[0x4000], snd[0x1000], tiles[0x1000], sprites[0x2000], proms[0x300];
    std::memset(cpu_rom, 0, sizeof cpu_rom);
    std::memset(snd, 0, sizeof snd);
    std::memset(tiles, 0, sizeof tiles);
    std::memset(sprites, 0, sizeof sprites);
    std::memset(proms, 0xff, 0x200);              // all PROM bits set = black (stored inverted)
    std::memset(proms + 0x200, 1, 0x100);         // every tile column in colour 1
    // Colour 1: pixel value 1 red, 3 blue.
    proms[4 + 1] = 0x0f; proms[0x100 + 4 + 1] = 0x01;
    proms[4 + 3] = 0x0c; proms[0x100 + 4 + 3] = 0x0f;
    // Tile 2: hardware pixel (0, 0) = 1. Sprite 1: every pixel 3.
    tiles[2 * 8] = 0x80;
    for (int py = 0; py < 16; ++py)
        for (int part = 0; part < 4; ++part) sprites[part * 0x800 + 16 + py] = 0xff;

    const uint8_t prog[] = {
        0xf3,                   // 0000 DI
        0x31, 0x00, 0x6c,       //      LD SP,6C00
        0x3e, 0x01,             //      LD A,1
        0x32, 0x84, 0x7d,       //      LD (7D84),A   - NMI on
        0x3a, 0x00, 0x7d,       // 0009 LD A,(7D00)   - IN2 (watchdog)
        0x32, 0x10, 0x60,       //      LD (6010),A
        0x3a, 0x20, 0x60,       //      LD A,(6020)
        0x32, 0x02, 0x7d,       //      LD (7D02),A   - stomp
        0x3a, 0x21, 0x60,       //      LD A,(6021)
        0x32, 0x00, 0x7c,       //      LD (7C00),A   - tune
        0x18, 0xec,             //      JR 0009
    };
    std::memcpy(cpu_rom, prog, sizeof prog);
    // NMI: count, then the sprite DMA 6900 -> 7000, 0x180 bytes.
    const uint8_t nmi[] = {
        0xf5, 0x21, 0x00, 0x60, 0x34,
        0x3e, 0x00, 0x32, 0x00, 0x78, 0x3e, 0x69, 0x32, 0x00, 0x78,     // ch 0 address 6900
        0x3e, 0x7f, 0x32, 0x01, 0x78, 0x3e, 0x41, 0x32, 0x01, 0x78,     // ch 0 count 017F
        0x3e, 0x00, 0x32, 0x02, 0x78, 0x3e, 0x70, 0x32, 0x02, 0x78,     // ch 1 address 7000
        0x3e, 0x7f, 0x32, 0x03, 0x78, 0x3e, 0x81, 0x32, 0x03, 0x78,     // ch 1 count
        0x3e, 0x53, 0x32, 0x08, 0x78,                                   // mode: ch 0 + 1, TC stop
        0x3e, 0x01, 0x32, 0x85, 0x7d, 0xaf, 0x32, 0x85, 0x7d,           // DRQ
        0xf1, 0xed, 0x45,                                               // POP AF; RETN
    };
    std::memcpy(cpu_rom + 0x66, nmi, sizeof nmi);
    // 8035: P2 = C0 (tune latch on MOVX, DAC full); a square on the DAC of
    // the tune latch's level (inverted), none while that reads 0.
    const uint8_t sprog[] = {
        0x23, 0xc0, 0x3a, 0xb8, 0x00,   // 000 MOV A,#C0; OUTL P2,A; MOV R0,#0
        0x80, 0xc6, 0x05,               // 005 MOVX A,@R0; JZ 005
        0x39, 0xb9, 0x14, 0xe9, 0x0b,   // 008 OUTL P1,A; MOV R1,#20; DJNZ R1,00B
        0x27, 0x39, 0xb9, 0x14, 0xe9, 0x11,   // 00D CLR A; OUTL P1,A; MOV R1,#20; DJNZ R1,011
        0x04, 0x05,                     // 013 JMP 005
    };
    std::memcpy(snd, sprog, sizeof sprog);

    static dkong::Machine m;
    m.init({cpu_rom, snd, tiles, sprites, proms}, 24000);
    auto& c = m.cpu();
    c.wr(0x6021, 0x0f);                           // tune F: the 8035 reads 0 - silent
    int16_t audio[1024];
    int samples = 0;
    for (int f = 0; f < 30; ++f) samples += m.run_frame(audio, 1024);
    CHECK_EQ(samples, 30 * 396);                  // 24000 Hz / 60.606 Hz
    CHECK_EQ(c.rd(0x6000), 30);                   // one NMI per frame
    CHECK_EQ(m.dma_count(), 30);
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(c.rd(0x6010) & 0x40, 0x40);          // 8035 P2 bit 4 clear -> IN2 bit 6 set
    const uint32_t quiet = m.dac_writes();        // a few before the tune was set

    // Inputs (active high) and DIPs.
    m.set_inputs(dkong::kRight | dkong::kJump | dkong::kStart1 | dkong::kCoin);
    CHECK_EQ(c.rd(0x7c00), 0x11);
    CHECK_EQ(c.rd(0x7d00) & 0x8c, 0x84);
    m.set_inputs(dkong::kUp | dkong::kLeft | dkong::kDown | dkong::kStart2);
    CHECK_EQ(c.rd(0x7c00), 0x0e);
    CHECK_EQ(c.rd(0x7d00) & 0x8c, 0x08);
    m.set_inputs(0);
    CHECK_EQ(c.rd(0x7d80), dkong::kDipDefault);
    CHECK_EQ(dkong::kDipDefault, 0x80);

    // Video: tile 2 at row 2, column 31 = hardware (248, 16); sprite 1 at
    // y byte 100, x byte 108: hardware x 100-115, lines 147-162; seventeen
    // more sprites on its lines, the last one past the 16-a-line limit.
    c.wr(0x7400 + 2 * 32 + 31, 2);
    const uint8_t sp0[4] = {100, 1, 1, 108};
    for (int k = 0; k < 4; ++k) c.wr(uint16_t(0x6900 + k), sp0[k]);
    for (int i = 1; i <= 16; ++i) {
        const uint8_t s[4] = {100, 1, 1, uint8_t(i == 16 ? 20 : 140)};
        for (int k = 0; k < 4; ++k) c.wr(uint16_t(0x6900 + i * 4 + k), s[k]);
    }
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x7000), 100);                  // copied by the DMA
    CHECK_EQ(c.rd(0x7043), 20);
    CHECK_EQ(m.video().sprite_count, 16);         // the 17th has no line left
    const uint16_t red = 0xf800, blue = 0x001f, black = 0;
    uint16_t line[224];
    // Flip screen off: display line y = hardware column 255 - y, x = line - 16.
    m.render_line(7, line);
    CHECK_EQ(line[0], red);
    CHECK_EQ(line[1], black);
    m.render_line(140, line);                     // hardware x 115: the sprite's right edge
    CHECK_EQ(line[130], black);
    CHECK_EQ(line[131], blue);
    CHECK_EQ(line[146], blue);
    CHECK_EQ(line[147], black);
    m.render_line(155, line);
    CHECK_EQ(line[131], blue);
    m.render_line(156, line);
    CHECK_EQ(line[131], black);
    m.render_line(255 - 12, line);                // the 17th sprite (x 12-27): dropped
    CHECK_EQ(line[131], black);
    // Flip screen on (as the upright game runs): the picture turns 180 degrees.
    c.wr(0x7d82, 1);
    m.run_frame(nullptr, 0);
    m.render_line(248, line);
    CHECK_EQ(line[223], red);
    CHECK_EQ(line[222], black);
    m.render_line(115, line);
    CHECK_EQ(line[223 - 131], blue);
    CHECK_EQ(line[223 - 130], black);
    // Palette bank 1: colour 17 has no pens set - black.
    c.wr(0x7d86, 1);
    m.run_frame(nullptr, 0);
    m.render_line(248, line);
    CHECK_EQ(line[223], black);

    // Sound: the tune latch reaches the 8035 inverted; its square on the
    // DAC (a small one: level 0F of FF) is heard. The stomp (latch bit 2) is heard on its own.
    auto level = [&](int frames) {
        std::vector<int16_t> all;
        for (int f = 0; f < frames; ++f) {
            const int n = m.run_frame(audio, 1024);
            all.insert(all.end(), audio, audio + n);
        }
        return rms(all, all.size() / 2, all.size());
    };
    CHECK(level(30) < 2);
    c.wr(0x6021, 0x00);                           // DAC square of level 0F
    const double square = level(10);
    CHECK_EQ(m.sound_cpu().p1 == 0x0f || m.sound_cpu().p1 == 0x00, 1);
    CHECK(m.dac_writes() > quiet + 1000u);
    c.wr(0x6021, 0x0f);
    const double after = level(60);
    c.wr(0x6020, 1);                              // stomp
    m.run_frame(audio, 1024);
    c.wr(0x6020, 0);
    const double stomp = level(15);
    std::printf("dkong: DAC square rms %.0f, silent %.1f, stomp rms %.0f\n", square, after, stomp);
    CHECK(square > 50);
    CHECK(after < 5);
    CHECK(stomp > 300);

    // Without IN2 reads the board resets after 256 frames.
    cpu_rom[0x0a] = 0x30; cpu_rom[0x0b] = 0x60;   // LD A,(6030)
    m.init({cpu_rom, snd, tiles, sprites, proms}, 24000);
    for (int f = 0; f < 600; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(m.watchdog_resets(), 2);
}

// --- Rally-X: scroll / radar maths, crash sound, wiring ---------------------------

void test_rallyx_maths()
{
    using namespace rallyx;
    // The playfield starts 3 pixels in: screen x 3 shows tilemap x 0.
    CHECK_EQ(playfield_x(0, 3), 0);
    CHECK_EQ(playfield_x(0, 0), 253);
    CHECK_EQ(playfield_x(250, 10), 1);            // wraps round the 256-pixel map
    CHECK_EQ(playfield_x(255, 223), 219);
    // Display line 0 is hardware line 16.
    CHECK_EQ(playfield_y(0, 0), 16);
    CHECK_EQ(playfield_y(240, 0), 0);
    CHECK_EQ(playfield_y(8, 223), 247);
    // The radar / score area shows tilemap columns 4-7, then 0-3.
    CHECK_EQ(radar_column(224), 4);
    CHECK_EQ(radar_column(255), 7);
    CHECK_EQ(radar_column(256), 0);
    CHECK_EQ(radar_column(287), 3);
    // Radar dots: attribute bit 0 clear adds 256 to X; Y counts up from the bottom.
    CHECK_EQ(dot_position(240, 217, 1).x, 240);
    CHECK_EQ(dot_position(240, 217, 1).y, 20);
    CHECK_EQ(dot_position(10, 217, 0).x, 266);
    CHECK_EQ(dot_position(0, 0, 1).y, 237);
    CHECK_EQ(dot_shape(0x0e), 0);
    CHECK_EQ(dot_shape(0x00), 7);
    CHECK_EQ(dot_shape(0x03), 6);
    CHECK_EQ(sprite_position(100, 0x80, 175).x, 356);
    CHECK_EQ(sprite_position(100, 0x3f, 175).x, 100);
    CHECK_EQ(sprite_position(100, 0, 175).y, 50);
}

void test_rallyx_bang()
{
    using rallyx_bang::Bang;
    // The noise register runs through all 2^18 - 1 states.
    uint32_t r = 1;
    int period = 0;
    do { r = Bang::lfsr_next(r); ++period; } while (r != 1 && period < (1 << 19));
    CHECK_EQ(period, (1 << 18) - 1);

    const int rate = 24000;
    Bang b;
    b.init(rate);
    std::vector<int16_t> out(size_t(rate) * 3);
    b.render(out.data(), rate);
    CHECK(rms(out, 0, rate) < 1);
    CHECK(!b.active());

    // A short pulse on BANG: loud at once, then the capacitor runs down.
    b.set_trigger(true);
    b.render(out.data(), rate / 20);
    b.set_trigger(false);
    b.render(out.data() + rate / 20, int(out.size()) - rate / 20);
    const double start = rms(out, 0, rate / 5), late = rms(out, size_t(rate) * 3 / 2, size_t(rate) * 2);
    std::printf("rallyx bang: rms %.0f at the start, %.0f after 1.5 s\n", start, late);
    CHECK(start > 3000);
    CHECK(late < start / 20);
    CHECK(rms(out, size_t(rate) * 5 / 2, out.size()) < start / 200);
    // A rumble: the 840 Hz low-pass leaves little sample-to-sample change
    // (white noise would give ~1.4 x the signal).
    double d = 0, s = 0;
    for (int i = 1; i < rate / 5; ++i) {
        const double dv = double(out[size_t(i)]) - out[size_t(i - 1)];
        d += dv * dv;
        s += double(out[size_t(i)]) * out[size_t(i)];
    }
    CHECK(std::sqrt(d / s) < 0.5);
    // Quiet again: nothing computed, nothing added.
    for (int k = 0; k < 3; ++k) b.render(out.data(), rate);
    CHECK(!b.active());
    std::vector<int16_t> keep(100, 1234);
    b.mix(keep.data(), 100);
    CHECK_EQ(keep[50], 1234);

    // Held on, it keeps sounding at full strength.
    b.set_trigger(true);
    b.render(out.data(), int(out.size()));
    CHECK(rms(out, size_t(rate) * 2, out.size()) > 3000);
    CHECK(b.charge() > 0.99f);
}

void test_rallyx()
{
    static uint8_t cpu_rom[0x4000];
    static uint8_t gfx[0x1000];
    static uint8_t dots[0x100];
    static uint8_t proms[0x160];
    static uint8_t sound[0x200];
    std::memset(cpu_rom, 0, sizeof cpu_rom);
    std::memset(gfx, 0, sizeof gfx);
    std::memset(dots, 3, sizeof dots);            // all transparent
    std::memset(proms, 0, sizeof proms);
    const uint8_t prog[] = {
        0xf3,                   // DI
        0x31, 0xf0, 0x9f,       // LD SP,9FF0
        0xed, 0x5e,             // IM 2
        0x3e, 0x3f, 0xed, 0x47, // LD A,3F; LD I,A
        0x3e, 0x80, 0xd3, 0x00, // LD A,80; OUT (0),A  - vector
        0x3e, 0x01,             // LD A,1
        0x32, 0x81, 0xa1,       // LD (A181),A         - IRQ enable
        0x32, 0x82, 0xa1,       // LD (A182),A         - sound on
        0xfb,                   // EI
        0x32, 0x80, 0xa0,       // loop: LD (A080),A   - watchdog
        0x18, 0xfb,             // JR loop
    };
    std::memcpy(cpu_rom, prog, sizeof prog);
    const uint8_t isr[] = {
        0xf5, 0xaf, 0x32, 0x81, 0xa1,   // PUSH AF; XOR A; LD (A181),A - acknowledge
        0x21, 0x00, 0x98, 0x34,         // LD HL,9800; INC (HL)
        0x3e, 0x01, 0x32, 0x81, 0xa1,   // LD A,1; LD (A181),A
        0xf1, 0xfb, 0xed, 0x4d,         // POP AF; EI; RETI
    };
    std::memcpy(cpu_rom + 0x100, isr, sizeof isr);
    cpu_rom[0x3f80] = 0x00; cpu_rom[0x3f81] = 0x01;   // vector table -> 0100

    // Palette: 1 red, 2 green, 3 blue (with the pull-down: 222 of 255);
    // the dots' bank: 17 red, 19 green. Colour 1 = entries 0 1 2 3; colour
    // 33 (bit 5 = covers sprites) = all 0.
    proms[1] = 0x07; proms[2] = 0x38; proms[3] = 0xc0;
    proms[17] = 0x07; proms[19] = 0x38;
    for (int p = 0; p < 4; ++p) proms[0x20 + 4 + p] = uint8_t(p);
    // Tile 1: all pixels 3. Tile 2: only the top-left pixel (x 0 is in
    // byte 8, planes in bits 7 and 3).
    std::memset(gfx + 16, 0xff, 16);
    gfx[32 + 8] = 0x88;
    // Sprite 2 (bytes 128-191): all pixels 3.
    std::memset(gfx + 128, 0xff, 64);
    // Dot shape 0, top row: pixel value 1 three times, then transparent.
    dots[0] = dots[1] = dots[2] = 1;

    static rallyx::Machine m;
    m.init({cpu_rom, gfx, dots, proms, sound}, 24000);
    int16_t audio[1024];
    int samples = 0;
    for (int f = 0; f < 30; ++f) samples += m.run_frame(audio, 1024);
    CHECK_EQ(samples, 30 * 396);                  // 24000 Hz / 60.606 Hz
    CHECK_EQ(m.cpu().rd(0x9800), 30);             // one IRQ per VBLANK
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(m.latch(), 0x06);
    CHECK_EQ(m.cpu().rd(0xa100), rallyx::kDipDefault);
    CHECK_EQ(m.cpu().rd(0xe100), rallyx::kDipDefault);   // A14 not decoded
    m.set_inputs(rallyx::kCoin | rallyx::kUp | rallyx::kSmoke | rallyx::kStart2);
    CHECK_EQ(m.cpu().rd(0xa000), 0xff & ~0xa2);
    CHECK_EQ(m.cpu().rd(0xa080), 0xff & ~0x40);   // bit 0 high: upright
    m.set_inputs(0);
    CHECK_EQ(m.cpu().rd(0x9000), 0xff);           // nothing there
    m.cpu().wr(0xd800, 0x5a);                     // work RAM mirror
    CHECK_EQ(m.cpu().rd(0x9800), 0x5a);
    m.cpu().wr(0x9800, 30);

    auto& c = m.cpu();
    // Playfield, scrolled 8 right and 8 down: tile 2 at row 3, column 1
    // shows at screen (3, 0); at column 2 mirrored (attribute bit 6 clear)
    // its pixel is the tile's last: (18, 0);
    // tile 1 upside down at row 4, column 3 fills line 8 from x 19.
    c.wr(0xa130, 8); c.wr(0xa140, 8);
    c.wr(0x8400 + 3 * 32 + 1, 2); c.wr(0x8c00 + 3 * 32 + 1, 0x41);
    c.wr(0x8400 + 3 * 32 + 2, 2); c.wr(0x8c00 + 3 * 32 + 2, 0x01);
    c.wr(0x8400 + 4 * 32 + 3, 2); c.wr(0x8c00 + 4 * 32 + 3, 0xc1);
    // Radar / score: tile 1 at row 2, column 4 (x 224-231) and column 0 (x 256-263).
    c.wr(0x8000 + 2 * 32 + 4, 1); c.wr(0x8800 + 2 * 32 + 4, 0x41);
    c.wr(0x8000 + 2 * 32 + 0, 1); c.wr(0x8800 + 2 * 32 + 0, 0x41);
    // Sprite slot 0: code 2, colour 1, at (100, 50); a tile that covers
    // sprites over screen x 99-106 of its rows (row 9, column 13).
    c.wr(0x8014, 2 << 2); c.wr(0x8015, 100);
    c.wr(0x8814, 175); c.wr(0x8815, 1);
    c.wr(0x8c00 + 9 * 32 + 13, 0x21);
    // Dots: one over the radar at (240, 20), one over the sprite at (110, 50).
    c.wr(0x8034, 240); c.wr(0x8834, 217); c.wr(0xa004, 0x0f);
    c.wr(0x8035, 110); c.wr(0x8835, 187); c.wr(0xa005, 0x0f);
    m.run_frame(nullptr, 0);

    const uint16_t red = 0xf800, green = 0x07e0, blue = 0x001b, black = 0;
    uint16_t line[rallyx::kScreenW];
    m.render_line(0, line);
    CHECK_EQ(line[3], blue);
    CHECK_EQ(line[2], black);
    CHECK_EQ(line[4], black);
    CHECK_EQ(line[18], blue);
    CHECK_EQ(line[17], black);
    CHECK_EQ(line[11], black);
    CHECK_EQ(line[224], blue);
    CHECK_EQ(line[231], blue);
    CHECK_EQ(line[232], black);
    CHECK_EQ(line[256], blue);
    CHECK_EQ(line[263], blue);
    CHECK_EQ(line[255], black);
    m.render_line(1, line);
    CHECK_EQ(line[3], black);
    m.render_line(15, line);                      // flipped: bottom row of the tile
    CHECK_EQ(line[19], blue);
    CHECK_EQ(line[20], black);
    m.render_line(8, line);
    CHECK_EQ(line[19], black);
    // Sprite rows 50-65, x 100-115, hidden over x 99-106.
    m.render_line(50, line);
    CHECK_EQ(line[100], black);
    CHECK_EQ(line[106], black);
    CHECK_EQ(line[107], blue);
    CHECK_EQ(line[115], blue);
    CHECK_EQ(line[116], black);
    // The dot over the sprite: the sprite's entry (3) in the dots' bank.
    CHECK_EQ(line[110], green);
    CHECK_EQ(line[112], green);
    CHECK_EQ(line[113], blue);
    m.render_line(49, line);
    CHECK_EQ(line[110], black);
    m.render_line(65, line);
    CHECK_EQ(line[115], blue);
    m.render_line(66, line);
    CHECK_EQ(line[115], black);
    // The dot over the radar: tile entry 0 with the dot's low bits (1).
    m.render_line(20, line);
    CHECK_EQ(line[240], red);
    CHECK_EQ(line[242], red);
    CHECK_EQ(line[243], black);
    m.render_line(21, line);
    CHECK_EQ(line[240], black);
    // Mirrored sprite (attribute bit 0) and X bit 8.
    c.wr(0x8014, (2 << 2) | 1); c.wr(0x8015, 10); c.wr(0x8815, 0x81);
    m.run_frame(nullptr, 0);
    m.render_line(50, line);
    CHECK_EQ(line[265], black);
    CHECK_EQ(line[266], blue);
    CHECK_EQ(line[281], blue);
    CHECK_EQ(line[282], black);
    CHECK_EQ(line[107], black);

    // The crash sound: BANG (latch bit 0) adds noise to the frame's audio.
    for (int f = 0; f < 5; ++f) m.run_frame(audio, 1024);
    std::vector<int16_t> quiet(audio, audio + 396);
    c.wr(0xa180, 1);
    m.run_frame(audio, 1024);
    c.wr(0xa180, 0);
    std::vector<int16_t> loud(audio, audio + 396);
    CHECK(rms(quiet, 0, quiet.size()) < 1);
    CHECK(rms(loud, 0, loud.size()) > 2000);
    CHECK(m.bang().active());

    // Without watchdog writes the board resets after 16 frames.
    cpu_rom[0x17] = 0x18; cpu_rom[0x18] = 0xfe;   // loop: JR $
    m.init({cpu_rom, gfx, dots, proms, sound}, 24000);
    for (int f = 0; f < 40; ++f) m.run_frame(nullptr, 0);
    CHECK(m.watchdog_resets() >= 2);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "build/fixtures";
    test_crc32();
    test_zip(dir);
    test_romset(dir);
    test_wsg();
    test_z80_timing();
    test_z80_misc();
    test_z80_interrupts();
    test_z80_block_copy();
    test_pacman();
    test_i8080_timing();
    test_i8080_flags();
    test_i8080_interrupts();
    test_i8035_timing();
    test_i8035_alu();
    test_i8035_flow();
    test_i8035_timer_irq();
    test_i8035_ports();
    test_wav();
    test_sample_player();
    test_invaders_sound();
    test_invaders();
    test_rallyx_maths();
    test_rallyx_bang();
    test_rallyx();
    test_dkong();
    std::printf("unit tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
