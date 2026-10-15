// Unit tests for Burger Time's parts, with hand-made data - no ROMs:
// the AY-3-8910 (tones, noise, envelopes, mixer, bus, ports, lazy counters),
// the DECO CPU-7 opcode scramble, and the board's wiring on tiny programs
// (decryption in execution, VBLANK waits, coin IRQ, sound latch / NMI / AY
// writes, characters, sprites and background on screen).
//
//   btime_tests
#include "../../emu/machines/btime.h"
#include "../../emu/sound/ay8910.h"

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

// --- AY-3-8910 -------------------------------------------------------------------

// Runs `clocks` and returns channel ch's summed level.
uint32_t run_acc(ay8910::Chip& ay, uint32_t clocks, int ch)
{
    uint32_t acc[3] = {};
    ay.run(clocks, acc);
    return acc[ch];
}

void test_ay_levels_and_bus()
{
    using ay8910::kLevels;
    CHECK_EQ(kLevels[0], 0);
    CHECK_EQ(kLevels[15], ay8910::kMaxLevel);
    for (int i = 2; i < 16; ++i) {
        // ~3 dB a step: each level about sqrt 2 x the one below.
        CHECK(kLevels[i] > kLevels[i - 1]);
        CHECK(kLevels[i] * 100 > kLevels[i - 1] * 138 && kLevels[i] * 100 < kLevels[i - 1] * 144);
    }

    ay8910::Chip ay;
    ay.write_address(1);
    ay.write_data(0xff);
    CHECK_EQ(ay.reg(1), 0x0f);                  // coarse tone: 4 bits
    CHECK_EQ(ay.read_data(), 0x0f);
    ay.write(6, 0xff);
    CHECK_EQ(ay.reg(6), 0x1f);                  // noise period: 5 bits
    ay.write(8, 0xff);
    CHECK_EQ(ay.reg(8), 0x1f);                  // amplitude + envelope bit
    ay.write(13, 0xff);
    CHECK_EQ(ay.reg(13), 0x0f);
    // A high nibble in the address deselects the chip.
    ay.write_address(0x12);
    ay.write_data(0x55);
    CHECK_EQ(ay.reg(2), 0);
    CHECK_EQ(ay.read_data(), 0xff);
    ay.write_address(0x02);
    ay.write_data(0x55);
    CHECK_EQ(ay.reg(2), 0x55);

    // Ports: R7 bit 6 / 7 = output. Inputs read the pins.
    ay.write(14, 0xa5);
    ay.write(15, 0x3c);
    ay.write(7, 0x40);
    CHECK_EQ(ay.port_out(0), 0xa5);
    CHECK_EQ(ay.port_out(1), 0xff);
    ay.set_port_in(1, 0x81);
    ay.write_address(15);
    CHECK_EQ(ay.read_data(), 0x81);
    ay.write_address(14);
    CHECK_EQ(ay.read_data(), 0xa5);
}

void test_ay_tone()
{
    ay8910::Chip ay;
    ay.write(0, 100);                           // A: period 100 -> flips every 800 clocks
    ay.write(8, 15);
    ay.write(7, 0x3e);                          // tone A on, noise off everywhere
    CHECK_EQ(ay.level(0), 0);                   // the flip-flop starts low
    CHECK_EQ(run_acc(ay, 800, 0), 0);
    CHECK_EQ(ay.level(0), ay8910::kMaxLevel);
    CHECK_EQ(run_acc(ay, 800, 0), 800u * ay8910::kMaxLevel);
    CHECK_EQ(ay.level(0), 0);
    // Half a period in two pieces, across an odd clock boundary.
    CHECK_EQ(run_acc(ay, 403, 0) + run_acc(ay, 397, 0), 0);
    CHECK_EQ(run_acc(ay, 1, 0), uint32_t(ay8910::kMaxLevel));
    // Over many periods the average is half.
    const uint32_t many = run_acc(ay, 1600 * 50 - 1, 0);
    CHECK_EQ(many, 800u * 50 * ay8910::kMaxLevel - 1 * ay8910::kMaxLevel + 0);
    // A lowered period fires on the next tick.
    ay8910::Chip b;
    b.write(0, 200);
    b.write(8, 15);
    b.write(7, 0x3e);
    run_acc(b, 8 * 150, 0);                     // count 150
    b.write(0, 50);
    CHECK_EQ(run_acc(b, 8, 0), 0);
    CHECK_EQ(b.level(0), ay8910::kMaxLevel);
    // Periods 1-4 (above ~23 kHz) count as their average, half on.
    ay8910::Chip f;
    f.write(0, 0);
    f.write(8, 15);
    f.write(7, 0x3e);
    CHECK_EQ(f.level(0), ay8910::kMaxLevel / 2);
    f.write(0, 5);                              // 5 is exact again
    CHECK_EQ(f.level(0), 0);
    CHECK_EQ(run_acc(f, 40, 0), 0);
    CHECK_EQ(f.level(0), ay8910::kMaxLevel);
    // Tone and noise both off: a steady level the volume sets.
    ay8910::Chip s;
    s.write(7, 0x3f);
    s.write(9, 7);
    CHECK_EQ(s.level(1), ay8910::kLevels[7]);
    CHECK_EQ(run_acc(s, 1000, 1), 1000u * ay8910::kLevels[7]);
}

// The data sheet's LFSR: new bit 16 = bit 0 XOR bit 3.
uint32_t lfsr_step(uint32_t r) { return (r >> 1) | (((r ^ (r >> 3)) & 1) << 16); }

void test_ay_noise()
{
    ay8910::Chip ay;
    ay.write(6, 1);                             // a register step every 2 ticks = 16 clocks
    ay.write(8, 15);
    ay.write(7, 0x37);                          // noise on A only
    uint32_t ref = 1;
    for (int i = 0; i < 200; ++i) {
        CHECK_EQ(ay.level(0), (ref & 1) ? ay8910::kMaxLevel : 0);
        run_acc(ay, 16, 0);
        ref = lfsr_step(ref);
        if (ay.noise_register() != ref) break;
    }
    CHECK_EQ(ay.noise_register(), ref);
    // A long stretch at once (steps in chunks) lands on the same register.
    for (int i = 0; i < 1000; ++i) ref = lfsr_step(ref);
    run_acc(ay, 16000, 0);
    CHECK_EQ(ay.noise_register(), ref);
    // Period 5: a step every 10 ticks.
    ay.write(6, 5);
    run_acc(ay, 80 * 10, 0);
    for (int i = 0; i < 10; ++i) ref = lfsr_step(ref);
    CHECK_EQ(ay.noise_register(), ref);
    // Unheard (amplitude 0), the register is carried along lazily: the same
    // after sync().
    ay.write(8, 0);
    run_acc(ay, 80 * 1234, 0);
    for (int i = 0; i < 1234; ++i) ref = lfsr_step(ref);
    ay.sync();
    CHECK_EQ(ay.noise_register(), ref);
    // The period through 2^17 - 1 steps comes back to the start.
    uint32_t r = 1;
    int n = 0;
    do { r = lfsr_step(r); ++n; } while (r != 1 && n < 200000);
    CHECK_EQ(n, 131071);
}

// The data sheet's envelope shapes: volume after step n (0 = right after
// the shape write), for shapes 0-15.
int env_ref(int shape, int n)
{
    const bool cont = shape & 8, att = shape & 4, alt = shape & 2, hold = shape & 1;
    const int ramp = n / 16, pos = n % 16;
    const int first = att ? pos : 15 - pos;
    if (ramp == 0) return first;
    if (!cont) return 0;
    if (hold) return (alt ? !att : att) ? 15 : 0;
    const bool up = alt ? ((ramp & 1) ? !att : att) : att;
    return up ? pos : 15 - pos;
}

void test_ay_envelope()
{
    for (int shape = 0; shape < 16; ++shape) {
        ay8910::Chip ay;
        ay.write(11, 3);                        // a step every 6 ticks = 48 clocks
        ay.write(8, 0x10);                      // A follows the envelope
        ay.write(7, 0x3f);
        ay.write(13, uint8_t(shape));
        int bad = -1;
        for (int n = 0; n < 70; ++n) {
            if (ay.envelope_volume() != env_ref(shape, n) ||
                ay.level(0) != ay8910::kLevels[env_ref(shape, n)]) {
                bad = n;
                break;
            }
            run_acc(ay, 48, 0);
        }
        if (bad >= 0) std::printf("  envelope shape %d differs at step %d\n", shape, bad);
        CHECK_EQ(bad, -1);
        // Unheard, a long stretch at once, then sync().
        ay8910::Chip lazy;
        lazy.write(11, 3);
        lazy.write(7, 0x3f);
        lazy.write(13, uint8_t(shape));
        run_acc(lazy, 48 * 61, 0);
        lazy.sync();
        CHECK_EQ(lazy.envelope_volume(), env_ref(shape, 61));
    }
    // Period 0: a step every tick (twice as fast as period 1).
    ay8910::Chip z;
    z.write(8, 0x10);
    z.write(7, 0x3f);
    z.write(13, 0x0c);
    run_acc(z, 8 * 5, 0);
    CHECK_EQ(z.envelope_volume(), 5);
    z.write(11, 1);
    z.write(13, 0x0c);
    run_acc(z, 8 * 10, 0);
    CHECK_EQ(z.envelope_volume(), 5);
}

// Running clock by clock or in big pieces gives the same sums: the event
// search, the lazy counters and the chunked noise agree.
void test_ay_chunks()
{
    auto setup = [](ay8910::Chip& ay) {
        ay.write(0, 37);  ay.write(1, 0);       // A tone
        ay.write(2, 0x23); ay.write(3, 1);      // B tone
        ay.write(4, 3);                         // C: a fast tone, unheard noise
        ay.write(6, 7);
        ay.write(7, 0x2c);                      // tones A B on, noise A B on, C tone only
        ay.write(8, 12); ay.write(9, 0x10); ay.write(10, 0);
        ay.write(11, 9); ay.write(13, 0x0e);
    };
    ay8910::Chip a, b;
    setup(a);
    setup(b);
    uint32_t sa[3] = {}, sb[3] = {};
    const uint32_t pieces[] = {1, 7, 63, 64, 1000, 3, 9999, 62, 63, 40000};
    uint32_t total = 0;
    for (uint32_t p : pieces) {
        b.run(p, sb);
        for (uint32_t k = 0; k < p; ++k) a.run(1, sa);
        total += p;
        if (p == 1000) {                        // a register write in the middle
            a.write(10, 9);
            b.write(10, 9);
        }
    }
    CHECK_EQ(total, 51262u);
    for (int c = 0; c < 3; ++c) CHECK_EQ(sa[c], sb[c]);
    a.sync();
    b.sync();
    CHECK_EQ(a.noise_register(), b.noise_register());
    CHECK_EQ(a.envelope_volume(), b.envelope_volume());
    for (int c = 0; c < 3; ++c) CHECK_EQ(a.tone_output(c), b.tone_output(c));
}

// --- DECO CPU-7 -------------------------------------------------------------------

void test_deco7()
{
    using btime::deco7_decrypt;
    // No write before, or an address without bits 8 and 2: unchanged.
    for (int op = 0; op < 256; ++op) {
        CHECK_EQ(deco7_decrypt(0x0104, uint8_t(op), false), op);
        if (deco7_decrypt(0xc100, uint8_t(op), true) != op || deco7_decrypt(0xc004, uint8_t(op), true) != op)
            CHECK(false);
    }
    // Bits 7..0 from bits 6 5 3 4 2 7 1 0.
    const int from[8] = {0, 1, 7, 2, 4, 3, 5, 6};       // output bit n <- input bit from[n]
    for (int n = 0; n < 8; ++n) CHECK_EQ(deco7_decrypt(0xf1fc, uint8_t(1 << from[n]), true), 1 << n);
    // A permutation of the 256 values.
    bool seen[256] = {};
    for (int op = 0; op < 256; ++op) seen[deco7_decrypt(0x0104, uint8_t(op), true)] = true;
    int all = 0;
    for (bool s : seen) all += s;
    CHECK_EQ(all, 256);
    CHECK_EQ(deco7_decrypt(0xe5cc, 0x6e, true), 0xea);  // ROR abs -> NOP
    CHECK_EQ(deco7_decrypt(0xc104, 0x6c, true), 0xe8);  // JMP (ind) -> INX
}

// --- the board -------------------------------------------------------------------------

struct Board {
    std::vector<uint8_t> cpu, sound, gfx, tiles, map;
    btime::Machine* m = nullptr;

    Board() : cpu(0x4000, 0xea), sound(0x1000, 0xea), gfx(0x6000), tiles(0x1800), map(0x800)
    {
        // Sound CPU: idle loop at F006; NMI and IRQ handlers below.
        put_sound(0xf000, {0x4c, 0x06, 0xf0, 0xea, 0xea, 0xea, 0x4c, 0x06, 0xf0});
        put_sound(0xfffa, {0x10, 0xf0, 0x00, 0xf0, 0x20, 0xf0});
        put_sound(0xf010, {0x40});
        put_sound(0xf020, {0xad, 0x00, 0xa0, 0x40});    // LDA A000 (the latch); RTI
        // Main CPU: vectors; the program is put in by each test.
        put(0xfffa, {0x00, 0xc0, 0x00, 0xc0, 0x80, 0xc0});
        put(0xc080, {0x40});
    }
    ~Board() { delete m; }
    void put(uint16_t at, std::initializer_list<uint8_t> b)
    {
        for (uint8_t v : b) cpu[at++ - 0xc000] = v;
    }
    void put_sound(uint16_t at, std::initializer_list<uint8_t> b)
    {
        for (uint8_t v : b) sound[(at++ - 0xf000) & 0xfff] = v;
    }
    void start()
    {
        m = new btime::Machine;
        m->init({cpu.data(), sound.data(), gfx.data(), tiles.data(), map.data()}, 24000);
    }
    int frame(uint16_t buttons = 0)
    {
        int16_t audio[1024];
        m->set_inputs(buttons);
        return m->run_frame(audio, 1024);
    }
    uint8_t ram(uint16_t a) { return m->cpu().rd(a); }
};

void test_board_decryption()
{
    Board b;
    // C100: LDA #55; STA 10; [C104: 6C = INX scrambled]; INX; STA 11;
    // C108: INX; JMP C109 (to itself).
    b.put(0xfffc, {0x00, 0xc1});
    b.put(0xc100, {0xa9, 0x55, 0x85, 0x10, 0x6c, 0xe8, 0x85, 0x11, 0xe8, 0x4c, 0x09, 0xc1});
    b.start();
    b.frame();
    CHECK_EQ(b.m->cpu().x, 3);
    CHECK_EQ(b.ram(0x10), 0x55);
    CHECK_EQ(b.ram(0x11), 0x55);
    CHECK_EQ(b.m->cpu().pc, 0xc109);
}

void test_board_vblank_and_coin()
{
    Board b;
    // C000: CLI; wait for VBLANK; INC 20; wait for its end; JMP back.
    // IRQ (C080): INC 21; RTI.
    b.put(0xc000, {0x58, 0xad, 0x03, 0x40, 0x10, 0xfb, 0xe6, 0x20, 0xad, 0x03, 0x40, 0x30, 0xfb,
                   0x4c, 0x01, 0xc0});
    b.put(0xc080, {0xe6, 0x21, 0x40});
    b.start();
    for (int f = 0; f < 10; ++f) b.frame();
    CHECK_EQ(b.ram(0x20), 10);                  // one VBLANK a frame
    CHECK(b.m->main_cycles() < 200);            // the waits were skipped
    // A coin: one IRQ per press, however long it's held.
    b.frame(btime::kCoin);
    for (int f = 0; f < 4; ++f) b.frame(btime::kCoin);
    CHECK_EQ(b.ram(0x21), 1);
    b.frame();
    b.frame(btime::kCoin2);
    CHECK_EQ(b.ram(0x21), 2);
    CHECK_EQ(b.m->coin_irqs(), 2u);
    CHECK_EQ(b.ram(0x20), 17);
    // Inputs: P1 active low, starts low, coins high, DSW2 as set.
    Board c;
    c.put(0xc000, {0xad, 0x00, 0x40, 0x85, 0x30, 0xad, 0x02, 0x40, 0x85, 0x31, 0xad, 0x04, 0x40, 0x85,
                   0x32, 0x4c, 0x00, 0xc0});
    c.start();
    c.m->set_dips(0x5a);
    c.frame(btime::kLeft | btime::kPepper | btime::kStart1 | btime::kCoin);
    CHECK_EQ(c.ram(0x30), 0xff & ~0x12);
    CHECK_EQ(c.ram(0x31), 0x7e);
    CHECK_EQ(c.ram(0x32), 0x5a);
}

void test_board_sound()
{
    Board b;
    // Main: write 0B to the latch once, then idle.
    b.put(0xc000, {0xa9, 0x0b, 0x8d, 0x03, 0x40, 0x4c, 0x05, 0xc0});
    // Sound: enable NMI, CLI, idle. NMI: INC 00. IRQ: read the latch into
    // 01, INC 02, AY 1 register 8 = latch; RTI.
    b.put_sound(0xf000, {0xa9, 0x01, 0x8d, 0x00, 0xc0, 0x58, 0x4c, 0x06, 0xf0});
    b.put_sound(0xf010, {0xe6, 0x00, 0x40});
    b.put_sound(0xf020, {0xad, 0x00, 0xa0, 0x85, 0x01, 0xe6, 0x02, 0xa9, 0x08, 0x8d, 0x00, 0x40, 0xa5, 0x01,
                         0x8d, 0x00, 0x20, 0x40});
    b.start();
    int samples = 0;
    samples += b.frame();
    m6502::Cpu& s = b.m->sound_cpu();
    CHECK_EQ(s.rd(0x01), 0x0b);
    CHECK_EQ(s.rd(0x02), 1);                    // one IRQ: reading the latch clears it
    CHECK_EQ(b.m->ay(0).reg(8), 0x0b);
    CHECK_EQ(b.m->ay(1).reg(8), 0);
    CHECK_EQ(s.rd(0x1c01), 0x0b);               // RAM seen 8 times up to 1FFF
    const uint32_t n0 = b.m->sound_nmis();
    for (int f = 0; f < 56; ++f) samples += b.frame();
    CHECK_EQ(b.m->sound_nmis() - n0, 56u * 17); // every 16 lines
    CHECK_EQ(s.rd(0x00), uint8_t(b.m->sound_nmis()));
    CHECK(samples >= 23800 && samples <= 23820);   // 57 frames of 1 / 57.44 s at 24 kHz
}

void test_board_video()
{
    Board b;
    b.put(0xc000, {0x4c, 0x00, 0xc0});
    // Character 1: plane 0 all set, plane 2 row 0 bit 0 (its right pixel).
    for (int r = 0; r < 8; ++r) b.gfx[8 + r] = 0xff;
    b.gfx[0x4000 + 8] = 0x01;
    // Sprite 2 = characters 8-11: its left top quarter (character 10) value 3.
    for (int r = 0; r < 8; ++r) b.gfx[10 * 8 + r] = b.gfx[0x2000 + 10 * 8 + r] = 0xff;
    // Background tile 1 = tile characters 4-7, value 2; map page 1, column 0, row 1.
    for (int r = 0; r < 32; ++r) b.tiles[0x800 + 32 + r] = 0xff;
    b.map[256 + 1] = 1;
    b.start();
    m6502::Cpu& c = b.m->cpu();
    c.wr(0x0c00, 0xff);                         // pen 0 black
    c.wr(0x0c01, 0x00);                         // pen 1 white (stored inverted)
    c.wr(0x0c03, uint8_t(~0x38));               // pen 3 green
    c.wr(0x0c05, uint8_t(~0x07));               // pen 5 red
    c.wr(0x0c08, 0xff);                         // pen 8 black
    c.wr(0x0c0a, uint8_t(~0xc0));               // pen 10 blue
    c.wr(0x1000 + 33, 1);                       // row 1, column 1: the top left cell
    c.wr(0x1800 + 3 * 32 + 2, 1);               // x / y swapped: row 2, column 3
    b.frame();
    uint16_t line[240];
    b.m->render_line(0, line);
    CHECK_EQ(line[0], 0xf800);                  // the right pixel of row 0 = display (0, 0)
    for (int x = 1; x < 8; ++x) CHECK_EQ(line[x], 0xffff);
    CHECK_EQ(line[8], 0x0000);
    b.m->render_line(1, line);
    CHECK_EQ(line[0], 0xffff);
    b.m->render_line(8, line);
    CHECK_EQ(line[15], 0x0000);
    CHECK_EQ(line[16], 0xf800);
    CHECK_EQ(line[23], 0xffff);
    CHECK_EQ(line[24], 0x0000);

    // Sprite 0: code 2 at x register 100, y register 100: display x
    // 131-138, lines 100-107 for its left top quarter.
    c.wr(0x1000 + 0x00, 0x01);
    c.wr(0x1000 + 0x20, 2);
    c.wr(0x1000 + 0x40, 100);
    c.wr(0x1000 + 0x60, 100);
    b.m->render_line(100, line);
    CHECK_EQ(line[130], 0x0000);
    CHECK_EQ(line[131], 0x07e0);
    CHECK_EQ(line[138], 0x07e0);
    CHECK_EQ(line[139], 0x0000);
    b.m->render_line(107, line);
    CHECK_EQ(line[131], 0x07e0);
    b.m->render_line(99, line);
    CHECK_EQ(line[131], 0x0000);
    b.m->render_line(108, line);
    CHECK_EQ(line[131], 0x0000);
    c.wr(0x1000 + 0x00, 0x05);                  // flip x: the quarter moves to lines 92-99
    b.m->render_line(99, line);
    CHECK_EQ(line[131], 0x07e0);
    b.m->render_line(100, line);
    CHECK_EQ(line[131], 0x0000);

    // Background on (page 1): tile 1 at display x 8-23 on lines 0-8, under
    // the characters (whose pixel 0 is clear).
    c.wr(0x4004, 0x10);
    b.m->render_line(0, line);
    CHECK_EQ(line[0], 0xf800);                  // character over it
    CHECK_EQ(line[8], 0x001f);
    CHECK_EQ(line[23], 0x001f);
    CHECK_EQ(line[24], 0x0000);
    b.m->render_line(9, line);                  // the next column of tiles
    CHECK_EQ(line[8], 0x0000);
}

// A screen of 900 different characters (more than the glyph cache holds),
// drawn twice, against the hardware's layout computed straight from the
// ROM bits: video RAM offset (31 - column) x 32 + row holds the character
// at hardware x = 8 x column, y = 8 x row; display (x, y) = hardware
// (247 - y, x + 8).
void test_board_all_characters()
{
    Board b;
    b.put(0xc000, {0x4c, 0x00, 0xc0});
    uint32_t seed = 12345;
    for (uint8_t& v : b.gfx) {
        seed = seed * 1103515245u + 12345u;
        v = uint8_t(seed >> 16);
    }
    b.start();
    m6502::Cpu& c = b.m->cpu();
    for (int i = 0; i < 8; ++i) c.wr(uint16_t(0x0c00 + i), uint8_t(~(i * 0x25)));
    for (int r = 1; r <= 30; ++r)
        for (int col = 1; col <= 30; ++col) {
            const int code = ((r - 1) * 30 + col - 1) * 7 % 1024;
            c.wr(uint16_t(0x1000 + r * 32 + col), uint8_t(code));
            c.wr(uint16_t(0x1400 + r * 32 + col), uint8_t(code >> 8));
        }
    b.frame();
    // Pen i: palette RAM holds ~(i x 0x25); bits through the resistor weights.
    uint16_t pen[8], line[240];
    for (int i = 0; i < 8; ++i) {
        const uint8_t d = uint8_t(i * 0x25);
        const int w3[3] = {46, 65, 144}, w2[2] = {80, 175};
        int r = 0, g = 0, bl = 0;
        for (int k = 0; k < 3; ++k) {
            if (d & (1 << k)) r += w3[k];
            if (d & (8 << k)) g += w3[k];
        }
        for (int k = 0; k < 2; ++k)
            if (d & (0x40 << k)) bl += w2[k];
        pen[i] = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3));
    }
    int bad = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int y = 0; y < 240; ++y) {
            b.m->render_line(y, line);
            for (int x = 0; x < 240; ++x) {
                const int hx = 247 - y, hy = x + 8;
                const int offs = (31 - hx / 8) * 32 + hy / 8;
                const int code = b.m->cpu().rd(uint16_t(0x1000 + offs)) | ((b.m->cpu().rd(uint16_t(0x1400 + offs)) & 3) << 8);
                const int bit = 7 - (hx & 7), row = code * 8 + (hy & 7);
                const int v = ((b.gfx[0x4000 + row] >> bit) & 1) << 2 | ((b.gfx[0x2000 + row] >> bit) & 1) << 1 |
                              ((b.gfx[row] >> bit) & 1);
                if (line[x] != pen[v]) ++bad;
            }
        }
    CHECK_EQ(bad, 0);
    CHECK(b.m->glyph_decodes() > 1000);         // the cache was cycled through
}

} // namespace

int main()
{
    test_ay_levels_and_bus();
    test_ay_tone();
    test_ay_noise();
    test_ay_envelope();
    test_ay_chunks();
    test_deco7();
    test_board_decryption();
    test_board_vblank_and_coin();
    test_board_sound();
    test_board_video();
    test_board_all_characters();
    std::printf("btime tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
