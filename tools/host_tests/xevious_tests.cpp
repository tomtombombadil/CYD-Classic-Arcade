// Unit tests for the Xevious parts that can be checked without ROMs: the
// 50XX score keeper model, the 51XX's Xevious coinage / joystick mode, the
// board's RAM map, DIP switches and Blaster button, the map ROM lookup
// (BS / BB), the video (background, characters, sprites with their third
// plane, flips, sizes and priorities) on synthetic graphics, and idle
// skipping against runs without it on hand-made Z80 programs.
//
//   xevious_tests
#include "../../emu/machines/namco_io.h"
#include "../../emu/machines/xevious.h"

#include <cstdio>
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

// --- 50XX ------------------------------------------------------------------------

void read4(namco_io::Chip50& c, uint8_t out[4])
{
    c.select();
    for (int i = 0; i < 4; ++i) out[i] = c.read();
}

void test_chip50()
{
    namco_io::Chip50 c;
    c.reset();
    uint8_t r[4];
    // Xevious' power-on check: scores to 0, add 5, read; add 90, read.
    c.write(0x10);
    c.write(0x80);
    read4(c, r);
    CHECK_EQ(r[0], 0x00); CHECK_EQ(r[1], 0x00); CHECK_EQ(r[2], 0x00); CHECK_EQ(r[3], 0x05);
    c.write(0xe5);
    read4(c, r);
    CHECK_EQ(r[3], 0x95);
    CHECK_EQ(c.score(0), 95);
    // x10 / x100 tables: 9F = 5000, AD = 20000, CF = 10000, F3 = 600.
    for (uint8_t b : {0x9f, 0xad, 0xcf, 0xf3}) c.write(b);
    CHECK_EQ(c.score(0), 95 + 5000 + 20000 + 10000 + 600);
    read4(c, r);
    CHECK_EQ(r[0], 0x00); CHECK_EQ(r[1], 0x03); CHECK_EQ(r[2], 0x56); CHECK_EQ(r[3], 0x95);
    // Player 2, taking away (not below 0).
    c.write(0x68);
    c.write(0x8c);                              // +100
    c.write(0x71);
    c.write(0x81);                              // -10
    CHECK_EQ(c.score(1), 90);
    c.write(0xbf);                              // -1000
    CHECK_EQ(c.score(1), 0);
    c.write(0x70);
    c.write(0x60);
    c.write(0x81);
    CHECK_EQ(c.score(0), 35705);
    // Bonuses: first at 20,000, then every 50,000 (3 BCD bytes each); the
    // flags show once, in the first byte with the top digit.
    c.write(0x10);
    for (uint8_t b : {0x20, 0x02, 0x00, 0x00, 0x30, 0x05, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00}) c.write(b);
    c.write(0xcf);                              // 10,000
    read4(c, r);
    CHECK_EQ(r[0], 0x00);
    c.write(0xcf);                              // 20,000: first bonus
    read4(c, r);
    CHECK_EQ(r[0], 0x40);
    CHECK_EQ(r[1], 0x02);
    read4(c, r);
    CHECK_EQ(r[0], 0x00);                       // once
    for (int i = 0; i < 5; ++i) c.write(0xcf);  // 70,000: the next bonus, and past the high score
    read4(c, r);
    CHECK_EQ(r[0], 0xa0);
    CHECK_EQ(r[1], 0x07);
    for (int i = 0; i < 100; ++i) c.write(0xdf);    // 10,000,000 stops at 9,999,999
    read4(c, r);
    CHECK_EQ(r[0] & 0x0f, 9); CHECK_EQ(r[1], 0x99); CHECK_EQ(r[3], 0x99);
}

// --- 51XX in Xevious' mode -----------------------------------------------------------

void test_chip51_xevious()
{
    namco_io::Chip51 c;
    c.set_xevious(true);
    c.reset();
    // Command 1 + 6 bytes (the first two not coinage), then credit mode.
    for (uint8_t b : {1, 7, 7, 2, 1, 1, 1, 2}) c.write(b);
    CHECK_EQ(c.mode(), 1);
    uint8_t r[3];
    auto cycle = [&] { for (int i = 0; i < 3; ++i) r[i] = c.read(); };
    c.set_inputs(0xff, 0xff);
    cycle();
    c.set_inputs(0xff, uint8_t(~0x10));         // a coin: 2 coins a credit
    cycle();
    CHECK_EQ(r[0], 0x00);
    c.set_inputs(0xff, 0xff);
    cycle();
    c.set_inputs(0xff, uint8_t(~0x10));
    cycle();
    CHECK_EQ(r[0], 0x01);
    // The stick reads as a direction (remapping on): up-right = 1.
    c.set_inputs(0xfc, 0xff);
    cycle();
    CHECK_EQ(r[1] & 0x0f, 1);
    // Galaga's 4-byte coinage is unchanged without the mode.
    namco_io::Chip51 g;
    g.reset();
    for (uint8_t b : {1, 1, 1, 1, 1, 2}) g.write(b);
    CHECK_EQ(g.mode(), 1);
}

// --- the board, on hand-made programs ------------------------------------------------

struct XeviousRoms {
    uint8_t main[0x4000], sub[0x2000], sound[0x1000], chars[0x1000], tiles[0x2000], sprites[0x7000],
        map[0x4000], proms[0xb00], wave[0x200];

    XeviousRoms() { std::memset(this, 0, sizeof *this); }
    xevious::Roms roms() const { return {main, sub, sound, chars, tiles, sprites, map, proms, wave}; }
};

// Main CPU: releases the sub CPUs, turns on both IRQs and the sound NMIs,
// then runs Xevious' frame wait at 0437 (LD HL,(8000) / DEC L / JR NZ),
// counting frames in 8001. IRQ: kicks the watchdog and sets 8000 = 1.
// Sub CPU: Xevious' wait at 018D for 8002 (set to 1 by its IRQ), counting
// in 8010. Sound CPU: JR $ at 0098, counting NMIs in 8020.
void load_programs(XeviousRoms& r)
{
    put(r.main, 0x0000, {0x31, 0x00, 0x7c,          // LD SP,7C00
                         0xed, 0x56,                // IM 1
                         0x3e, 0x01,                // LD A,1
                         0x32, 0x23, 0x68,          // LD (6823),A  - sub CPUs run
                         0x32, 0x20, 0x68,          // LD (6820),A  - main IRQ on
                         0x32, 0x21, 0x68,          // LD (6821),A  - sub IRQ on
                         0xaf,                      // XOR A
                         0x32, 0x22, 0x68,          // LD (6822),A  - sound NMI on
                         0xfb,                      // EI
                         0xc3, 0x37, 0x04});        // JP 0437
    put(r.main, 0x0038, {0xf5, 0xaf, 0x32, 0x20, 0x68,          // PUSH AF; XOR A; LD (6820),A
                         0x32, 0x30, 0x68,                      // LD (6830),A - watchdog
                         0x3e, 0x01, 0x32, 0x00, 0x80,          // LD A,1; LD (8000),A
                         0x32, 0x20, 0x68,                      // LD (6820),A
                         0xf1, 0xfb, 0xc9});                    // POP AF; EI; RET
    put(r.main, 0x0437, {0x2a, 0x00, 0x80,          // LD HL,(8000)
                         0x2d,                      // DEC L
                         0x20, 0xfa,                // JR NZ,0437
                         0x24,                      // INC H
                         0x22, 0x00, 0x80,          // LD (8000),HL
                         0x18, 0xf4});              // JR 0437
    put(r.sub, 0x0000, {0x31, 0x00, 0x7e, 0xed, 0x56, 0xfb, 0xc3, 0x8a, 0x01});
    put(r.sub, 0x0038, {0xf5, 0xaf, 0x32, 0x21, 0x68,           // PUSH AF; XOR A; LD (6821),A
                        0x3e, 0x01, 0x32, 0x02, 0x80,           // LD A,1; LD (8002),A
                        0x32, 0x21, 0x68,                       // LD (6821),A
                        0xf1, 0xfb, 0xc9});                     // POP AF; EI; RET
    put(r.sub, 0x018a, {0x21, 0x02, 0x80,           // LD HL,8002
                        0x7e, 0x3d, 0x20, 0xfc,     // 018D: LD A,(HL); DEC A; JR NZ,018D
                        0x77,                       // LD (HL),A
                        0x3a, 0x10, 0x80, 0x3c, 0x32, 0x10, 0x80,  // INC (8010)
                        0x18, 0xef});               // JR 018A
    put(r.sound, 0x0000, {0x31, 0x00, 0x7f, 0xc3, 0x98, 0x00});
    put(r.sound, 0x0066, {0xf5, 0x3a, 0x20, 0x80, 0x3c, 0x32, 0x20, 0x80, 0xf1, 0xed, 0x45});
    put(r.sound, 0x0098, {0x18, 0xfe});
}

// The same frames with and without idle skipping: every register, R
// included, and the counters end the same; the waits are skipped.
void test_speedups()
{
    static XeviousRoms r;
    load_programs(r);
    struct Seen {
        uint16_t pc[3], hl[3], af[3];
        uint8_t  r[3];
        uint8_t  frames, sub, nmis;
    };
    std::vector<Seen> seen[2];
    uint32_t skipped[2][3] = {};
    static xevious::Machine machines[2];        // fresh ones: a board's time carries over a reset
    for (int speed = 0; speed < 2; ++speed) {
        xevious::Machine& m = machines[speed];
        m.init(r.roms(), 24000);
        m.set_speedups(speed != 0);
        for (int f = 0; f < 40; ++f) {
            m.run_frame(nullptr, 0);
            Seen s{};
            for (int k = 0; k < 3; ++k) {
                s.pc[k] = m.cpu(k).pc.w;
                s.hl[k] = m.cpu(k).hl.w;
                s.af[k] = m.cpu(k).af.w;
                s.r[k] = m.cpu(k).r;
                skipped[speed][k] += m.stats(k).skipped;
            }
            s.frames = m.peek(0x8001);
            s.sub = m.peek(0x8010);
            s.nmis = m.peek(0x8020);
            seen[speed].push_back(s);
        }
        CHECK_EQ(m.watchdog_resets(), 0);
    }
    bool same = true;
    for (size_t i = 0; i < seen[0].size(); ++i) same = same && std::memcmp(&seen[0][i], &seen[1][i], sizeof(Seen)) == 0;
    CHECK(same);
    CHECK_EQ(seen[0].back().frames, 40);
    CHECK_EQ(seen[0].back().sub, 40);
    CHECK_EQ(seen[0].back().nmis, 80);
    for (int k = 0; k < 3; ++k) {
        CHECK_EQ(skipped[0][k], 0);
        CHECK(skipped[1][k] > 40 * 30000u);
    }
}

// RAM and its mirrors, the DIP switches with the Blaster button, the map
// ROMs and the 06XX path to the 50XX.
void test_board_map()
{
    static XeviousRoms r;
    put(r.main, 0, {0x3e, 0x01, 0x32, 0x30, 0x68, 0x18, 0xf9});   // LD A,1; LD (6830),A; JR 0
    static xevious::Machine m;
    m.init(r.roms(), 24000);
    auto& c = m.cpu(namco_galaga::kMain);
    auto& s = m.cpu(namco_galaga::kSub);
    c.wr(0x7812, 0x12); c.wr(0x7c34, 0x34);
    CHECK_EQ(s.rd(0x7812), 0x12); CHECK_EQ(m.peek(0x7c34), 0x34);
    c.wr(0x8856, 0x56);                         // A11 isn't decoded above 8000
    CHECK_EQ(s.rd(0x8056), 0x56); CHECK_EQ(m.peek(0x8056), 0x56);
    c.wr(0x9078, 0x78);
    CHECK_EQ(s.rd(0x9878), 0x78);
    c.wr(0xa79a, 0x9a);
    CHECK_EQ(s.rd(0xaf9a), 0x9a); CHECK_EQ(m.peek(0xa79a), 0x9a);
    c.wr(0xcfff, 0xbc);
    CHECK_EQ(s.rd(0xcfff), 0xbc);

    // DIP switches: 6800 + n reads bank B bit n in bit 0, bank A's in bit 1.
    m.set_dips(0x5a);
    m.set_dips_b(0xfe);
    m.set_inputs(0);
    uint8_t a = 0, b = 0;
    for (int n = 0; n < 8; ++n) {
        const uint8_t v = c.rd(uint16_t(0x6800 + n));
        b |= uint8_t((v & 1) << n);
        a |= uint8_t(((v >> 1) & 1) << n);
    }
    CHECK_EQ(a, 0x5a);
    CHECK_EQ(b, 0xff);                          // bit 0 = the Blaster, not pressed (high)
    m.set_inputs(xevious::kBlaster);
    CHECK_EQ(c.rd(0x6800) & 1, 0);
    m.set_inputs(0);
    CHECK_EQ(c.rd(0x6800) & 1, 1);

    // The map ROMs: BS0 = 0x05, BS1 = 0x03 -> xvi_10 entry (3 & 7E) << 6 |
    // 5 >> 1 = 0x82 (even: its top 4 bits from xvi_9's low nibble at 0x41)
    // = 0x6ab: block 0x0ab, x flip (0x400), y flip (0x200). xvi_11 at
    // (0x0ab << 2 | 1 << 1 | 1) ^ 3.
    static XeviousRoms g;
    g.map[0x41] = 0x76;                         // low nibble 6 (high: the odd entry)
    g.map[0x1000 + 0x82] = 0xab;
    const int a2c = ((0x0ab << 2) | 3) ^ 3;
    g.map[0x3000 + a2c] = 0x81;                 // BB0: bits 6 / 7 swapped -> 0x41, then flips ^C0 -> 0x81
    g.map[0x3000 + (a2c | 0x800)] = 0x5c;
    put(g.main, 0, {0x3e, 0x01, 0x32, 0x30, 0x68, 0x18, 0xf9});
    static xevious::Machine x;
    x.init(g.roms(), 24000);
    auto& xc = x.cpu(namco_galaga::kMain);
    xc.wr(0xf000, 0x05);
    xc.wr(0xf001, 0x03);
    CHECK_EQ(xc.rd(0xf000), 0x81);
    CHECK_EQ(xc.rd(0xf001), 0x5c);
    CHECK_EQ(x.map_read(1), 0x5c);
    // The odd entry takes the high nibble (7): 0x7ab = block 0x1ab, both
    // flips.
    g.map[0x1000 + 0x83] = 0xab;
    const int a2c2 = ((0x1ab << 2) | 3) ^ 3;
    g.map[0x3000 + (a2c2 | 0x800)] = 0x33;
    xc.wr(0xf000, 0x07);
    CHECK_EQ(xc.rd(0xf001), 0x33);
}

// --- video ------------------------------------------------------------------------------

constexpr uint16_t kRed = 0xf800, kGreen = 0x07e0, kBlue = 0x001f;

void test_video()
{
    static XeviousRoms r;
    put(r.main, 0, {0x3e, 0x01, 0x32, 0x30, 0x68, 0x18, 0xf9});
    // Palette: 1 red, 2 green, 3 blue (4-bit guns).
    r.proms[1] = 0x0f; r.proms[0x100 + 2] = 0x0f; r.proms[0x200 + 3] = 0x0f;
    // Background colour 0: pens 0-3 = entries 0-3; colour 33 (attr 0x05 +
    // code bit 7 clear: (1 << 5) | 1): all pens entry 1.
    for (int p = 0; p < 4; ++p) r.proms[0x300 + p] = uint8_t(p);
    for (int p = 0; p < 4; ++p) r.proms[0x300 + 33 * 4 + p] = 1;
    // Sprite colour 1: pixel 7 -> entry 3 (opaque: bit 7), 3 -> entry 2,
    // the rest transparent.
    r.proms[0x700 + 8 + 7] = 0x03; r.proms[0x900 + 8 + 7] = 0x08;
    r.proms[0x700 + 8 + 3] = 0x02; r.proms[0x900 + 8 + 3] = 0x08;
    // Background tile 5: row 0 pixel 3, rows 1-7 pixel 2; tile 0x105 all 1.
    for (int y = 0; y < 8; ++y) {
        r.tiles[5 * 8 + y] = 0xff;
        r.tiles[0x1000 + 5 * 8 + y] = y == 0 ? 0xff : 0x00;
        r.tiles[0x1000 + 0x105 * 8 + y] = 0xff;
    }
    // Characters: 7 solid, 8 only row 0, 9 only column 0.
    std::memset(r.chars + 7 * 8, 0xff, 8);
    r.chars[8 * 8] = 0xff;
    std::memset(r.chars + 9 * 8, 0x80, 8);
    // Sprite 2: planes 0-1 all set (pixel 3), plane 2 (xvi_18 low nibbles)
    // set (pixel 7); sprite 130: planes 0-1 set, plane 2 = xvi_18's high
    // nibbles at the same place, clear; sprite 0x105: no third plane.
    std::memset(r.sprites + 2 * 64, 0xff, 64);
    std::memset(r.sprites + 0x5000 + 2 * 64, 0x0f, 64);
    std::memset(r.sprites + 130 * 64, 0xff, 64);
    std::memset(r.sprites + 0x105 * 64, 0xff, 64);
    static xevious::Machine m;
    m.init(r.roms(), 24000);
    auto& c = m.cpu(namco_galaga::kMain);
    uint16_t line[224];

    // Background tile 5 at row 10, column 8: lines 44-51 (x + 20),
    // hardware y 64-71 (+ 16) = display x 159 down to 152.
    c.wr(0xc800 + 10 * 64 + 8, 5);
    m.run_frame(nullptr, 0);
    m.render_line(44, line);
    CHECK_EQ(line[159], kBlue);
    CHECK_EQ(line[158], kGreen);
    CHECK_EQ(line[152], kGreen);
    CHECK_EQ(line[151], 0);
    CHECK_EQ(line[160], 0);
    m.render_line(43, line);
    CHECK_EQ(line[155], 0);
    m.render_line(51, line);
    CHECK_EQ(line[152], kGreen);
    m.render_line(52, line);
    CHECK_EQ(line[152], 0);
    // Flipped in y (attribute bit 7): row 0 at display x 152.
    c.wr(0xb800 + 10 * 64 + 8, 0x80);
    m.run_frame(nullptr, 0);
    m.render_line(44, line);
    CHECK_EQ(line[152], kBlue);
    CHECK_EQ(line[159], kGreen);
    // Scrolled: x by 8 (lines 36-43), y by 8 (display x 167 down to 160);
    // D020's A0 = bit 8 of y (512 - 256 = no change).
    c.wr(0xb800 + 10 * 64 + 8, 0x00);
    c.wr(0xd000, 8);
    c.wr(0xd021, 8);
    m.run_frame(nullptr, 0);
    m.render_line(36, line);
    CHECK_EQ(line[167], kBlue);
    CHECK_EQ(line[160], kGreen);
    m.render_line(44, line);
    CHECK_EQ(line[163], 0);
    c.wr(0xd000, 0);
    c.wr(0xd020, 0);
    // Attribute bit 0 = tile 0x105 and colour bit 5; bits 2-5 colour bits
    // 0-3: attr 0x05 -> colour 33.
    c.wr(0xb800 + 10 * 64 + 8, 0x05);
    m.run_frame(nullptr, 0);
    m.render_line(44, line);
    CHECK_EQ(line[155], kRed);
    c.wr(0xb800 + 10 * 64 + 8, 0x00);

    // Characters: 7 at row 12, column 6 (lines 16-23: x + 32; hardware y
    // 78-85: + 18 = display x 145 down to 138), colour 1 (attr 0x04) = red,
    // over the background; 0 transparent.
    c.wr(0xc000 + 12 * 64 + 6, 7);
    c.wr(0xb000 + 12 * 64 + 6, 0x04);
    m.run_frame(nullptr, 0);
    m.render_line(16, line);
    CHECK_EQ(line[145], kRed);
    CHECK_EQ(line[138], kRed);
    CHECK_EQ(line[146], 0);
    CHECK_EQ(line[137], 0);
    m.render_line(24, line);
    CHECK_EQ(line[140], 0);
    // Character 8 (row 0 only) at display x 145; flipped in y at 138.
    c.wr(0xc000 + 12 * 64 + 6, 8);
    m.run_frame(nullptr, 0);
    m.render_line(20, line);
    CHECK_EQ(line[145], kRed);
    CHECK_EQ(line[144], 0);
    c.wr(0xb000 + 12 * 64 + 6, 0x84);
    m.run_frame(nullptr, 0);
    m.render_line(20, line);
    CHECK_EQ(line[138], kRed);
    CHECK_EQ(line[145], 0);
    // Character 9 (column 0 only): line 16; flipped in x: line 23.
    c.wr(0xc000 + 12 * 64 + 6, 9);
    c.wr(0xb000 + 12 * 64 + 6, 0x04);
    m.run_frame(nullptr, 0);
    m.render_line(16, line);
    CHECK_EQ(line[141], kRed);
    m.render_line(23, line);
    CHECK_EQ(line[141], 0);
    c.wr(0xb000 + 12 * 64 + 6, 0x44);
    m.run_frame(nullptr, 0);
    m.render_line(23, line);
    CHECK_EQ(line[141], kRed);
    m.render_line(16, line);
    CHECK_EQ(line[141], 0);
    c.wr(0xc000 + 12 * 64 + 6, 0);

    // Sprite 0: code 2, colour 1, x 140 - 40 = 100 (lines 100-115), y 223 -
    // 123 = 100 (hardware y 100-115 = display x 123 down to 108): pixel 7 =
    // blue.
    c.wr(0xa780, 2); c.wr(0xa781, 1);
    c.wr(0x8780, 123); c.wr(0x8781, 140);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[123], kBlue);
    CHECK_EQ(line[108], kBlue);
    CHECK_EQ(line[124], 0);
    CHECK_EQ(line[107], 0);
    m.render_line(115, line);
    CHECK_EQ(line[115], kBlue);
    m.render_line(116, line);
    CHECK_EQ(line[115], 0);
    // Code 130: the third plane from xvi_18's high nibbles (clear): pixel 3
    // = green.
    c.wr(0xa780, 130);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[115], kGreen);
    // Code 0x105 (flag bit 7 + code 5): no third plane.
    c.wr(0xa780, 5);
    c.wr(0x9780, 0x80);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[115], kGreen);
    // Off (colour bit 6): nothing.
    c.wr(0xa781, 0x41);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[115], 0);
    // Double width (flag bit 0): codes 2 and 3 on lines 100-131; code 3 is
    // blank. Flipped in x: code 3 first, then 2 (lines 116-131).
    c.wr(0xa781, 1);
    c.wr(0xa780, 3);
    c.wr(0x9780, 0x01);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[115], kBlue);
    m.render_line(120, line);
    CHECK_EQ(line[115], 0);
    c.wr(0x9780, 0x05);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[115], 0);
    m.render_line(120, line);
    CHECK_EQ(line[115], kBlue);
    // Double height (flag bit 1): codes 0 (16 above, blank) and 2 (at y).
    c.wr(0xa780, 2);
    c.wr(0x9780, 0x02);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[115], kBlue);                 // code 2 at y 100-115
    CHECK_EQ(line[130], 0);                     // code 0 at y 84-99 = display x 139-124
    // ... flipped in y: code 2 on top (y 84-99).
    c.wr(0x9780, 0x0a);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[130], kBlue);
    CHECK_EQ(line[115], 0);
    // Characters over sprites: character 7 at the sprite's place.
    c.wr(0x9780, 0x00);
    const int col = (100 + 32) >> 3, row = (100 + 18) >> 3;     // line 100, hardware y 100
    c.wr(0xc000 + row * 64 + col, 7);
    c.wr(0xb000 + row * 64 + col, 0x04);
    m.run_frame(nullptr, 0);
    m.render_line(100, line);
    CHECK_EQ(line[123], kRed);
    CHECK_EQ(m.watchdog_resets(), 0);
}

} // namespace

int main()
{
    test_chip50();
    test_chip51_xevious();
    test_speedups();
    test_board_map();
    test_video();
    std::printf("xevious tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
