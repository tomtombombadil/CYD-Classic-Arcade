// Unit tests for the Gyruss parts, with hand-made data - no ROMs: the
// Konami-1 opcode decryption through the 6809's opcode pages (and that a
// plain 6809 is unchanged), the AY-3-8910's quick path and closed-form
// tones (a chip run in any chunks gives the same sums and state), and the
// board on tiny programs for its four CPUs (main Z80 NMI, inputs and DIP
// switches, the sound command and IRQ, the sub CPU's IRQ, scanline and
// sprites - one rewritten as the beam goes down -, the AYs, the timer, the
// i8039's command, interrupt and DAC), and that skipping the four CPUs'
// idle loops leaves everything as it is without.
//
//   gyruss_tests
#include "../../emu/cpu/m6809.h"
#include "../../emu/machines/gyruss.h"
#include "../../emu/sound/ay8910.h"

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

// --- Konami-1 ----------------------------------------------------------------------

// Writes a 6809 program for a Konami-1: opcode bytes (marked by the
// caller) scrambled by address, operands as they are - or a plain 6809's.
struct Konami1Writer {
    uint8_t* rom;
    uint16_t base, at;
    bool scramble = true;
    void op(uint8_t v) { rom[at - base] = scramble ? gyruss::konami1_decrypt(at, v) : v; ++at; }
    void arg(uint8_t v) { rom[at - base] = v; ++at; }
    void arg16(uint16_t v) { arg(uint8_t(v >> 8)); arg(uint8_t(v)); }
};

// LDS #4400 (a page 2 opcode: both bytes scrambled); LDX #F800; LDA ,X
// (data reads are not scrambled); STA $4000; LDB #$5A; STB $4001; LDD
// #$1234; STD $4002; BRA *. Returns the BRA's address.
uint16_t test_program(uint8_t* rom, bool scramble)
{
    std::memset(rom, 0xff, 0x2000);
    Konami1Writer w{rom, 0xe000, 0xe000, scramble};
    w.op(0x10); w.op(0xce); w.arg16(0x4400);
    w.op(0x8e); w.arg16(0xf800);
    w.op(0xa6); w.arg(0x84);
    w.op(0xb7); w.arg16(0x4000);
    w.op(0xc6); w.arg(0x5a);
    w.op(0xf7); w.arg16(0x4001);
    w.op(0xcc); w.arg16(0x1234);
    w.op(0xfd); w.arg16(0x4002);
    const uint16_t idle = w.at;
    w.op(0x20); w.arg(0xfe);
    rom[0x1800] = 0x77;                                 // F800: data
    rom[0x1ffe] = 0xe0;                                 // the reset vector (data)
    rom[0x1fff] = 0x00;
    return idle;
}

uint8_t g_k1_rom[0x2000];
uint8_t g_k1_ram[0x400];
uint8_t k1_opcode(void*, uint16_t a) { return gyruss::konami1_decrypt(a, g_k1_rom[a - 0xe000]); }

void check_run(m6809::Cpu& c, uint16_t idle)
{
    std::memset(g_k1_ram, 0, sizeof g_k1_ram);
    c.map_ram(0x4000, 0x400, g_k1_ram);
    c.reset();
    c.run(200);
    CHECK_EQ(c.s, 0x4400);
    CHECK_EQ(g_k1_ram[0], 0x77);
    CHECK_EQ(g_k1_ram[1], 0x5a);
    CHECK_EQ(g_k1_ram[2], 0x12);
    CHECK_EQ(g_k1_ram[3], 0x34);
    CHECK_EQ(c.pc, idle);
}

void test_konami1()
{
    // The XOR by address bits 1 and 3.
    CHECK_EQ(gyruss::konami1_decrypt(0x0000, 0x00), 0x22);
    CHECK_EQ(gyruss::konami1_decrypt(0x0002, 0x00), 0x82);
    CHECK_EQ(gyruss::konami1_decrypt(0x0008, 0x00), 0x28);
    CHECK_EQ(gyruss::konami1_decrypt(0x000a, 0x00), 0x88);
    CHECK_EQ(gyruss::konami1_decrypt(0xf0b5, 0x12), 0x12 ^ 0x22);     // bits 0 / 2 don't count
    CHECK_EQ(gyruss::konami1_decrypt(0xf0b6, 0xa2), 0x20);           // the game's BRA * at F0B6

    // Opcode fetches through op_read.
    const uint16_t idle = test_program(g_k1_rom, true);
    m6809::Cpu c;
    c.map_read(0xe000, 0x2000, g_k1_rom);
    c.op_read = k1_opcode;
    c.map_opcodes(0xe000, 0x2000, nullptr);
    check_run(c, idle);

    // Through a decrypted copy in the opcode pages.
    static uint8_t ops[0x2000];
    for (int a = 0; a < 0x2000; ++a) ops[a] = gyruss::konami1_decrypt(uint16_t(0xe000 + a), g_k1_rom[a]);
    m6809::Cpu d;
    d.map_read(0xe000, 0x2000, g_k1_rom);
    d.map_opcodes(0xe000, 0x2000, ops);
    check_run(d, idle);

    // A plain 6809 (map_read sets the opcode pages too) runs the plain
    // program as before.
    static uint8_t plain[0x2000];
    CHECK_EQ(test_program(plain, false), idle);
    m6809::Cpu e;
    e.map_read(0xe000, 0x2000, plain);
    check_run(e, idle);
}

// --- AY-3-8910 ---------------------------------------------------------------------

// A chip run in chunks of different sizes adds the same levels over the
// same stretches, and ends in the same state: the quick path and the
// closed-form tones are exact.
void test_ay_chunks()
{
    struct Setting { uint8_t regs[14]; };
    const Setting settings[] = {
        {{10, 0, 3, 0, 200, 1, 0, 0x38, 15, 12, 9, 0, 0, 0}},           // fast-ish tones, no noise
        {{1, 0, 2, 0, 5, 0, 0, 0x38, 15, 15, 15, 0, 0, 0}},             // very fast (averaged)
        {{100, 0, 37, 0, 12, 0, 3, 0x30, 15, 0x10, 8, 20, 0, 0x0e}},    // noise on C, envelope on B
        {{7, 0, 0, 0, 0, 0, 1, 0x07, 15, 15, 15, 0, 0, 0}},             // noise only
        {{50, 0, 60, 0, 70, 0, 0, 0x38, 0x10, 0x10, 0x10, 0, 0, 0x08}}, // envelope period 0
        {{255, 15, 1, 0, 33, 2, 31, 0x00, 0, 15, 0x10, 255, 255, 0x0a}},
    };
    for (const Setting& st : settings) {
        ay8910::Chip a, b;
        for (int r = 0; r < 14; ++r) {
            a.write(r, st.regs[r]);
            b.write(r, st.regs[r]);
        }
        uint32_t seed = 12345;
        bool same = true;
        for (int sample = 0; sample < 3000; ++sample) {
            // a: one run a sample; b: the same sample in random pieces.
            const uint32_t n = 60 + (sample % 31);
            uint32_t acc_a[3] = {}, acc_b[3] = {};
            a.run(n, acc_a);
            for (uint32_t left = n; left;) {
                seed = seed * 1103515245u + 12345u;
                uint32_t k = 1 + (seed >> 16) % 23;
                if (k > left) k = left;
                b.run(k, acc_b);
                left -= k;
            }
            same = same && std::memcmp(acc_a, acc_b, sizeof acc_a) == 0;
            if (sample % 500 == 499) {                  // a register write now and then
                a.write(2, uint8_t(sample / 7));
                b.write(2, uint8_t(sample / 7));
            }
        }
        a.sync();
        b.sync();
        CHECK(same);
        for (int c = 0; c < 3; ++c) {
            CHECK_EQ(a.tone_output(c), b.tone_output(c));
            CHECK_EQ(a.level(c), b.level(c));
        }
        CHECK_EQ(a.noise_register(), b.noise_register());
        CHECK_EQ(a.envelope_volume(), b.envelope_volume());
    }

    // A tone's time high: period 100 = a square wave of 1600 clocks at
    // full level half the time.
    ay8910::Chip t;
    t.write(0, 100);
    t.write(7, 0x3e);
    t.write(8, 15);
    uint32_t acc[3] = {};
    t.run(1600 * 50, acc);
    CHECK_EQ(acc[0], 4096u * 800 * 50);
}

// --- the board -----------------------------------------------------------------------

struct Board {
    uint8_t cpu[0x6000], sub[0x2000], sound[0x4000], mcu[0x1000], sprites[0x8000], tiles[0x2000], proms[0x220];
};

void main_program(uint8_t* cpu)
{
    std::memset(cpu, 0, 0x6000);
    const uint8_t start[] = {
        0x31, 0x00, 0x9f,               // LD SP,9F00
        0x3e, 0x10, 0x32, 0x33, 0x94,   // (9433) = 10: the task entry 9510
        0x3e, 0x80, 0x32, 0x10, 0x95,   // (9510) = 80: nothing to do
        0x3e, 0x5a, 0x32, 0x00, 0xc1,   // the sound command 5A
        0x32, 0x80, 0xc0,               // the sound CPU's IRQ
        0x3e, 0x01, 0x32, 0x80, 0xc1,   // NMI on
        0xc3, 0xf4, 0x05,               // JP 05F4: the game's idle loop
    };
    std::memcpy(cpu, start, sizeof start);
    // NMI: count, the inputs to 9001-9005, a tile, then the NMI line down
    // and up again (as the game does).
    const uint8_t nmi[] = {
        0xf5,                                       // PUSH AF
        0x3a, 0x00, 0x90, 0x3c, 0x32, 0x00, 0x90,   // INC (9000)
        0x3a, 0x80, 0xc0, 0x32, 0x01, 0x90,         // SYSTEM
        0x3a, 0xa0, 0xc0, 0x32, 0x02, 0x90,         // P1
        0x3a, 0x00, 0xc0, 0x32, 0x03, 0x90,         // DSW2
        0x3a, 0x00, 0xc1, 0x32, 0x04, 0x90,         // DSW3
        0x3a, 0xe0, 0xc0, 0x32, 0x05, 0x90,         // DSW1
        0x3e, 0x01, 0x32, 0x42, 0x84,               // tile 0x42 (row 2, column 2) = 1
        0x3e, 0x13, 0x32, 0x42, 0x80,               // its colour 3, in front of sprites
        0xaf, 0x32, 0x80, 0xc1, 0x3c, 0x32, 0x80, 0xc1,   // NMI off, on
        0xf1, 0xed, 0x45,                           // POP AF; RETN
    };
    std::memcpy(cpu + 0x66, nmi, sizeof nmi);
    const uint8_t loop[] = {0xc3, 0xf7, 0x05, 0x3a, 0x65, 0x58, 0x67, 0x3a, 0x33, 0x94, 0x6f, 0x7e, 0x07,
                            0xda, 0xf4, 0x05};
    std::memcpy(cpu + 0x5f4, loop, sizeof loop);
    cpu[0x5865] = 0x95;
}

// Sub CPU: IRQ on, then the game's BRA * at F0B6. Its IRQ: drops and
// re-enables the line, counts at 4000, puts sprite slot 0 (code 1, at
// pixel 64, lines 120-135), waits for the beam to reach line 128 and
// changes it to code 2.
void sub_program(uint8_t* sub)
{
    std::memset(sub, 0xff, 0x2000);
    Konami1Writer w{sub, 0xe000, 0xe000};
    w.op(0x10); w.op(0xce); w.arg16(0x4400);       // LDS #4400
    w.op(0x86); w.arg(0x01);                       // LDA #1
    w.op(0xb7); w.arg16(0x2000);                   // STA $2000: IRQ on
    w.op(0x1c); w.arg(0xef);                       // ANDCC #$EF
    w.op(0x7e); w.arg16(0xf0b6);                   // JMP $F0B6
    w = {sub, 0xe000, 0xf0b6};
    w.op(0x20); w.arg(0xfe);                       // F0B6 BRA *
    w = {sub, 0xe000, 0xf100};
    w.op(0x4f);                                    // CLRA
    w.op(0xb7); w.arg16(0x2000);                   // the IRQ line down
    w.op(0x4c);                                    // INCA
    w.op(0xb7); w.arg16(0x2000);                   // and enabled again
    w.op(0x7c); w.arg16(0x4000);                   // INC $4000
    w.op(0x86); w.arg(64);   w.op(0xb7); w.arg16(0x4040);   // x
    w.op(0x86); w.arg(2);    w.op(0xb7); w.arg16(0x4041);   // code 1, bank 0
    w.op(0x86); w.arg(0x41); w.op(0xb7); w.arg16(0x4042);   // colour 1, no flips (bit 6 set = not flipped)
    w.op(0x86); w.arg(121);  w.op(0xb7); w.arg16(0x4043);   // y: line 241 - 121 = 120
    const uint16_t wrap = w.at;                    // (the IRQ comes at line 240)
    w.op(0xb6); w.arg16(0x0000);                   // LDA $0000: the beam's line
    w.op(0x81); w.arg(0x80);                       // CMPA #$80
    w.op(0x24); w.arg(uint8_t(wrap - (w.at + 1))); // BCC: until the next frame
    const uint16_t wait = w.at;
    w.op(0xb6); w.arg16(0x0000);
    w.op(0x81); w.arg(0x80);
    w.op(0x25); w.arg(uint8_t(wait - (w.at + 1))); // BCS: until line 128
    w.op(0xb7); w.arg16(0x4001);                   // the line it saw -> 4001 (RAM)
    w.op(0x86); w.arg(4);    w.op(0xb7); w.arg16(0x4041);   // code 2
    w.op(0x3b);                                    // RTI
    sub[0x1ff8] = 0xf1; sub[0x1ff9] = 0x00;        // IRQ vector
    sub[0x1ffe] = 0xe0; sub[0x1fff] = 0x00;        // reset
}

// Sound CPU: the game's timer waits at 00FE / 0105 (counting turns at
// 6002); its IRQ takes the command to 6000, counts at 6001, sets AY 1 (a
// tone, port B = the filters) and AY 5, and hands the command to the
// i8039.
void sound_program(uint8_t* snd)
{
    std::memset(snd, 0xff, 0x4000);
    const uint8_t start[] = {0x31, 0x00, 0x64, 0xed, 0x56, 0xc3, 0xfd, 0x00};   // LD SP,6400; IM 1; JP 00FD
    std::memcpy(snd, start, sizeof start);
    const uint8_t rst10[] = {0xd3, 0x08, 0xdb, 0x09, 0xc9};
    std::memcpy(snd + 0x10, rst10, sizeof rst10);
    const uint8_t isr[] = {
        0xf5,                                       // PUSH AF
        0x3a, 0x00, 0x80, 0x32, 0x00, 0x60,         // the command -> 6000
        0x3a, 0x01, 0x60, 0x3c, 0x32, 0x01, 0x60,   // INC (6001)
        0x3e, 0x07, 0xd3, 0x00, 0x3e, 0xbe, 0xd3, 0x02,   // AY 1 R7 = BE: tone A, port B out
        0x3e, 0x08, 0xd3, 0x00, 0x3e, 0x0f, 0xd3, 0x02,   // R8 = 0F
        0x3e, 0x00, 0xd3, 0x00, 0x3e, 0x40, 0xd3, 0x02,   // R0 = 40
        0x3e, 0x0f, 0xd3, 0x00, 0x3e, 0x09, 0xd3, 0x02,   // R15 = 09: filters
        0x3e, 0x07, 0xd3, 0x10, 0x3e, 0x3d, 0xd3, 0x12,   // AY 5 R7 = 3D: tone B
        0x3e, 0x09, 0xd3, 0x10, 0x3e, 0x0c, 0xd3, 0x12,   // R9 = 0C
        0x3e, 0x02, 0xd3, 0x10, 0x3e, 0x99, 0xd3, 0x12,   // R2 = 99
        0x3a, 0x00, 0x60, 0xd3, 0x18,               // the i8039's command
        0xd3, 0x14,                                 // and its interrupt
        0xf1, 0xfb, 0xed, 0x4d,                     // POP AF; EI; RETI
    };
    std::memcpy(snd + 0x38, isr, sizeof isr);
    const uint8_t wait[] = {
        0xfb,                                       // 00FD EI
        0x3e, 0x0e, 0xd7, 0xe6, 0x04, 0x20, 0xf9,   // 00FE wait while bit 2 set
        0x3e, 0x0e, 0xd7, 0xe6, 0x04, 0x28, 0xf9,   // 0105 wait while clear
        0x21, 0x02, 0x60, 0x34,                     // 010C INC (6002)
        0xc3, 0xfe, 0x00,                           // JP 00FE
    };
    std::memcpy(snd + 0xfd, wait, sizeof wait);
}

// i8039: idle at 0019 (NOP / JMP); its interrupt reads the command, drops
// the line (P2), and plays it on the DAC: the command, a DJNZ delay, 80.
void mcu_program(uint8_t* mcu)
{
    std::memset(mcu, 0, 0x1000);
    const uint8_t code[] = {
        0x04, 0x09, 0x00, 0x04, 0x1c, 0x00, 0x00, 0x00, 0x00,   // 000 JMP 009; 003 JMP 01C
        0x55,                   // 009 STRT T (the timer runs, as the game's)
        0x05,                   // 00A EN I
        0x04, 0x19,             // 00B JMP 019
    };
    std::memcpy(mcu, code, sizeof code);
    const uint8_t idle[] = {0x00, 0x04, 0x19};           // 019 NOP; JMP 019
    std::memcpy(mcu + 0x19, idle, sizeof idle);
    const uint8_t irq[] = {
        0x80,                   // 01C MOVX A,@R0: the command
        0x3a,                   // 01D OUTL P2,A: the line drops
        0xbd, 0x40,             // 01E MOV R5,#40
        0x39,                   // 020 OUTL P1,A: the DAC
        0xed, 0x21,             // 021 DJNZ R5,021
        0x23, 0x80,             // 023 MOV A,#80
        0x39,                   // 025 OUTL P1,A
        0x93,                   // 026 RETR
    };
    std::memcpy(mcu + 0x1c, irq, sizeof irq);
}

void make_board(Board& b)
{
    main_program(b.cpu);
    sub_program(b.sub);
    sound_program(b.sound);
    mcu_program(b.mcu);
    std::memset(b.sprites, 0, sizeof b.sprites);
    std::memset(b.tiles, 0, sizeof b.tiles);
    std::memset(b.proms, 0, sizeof b.proms);
    // Sprite 1: pen 1 everywhere (plane 0: the high nibble's bits); sprite 2:
    // pen 2 (plane 1: the low nibble's bits).
    for (int r = 0; r < 8; ++r)
        for (int half : {0, 8, 32, 40}) {
            b.sprites[1 * 64 + half + r] = 0xf0;
            b.sprites[2 * 64 + half + r] = 0x0f;
        }
    // Tile 1: pen 3 everywhere.
    for (int r = 0; r < 16; ++r) b.tiles[16 + r] = 0xff;
    // Palette: 1 red, 2 green, 3 blue; sprite colour 1: pen n -> entry n;
    // tile colour 3: pen 3 -> entry 0x13 (white).
    b.proms[1] = 0x07;
    b.proms[2] = 0x38;
    b.proms[3] = 0xc0;
    b.proms[0x13] = 0xff;
    for (int p = 0; p < 16; ++p) b.proms[0x20 + 16 + p] = uint8_t(p);
    b.proms[0x120 + 3 * 4 + 3] = 0x03;
}

uint16_t g_line[gyruss::kScreenW], g_line2[gyruss::kScreenW];

void test_board()
{
    static Board b;
    make_board(b);
    const gyruss::Roms roms{b.cpu, b.sub, b.sound, b.mcu, b.sprites, b.tiles, b.proms};
    static gyruss::Machine m, plain;
    m.init(roms, 24000);
    plain.init(roms, 24000);
    plain.set_idle_skip(false);

    int16_t audio[1024], audio2[1024];
    bool same = true;
    int loud = 0;
    for (int f = 0; f < 150; ++f) {
        const int n = m.run_frame(audio, 1024);
        const int n2 = plain.run_frame(audio2, 1024);
        same = same && n == n2 && std::memcmp(audio, audio2, size_t(n) * 2) == 0;
        for (int y = 0; y < gyruss::kScreenH && same; ++y) {
            m.render_line(y, g_line);
            plain.render_line(y, g_line2);
            same = std::memcmp(g_line, g_line2, sizeof g_line) == 0;
        }
        same = same && m.cpu().pc.w == plain.cpu().pc.w && m.cpu().af.w == plain.cpu().af.w &&
               m.cpu().hl.w == plain.cpu().hl.w && m.cpu().r_value() == plain.cpu().r_value() &&
               m.sub_cpu().pc == plain.sub_cpu().pc && m.sub_cpu().d == plain.sub_cpu().d &&
               m.sound_cpu().pc.w == plain.sound_cpu().pc.w && m.sound_cpu().af.w == plain.sound_cpu().af.w &&
               m.sound_cpu().r_value() == plain.sound_cpu().r_value() && m.i8039().pc == plain.i8039().pc &&
               m.i8039().timer == plain.i8039().timer && m.i8039().prescaler == plain.i8039().prescaler &&
               m.i8039().reg(5) == plain.i8039().reg(5);
        for (int i = 0; i < n; ++i) loud = std::max(loud, std::abs(int(audio[i])));
    }
    CHECK(same);
    for (int k = 0; k < 4; ++k) CHECK(m.skipped_cycles(k) > 0);
    CHECK(m.skipped_cycles(0) > 150ull * 40000);       // the main CPU idles nearly all the time
    CHECK(m.skipped_cycles(1) > 150ull * 5000);        // (its IRQ polls the beam to line 128)
    CHECK(m.skipped_cycles(2) > 150ull * 50000);
    CHECK(m.skipped_cycles(3) > 150ull * 8000);
    for (int k = 0; k < 4; ++k) CHECK_EQ(plain.skipped_cycles(k), 0ull);

    z80::Cpu& c = m.cpu();
    z80::Cpu& s = m.sound_cpu();
    m6809::Cpu& sub = m.sub_cpu();
    CHECK_EQ(c.rd(0x9000), 150);                        // one NMI a frame
    CHECK_EQ(sub.rd(0x4000), 150);                      // one sub IRQ a frame
    CHECK(sub.rd(0x4001) >= 0x80 && sub.rd(0x4001) <= 0x81);   // the beam's line it saw
    // The sound command, its IRQ (taken once), the AYs, the filters.
    CHECK_EQ(s.rd(0x6000), 0x5a);
    CHECK_EQ(s.rd(0x6001), 1);
    CHECK_EQ(m.ay(0).reg(7), 0xbe);
    CHECK_EQ(m.ay(0).reg(0), 0x40);
    CHECK_EQ(m.ay(0).port_out(1), 0x09);
    CHECK_EQ(m.ay(4).reg(9), 0x0c);
    CHECK(loud > 2000);
    // The timer waits: a count each time bit 2 rises - twice a turn of the
    // decade counter (10240 sound cycles); 150 frames = 150 x 59062.5
    // cycles: 1730 (a byte: 1730 - 1536).
    const int rises = s.rd(0x6002) + 1536;
    CHECK(rises >= 2 * 150 * 59062 / 10240 - 2 && rises <= 2 * 150 * 59062 / 10240 + 1);
    // The i8039 took the command, dropped its line, played 5A then 80.
    CHECK_EQ(m.dac(), 0x80);
    CHECK(!m.i8039().irq_line());
    CHECK(m.i8039().pc == 0x19 || m.i8039().pc == 0x1a);

    // The picture: display line y = pixel 64-71 along the scanline,
    // display x = 239 - line. Sprite code 1 (red) on lines 120-127, code 2
    // (green) from the line the sub CPU rewrote it (128) to 135; the tile
    // at row 2 column 2 (white, in front) on pixels 16-23, lines 16-23.
    m.render_line(66, g_line);
    const uint16_t red = m.pen(1), green = m.pen(2), black = m.pen(0), white = m.pen(0x13);
    CHECK(red != green && red != black && white != black);
    for (int line = 120; line < 128; ++line) CHECK(g_line[239 - line] == red);
    for (int line = 130; line < 136; ++line) CHECK(g_line[239 - line] == green);
    CHECK(g_line[239 - 119] == black);
    CHECK(g_line[239 - 136] == black);
    m.render_line(18, g_line);
    for (int line = 16; line < 24; ++line) CHECK(g_line[239 - line] == white);
    CHECK(g_line[239 - 24] == black);
    CHECK(m.video().count >= 2);                        // slot 0 as two pieces of the frame

    // Inputs (active low) and the DIP switches, read by the main CPU's NMI.
    CHECK_EQ(c.rd(0x9001), 0xff);
    CHECK_EQ(c.rd(0x9002), 0xff);
    CHECK_EQ(c.rd(0x9003), 0x3b);                       // 3 lives, upright, 30k, difficulty 5, demo sounds on
    CHECK_EQ(c.rd(0x9004), 0xfe);                       // demo music on
    CHECK_EQ(c.rd(0x9005), 0xff);                       // 1 coin 1 credit
    m.set_inputs(gyruss::kCoin | gyruss::kStart1 | gyruss::kLeft | gyruss::kUp | gyruss::kFire);
    m.set_dips(gyruss::kDipLives5 | gyruss::kDipDiffAverage);
    m.set_demo_sounds(false);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x9001), 0xff & ~0x09);
    CHECK_EQ(c.rd(0x9002), 0xff & ~0x15);
    CHECK_EQ(c.rd(0x9003), 0x80 | 0x31);
    CHECK_EQ(c.rd(0x9004), 0xff);
    m.set_inputs(gyruss::kCoin2 | gyruss::kService | gyruss::kStart2 | gyruss::kRight | gyruss::kDown);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x9001), 0xff & ~0x16);
    CHECK_EQ(c.rd(0x9002), 0xff & ~0x0a);
}

} // namespace

int main()
{
    test_konami1();
    test_ay_chunks();
    test_board();
    std::printf("Gyruss tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
