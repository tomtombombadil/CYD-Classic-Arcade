// Unit tests for the emulator modules that can be checked without ROMs:
// CRC-32, the ZIP reader and ROM set matcher (on fixtures written by
// make_fixtures.py), the Namco WSG (synthetic waveform PROM), Z80 timing /
// interrupts, and the Pac-Man machine wiring on a tiny hand-made program
// and synthetic graphics.
//
//   unit_tests <fixture dir>
#include "../../emu/cpu/z80.h"
#include "../../emu/machines/pacman.h"
#include "../../emu/rom/crc32.h"
#include "../../emu/rom/romset.h"
#include "../../emu/rom/zip.h"
#include "../../emu/sound/namco_wsg.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
    test_pacman();
    std::printf("unit tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
