// Host tests (no ROMs) for Donkey Kong Jr. on the Donkey Kong board: where
// the three program chips' 2 KB pieces land in the map, the two tile banks
// (latch 4H bit 0), IN2 bit 6 (not connected), the 8035's interface (the
// tune latch read whole and not inverted by MOVX, the P2 inputs from
// latches 4H and 6H, the 4 KB program), the DAC's full level / fade, and
// the discrete sounds (walk / jump / climb on a latch bit's 1 -> 0 write,
// fall while 5H bit 1 is set) heard and silent again.
//
//   dkongjr_tests
#include "../../emu/machines/dkongjr.h"
#include "../../emu/sound/dkongjr_sound.h"

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

double rms(const std::vector<int16_t>& s)
{
    double e = 0;
    for (int16_t x : s) e += double(x) * x;
    return s.empty() ? 0 : std::sqrt(e / double(s.size()));
}

uint8_t cpu_rom[0x6000], snd[0x1000], tiles[0x2000], sprites[0x2000], proms[0x300];

dkong::Roms roms() { return {cpu_rom, snd, tiles, sprites, proms}; }

void build_roms()
{
    std::memset(cpu_rom, 0, sizeof cpu_rom);
    std::memset(snd, 0, sizeof snd);
    std::memset(tiles, 0, sizeof tiles);
    std::memset(sprites, 0, sizeof sprites);
    std::memset(proms, 0xff, 0x200);              // all PROM bits set = black (stored inverted)
    std::memset(proms + 0x200, 1, 0x100);         // every tile column in colour 1
    proms[4 + 1] = 0x0f; proms[0x100 + 4 + 1] = 0x01;     // colour 1, value 1: red
    proms[4 + 2] = 0xf3; proms[0x100 + 4 + 2] = 0x0e;     // colour 1, value 2: green
    // Tile 2, hardware pixel (0, 0): bank 0 = value 1 (plane 0, 3N), bank 1
    // = value 2 (plane 1, 3P's second half).
    tiles[2 * 8] = 0x80;
    tiles[0x1800 + 2 * 8] = 0x80;

    // Each chip piece (8 KB chips at 0000 / 2000 / 4000 in the region, 2 KB
    // pieces) is marked with its region offset / 0x800 at piece + 0x700.
    for (int p = 0; p < 12; ++p) cpu_rom[p * 0x800 + 0x700] = uint8_t(0xa0 + p);

    const uint8_t prog[] = {
        0xf3,                   // 0000 DI
        0x31, 0x00, 0x6c,       //      LD SP,6C00
        0x3e, 0x01,             //      LD A,1
        0x32, 0x84, 0x7d,       //      LD (7D84),A   - NMI on
        0x3a, 0x00, 0x7d,       // 0009 LD A,(7D00)   - IN2 (watchdog)
        0x32, 0x10, 0x60,       //      LD (6010),A
        0x18, 0xf8,             //      JR 0009
    };
    std::memcpy(cpu_rom, prog, sizeof prog);
    const uint8_t nmi[] = {0xf5, 0x21, 0x00, 0x60, 0x34, 0xf1, 0xed, 0x45};   // INC (6000); RETN
    std::memcpy(cpu_rom + 0x66, nmi, sizeof nmi);

    // 8035: P2 = F0 (DAC full, bits 4-6 high so they read as inputs);
    // loop: MOVX -> P1 (the DAC), P2 -> R2.
    const uint8_t sprog[] = {
        0x23, 0xf0, 0x3a,       // 000 MOV A,#F0; OUTL P2,A
        0xb8, 0x00,             // 003 MOV R0,#0
        0x80, 0x39,             // 005 MOVX A,@R0; OUTL P1,A
        0x0a, 0xaa,             // 007 IN A,P2; MOV R2,A
        0x04, 0x05,             // 009 JMP 005
    };
    std::memcpy(snd, sprog, sizeof sprog);
    // Past 2 KB: the 4 KB program is all there (no mirror at 0800).
    snd[0x800] = 0x5a;
}

void test_map()
{
    static dkongjr::Machine m;
    m.init(roms(), 24000);
    auto& c = m.cpu();
    // From the board's wiring (MAME's ROM_LOAD / ROM_CONTINUE list): the
    // map's 2 KB pieces 0000 ... 5800 come from (chip, piece) =
    // 5B: 0000 0800 | 3000 3800; 5C: 2000 4800 1000 5800; 5E: 4000 2800 5000 1800.
    const int want[12] = {
        0, 1,           // 0000, 0800: 5B pieces 0, 1
        4 + 2,          // 1000: 5C piece 2
        8 + 3,          // 1800: 5E piece 3
        4 + 0,          // 2000: 5C piece 0
        8 + 1,          // 2800: 5E piece 1
        2, 3,           // 3000, 3800: 5B pieces 2, 3
        8 + 0,          // 4000: 5E piece 0
        4 + 1,          // 4800: 5C piece 1
        8 + 2,          // 5000: 5E piece 2
        4 + 3,          // 5800: 5C piece 3
    };
    for (int a = 0; a < 12; ++a) CHECK_EQ(c.rd(uint16_t(a * 0x800 + 0x700)), 0xa0 + want[a]);
    CHECK_EQ(m.sound_cpu().rom_mask, 0xfff);
    CHECK_EQ(m.sound_cpu().rom[0x800], 0x5a);
}

void test_board()
{
    static dkongjr::Machine m;
    m.init(roms(), 24000);
    auto& c = m.cpu();
    int16_t audio[1024];
    for (int f = 0; f < 10; ++f) m.run_frame(audio, 1024);
    CHECK_EQ(c.rd(0x6000), 10);                   // NMI each frame
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(c.rd(0x6010) & 0x40, 0x40);          // IN2 bit 6 not connected: 1
    CHECK_EQ(c.rd(0x7d80), dkongjr::kDipDefault);

    // Tile banks: tile 2 at row 2, column 31 (display line 7, x 0).
    c.wr(0x7400 + 2 * 32 + 31, 2);
    uint16_t line[224];
    m.run_frame(nullptr, 0);
    m.render_line(7, line);
    CHECK_EQ(line[0], 0xf800);                    // bank 0: red
    c.wr(0x7c80, 1);
    m.run_frame(nullptr, 0);
    m.render_line(7, line);
    CHECK_EQ(line[0], 0x07e0);                    // bank 1: green
    CHECK_EQ(m.video().gfx_bank, 1);
    c.wr(0x7c80, 0);

    // The 8035 reads the tune latch's low 5 bits, not inverted.
    c.wr(0x7c00, 0xf5);
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.sound_cpu().p1, 0x15);
    // P2 inputs: bit 6 = !4H bit 1, bit 5 = !6H bit 3, bit 4 = !6H bit 6;
    // the rest as written (80; the port reads its latch AND the inputs).
    CHECK_EQ(m.sound_cpu().ram[2], 0x80 | 0x70);
    c.wr(0x7c81, 1);
    c.wr(0x7d03, 1);
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.sound_cpu().ram[2], 0x80 | 0x10);
    c.wr(0x7d06, 1);
    c.wr(0x7c81, 0);
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.sound_cpu().ram[2], 0x80 | 0x40);
    c.wr(0x7d03, 0);
    c.wr(0x7d06, 0);
    c.wr(0x7c00, 0x00);

    auto level = [&](int frames) {
        std::vector<int16_t> all;
        for (int f = 0; f < frames; ++f) {
            const int n = m.run_frame(audio, 1024);
            all.insert(all.end(), audio, audio + n);
        }
        return rms(all);
    };
    level(120);
    const double quiet = level(30);
    CHECK(quiet < 5);

    // The fall sounds while 5H bit 1 is set.
    c.wr(0x7d81, 1);
    const double fall = level(20);
    c.wr(0x7d81, 0);
    level(30);
    const double fall_after = level(30);
    std::printf("dkongjr: fall %.0f, after %.1f\n", fall, fall_after);
    CHECK(fall > 400);
    CHECK(fall_after < 5);

    // The discrete sounds fire when the bit goes back to 0, then die away
    // (jump and climb leave a quiet tail: the envelope's capacitor drains
    // through 100k with the node held near -0.6 V, ~1 s).
    for (int b = 0; b < 3; ++b) {
        c.wr(uint16_t(0x7d00 + b), 1);
        const double held = level(2);             // set, not yet fired
        c.wr(uint16_t(0x7d00 + b), 0);
        const double fired = level(b == 0 ? 3 : 12);
        level(180);
        const double after = level(30);
        std::printf("dkongjr: 6H bit %d held %.0f, fired %.0f, after %.1f\n", b, held, fired, after);
        CHECK(held < 100);                        // at most the last one's tail
        CHECK(fired > 400);
        CHECK(after < fired / 20);
    }

    // The DAC: a level from the tune latch (MOVX -> P1) is heard as a step;
    // with P2 bit 7 clear it fades out.
    c.wr(0x7c00, 0x1f);
    std::vector<int16_t> step;
    for (int f = 0; f < 3; ++f) {
        const int n = m.run_frame(audio, 1024);
        step.insert(step.end(), audio, audio + n);
    }
    int peak = 0;
    for (int16_t s : step) peak = std::max(peak, std::abs(int(s)));
    std::printf("dkongjr: DAC step 0 -> 1F peak %d\n", peak);
    CHECK(peak > 300);
}

void test_fade()
{
    // The model alone: P2 bit 7 clear fades the DAC (10k x 10 uF).
    dkongjr_sound::Board b;
    b.init(24000);
    auto burst = [&](bool full) {
        b.reset();
        b.set_dac_full(full);
        double e = 0;
        for (int i = 0; i < 24000; ++i) {
            const int16_t s = b.sample(((i / 24) & 1) ? 128.0f : 0.0f);
            if (i >= 12000) e += double(s) * s;
        }
        return std::sqrt(e / 12000);
    };
    const double full = burst(true), faded = burst(false);
    std::printf("dkongjr: DAC square rms full %.0f, fading %.1f\n", full, faded);
    CHECK(full > 1000);
    CHECK(faded < 50);
}

} // namespace

int main()
{
    build_roms();
    test_map();
    test_board();
    test_fade();
    std::printf("dkongjr_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
