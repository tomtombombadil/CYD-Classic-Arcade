// Unit tests for the Q*bert board (emu/machines/qbert.*), its sound models
// (emu/sound/qbert_sound.*) and the diagonal stick mapping (emu/games.*),
// with hand-made 8088 / 6502 programs and graphics - no ROMs.
//
//   qbert_tests
#include "../../emu/games.h"
#include "../../emu/machines/qbert.h"
#include "../../emu/sound/qbert_sound.h"

#include <cstdio>
#include <cstdlib>
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
        else { ++g_fail; std::printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, va_, vb_); } \
    } while (0)

// --- the diagonal stick --------------------------------------------------------

void test_pad()
{
    const games::Info* q = games::find("qbert");
    CHECK(q != nullptr);
    if (!q) return;
    CHECK(q->diagonal && !q->four_way);
    using namespace games;
    CHECK_EQ(map_pad(*q, kPadUp), qbert::kUpRight);
    CHECK_EQ(map_pad(*q, kPadRight), qbert::kDownRight);
    CHECK_EQ(map_pad(*q, kPadDown), qbert::kDownLeft);
    CHECK_EQ(map_pad(*q, kPadLeft), qbert::kUpLeft);
    // Held diagonals count as that diagonal - one switch only.
    CHECK_EQ(map_pad(*q, kPadUp | kPadRight), qbert::kUpRight);
    CHECK_EQ(map_pad(*q, kPadRight | kPadDown), qbert::kDownRight);
    CHECK_EQ(map_pad(*q, kPadDown | kPadLeft), qbert::kDownLeft);
    CHECK_EQ(map_pad(*q, kPadLeft | kPadUp), qbert::kUpLeft);
    CHECK_EQ(map_pad(*q, kPadUp | kPadDown), 0);
    CHECK_EQ(map_pad(*q, kPadLeft | kPadRight), 0);
    CHECK_EQ(map_pad(*q, kPadSelect | kPadDown | kPadLeft), qbert::kCoin | qbert::kDownLeft);
    CHECK_EQ(map_pad(*q, kPadStart), qbert::kStart1);
    CHECK_EQ(map_pad(*q, kPadL), qbert::kStart2);
    CHECK_EQ(diagonal_pad(kPadA | kPadUp | kPadRight), kPadA | kPadUp);
    // Other games keep both directions.
    const games::Info* p = games::find("pacman");
    CHECK(p && !p->diagonal);
    if (p) CHECK_EQ(map_pad(*p, kPadUp | kPadRight) & 0x0f, 0x0f & (map_pad(*p, kPadUp) | map_pad(*p, kPadRight)));
}

// --- the board with hand-made programs ------------------------------------------

struct Board {
    std::vector<uint8_t> main = std::vector<uint8_t>(0x6000, 0x90);   // A000-FFFF, NOPs
    std::vector<uint8_t> sound = std::vector<uint8_t>(0x1000, 0xea);  // 7000-7FFF, NOPs
    std::vector<uint8_t> tiles = std::vector<uint8_t>(0x2000, 0);
    std::vector<uint8_t> sprites = std::vector<uint8_t>(0x8000, 0);
    std::unique_ptr<qbert::Machine> m{new qbert::Machine};

    void at_main(uint16_t addr, std::initializer_list<uint8_t> b)
    {
        for (uint8_t v : b) main[addr++ - 0xa000] = v;
    }
    void at_sound(uint16_t addr, std::initializer_list<uint8_t> b)
    {
        for (uint8_t v : b) sound[(addr++) & 0xfff] = v;
    }
    void start() { m->init({main.data(), sound.data(), tiles.data(), sprites.data()}, 24000, false); }
};

// The main program: palette, a tile, a sprite, a sound command, two knocks,
// then a wait for the NMI's frame count (as Q*bert's own at B36D) and the
// watchdog. The NMI handler counts frames at 0080.
void build_main(Board& b, bool feed_watchdog)
{
    b.at_main(0xfff0, {0xea, 0x00, 0xa0, 0x00, 0x00});                   // JMP 0000:A000
    b.at_main(0xa000, {
        0xb8, 0x00, 0x00,                   // MOV AX,0
        0x8e, 0xd8,                         // MOV DS,AX
        0x8e, 0xd0,                         // MOV SS,AX
        0xbc, 0x00, 0x20,                   // MOV SP,2000
        0xc7, 0x06, 0x80, 0x00, 0x00, 0x00, // MOV [0080],0 (battery RAM starts as FF)
        0xc7, 0x06, 0x08, 0x00, 0x00, 0xa1, // MOV [0008],A100 (NMI vector)
        0xc7, 0x06, 0x0a, 0x00, 0x00, 0x00, // MOV [000A],0
        0xc6, 0x06, 0x02, 0x50, 0x00,       // colour 1: G B = 0
        0xc6, 0x06, 0x03, 0x50, 0x0f,       //           R = 15
        0xc6, 0x06, 0x04, 0x50, 0x0f,       // colour 2: B = 15
        0xc6, 0x06, 0x05, 0x50, 0x00,       //           R = 0
        0xc6, 0x06, 0x65, 0x38, 0x01,       // tile row 3, column 5 = tile 1
        0xc6, 0x06, 0x00, 0x30, 0x3f,       // sprite 0: y byte 63 (line 50)
        0xc6, 0x06, 0x01, 0x30, 0x68,       //           x byte 104 (column 100)
        0xc6, 0x06, 0x02, 0x30, 0xfd,       //           code ~2
        0xc6, 0x06, 0x02, 0x58, 0xff,       // sound: no command
        0xc6, 0x06, 0x02, 0x58, 0x0a,       // sound: command 0A
        0xc6, 0x06, 0x03, 0x58, 0x20,       // knocker on
        0xc6, 0x06, 0x03, 0x58, 0x00,       // off
        0xc6, 0x06, 0x03, 0x58, 0x20,       // on again
        0xe9, 0x0f, 0x13,                   // A058: JMP B36A
    });
    b.at_main(0xb36a, {
        0xa1, 0x80, 0x00,                   // B36A MOV AX,[0080]
        0x3b, 0x06, 0x80, 0x00,             // B36D CMP AX,[0080]
        0x74, 0xfa,                         // B371 JZ B36D
    });
    if (feed_watchdog) b.at_main(0xb373, {0xa2, 0x00, 0x58});          // MOV [5800],AL
    b.at_main(0xb376, {0xe9, 0xf1, 0xff});                               // JMP B36A
    b.at_main(0xa100, {0xff, 0x06, 0x80, 0x00, 0xcf});                   // INC WORD [0080]; IRET
}

// The sound program: the RIOT's PA7 edge interrupts; the IRQ handler puts
// the command (port A, low 6 bits) on the DAC and in RAM at 0010 and, through
// the stack page's mirror, 0112. The speech chip gets PA1 phonemes; its NMI
// counts them at 0011 and sends the next.
void build_sound(Board& b)
{
    b.at_sound(0x7000, {
        0xa2, 0x7b, 0x9a,                   // LDX #7B, TXS
        0xa9, 0x00, 0x8d, 0x01, 0x02,       // DDRA = 0
        0x8d, 0x07, 0x02,                   // PA7 rising edge, interrupt on
        0xad, 0x05, 0x02,                   // clear the flags
        0xa9, 0xa0, 0x8d, 0x00, 0x30,       // speech clock A0
        0xa9, 0xc1, 0x8d, 0x00, 0x20,       // speech: PA1 (inverted)
        0x58,                               // CLI
        0x4c, 0x19, 0x70,                   // 7019: JMP 7019
    });
    b.at_sound(0x7020, {
        0x48, 0xad, 0x05, 0x02,             // PHA; LDA flags (clears the edge)
        0xad, 0x00, 0x02, 0x29, 0x3f,       // LDA port A; AND #3F
        0x8d, 0x00, 0x10,                   // the DAC
        0x85, 0x10, 0x8d, 0x12, 0x01,       // STA 10; STA 0112
        0x68, 0x40,                         // PLA; RTI
    });
    b.at_sound(0x7100, {0xe6, 0x11, 0x48, 0xa9, 0xc1, 0x8d, 0x00, 0x20, 0x68, 0x40});
    b.at_sound(0x7ffa, {0x00, 0x71, 0x00, 0x70, 0x20, 0x70});
}

void build_gfx(Board& b)
{
    std::memset(b.tiles.data() + 32, 0x11, 32);         // tile 1: all colour 1
    std::memset(b.sprites.data() + 0x4000 + 2 * 32, 0xff, 32);   // sprite 2: colour 2 (plane bit 1)
}

void test_board()
{
    Board b;
    build_main(b, true);
    build_sound(b);
    build_gfx(b);
    b.start();
    int16_t audio[1024];
    int n = 0;
    long energy = 0;
    for (int f = 0; f < 61; ++f) {
        n = b.m->run_frame(audio, 1024);
        for (int i = 0; i < n; ++i) energy += std::abs(audio[i]);
    }
    CHECK(n == 390 || n == 391);                    // 24000 / 61.42
    CHECK_EQ(b.m->watchdog_resets(), 0);
    const uint8_t* ram = b.m->ram();
    CHECK_EQ(ram[0x80] | ram[0x81] << 8, 61);       // one NMI a frame
    CHECK(b.m->sound_commands() == 2);
    CHECK_EQ(b.m->knocks(), 2);
    CHECK(energy > 0);

    // The command reached the 6502: port A = ~0A & 3F = 35, on the DAC and
    // in RAM - 0112 is 0012 (the RIOT's 128 bytes seen in page 1 too).
    const uint8_t* sram = b.m->sound_cpu().read_page[0];
    CHECK_EQ(sram[0x10], 0x35);
    CHECK_EQ(sram[0x12], 0x35);
    CHECK(b.m->dac_writes() >= 1);
    // PA1 lasts 185 ms at the nominal clock; at A0 the clock is ~1.48x,
    // so ~8 phonemes a second, each ending in an NMI.
    CHECK(sram[0x11] >= 7 && sram[0x11] <= 9);
    CHECK_EQ(b.m->phonemes(), sram[0x11] + 1u);

    // The picture: display line y = hardware column 255 - y, display x =
    // hardware line. Tile (row 3, column 5) covers columns 40-47, lines
    // 24-31; the sprite columns 100-115, lines 50-65.
    uint16_t line[qbert::kScreenW];
    b.m->render_line(255 - 44, line);
    CHECK_EQ(line[28], 0xf800);                     // red
    CHECK_EQ(line[20], 0x0000);                     // colour 0: black
    CHECK_EQ(line[32], 0x0000);
    b.m->render_line(255 - 107, line);
    CHECK_EQ(line[50], 0x001f);                     // blue sprite
    CHECK_EQ(line[65], 0x001f);
    CHECK_EQ(line[49], 0x0000);
    CHECK_EQ(line[66], 0x0000);
    b.m->render_line(255 - 99, line);
    CHECK_EQ(line[55], 0x0000);
    b.m->render_line(255 - 116, line);
    CHECK_EQ(line[55], 0x0000);
}

void test_watchdog()
{
    Board b;
    build_main(b, false);
    build_sound(b);
    b.start();
    int16_t audio[1024];
    for (int f = 0; f < 40; ++f) b.m->run_frame(audio, 1024);
    CHECK_EQ(b.m->watchdog_resets(), 2);            // every 16 frames without a write to 5800
}

// Idle skipping changes nothing: the same RAM, picture and sound with it
// on and off (the main CPU's wait at B36D).
void test_idle_skip()
{
    Board a, b;
    for (Board* x : {&a, &b}) {
        build_main(*x, true);
        build_sound(*x);
        build_gfx(*x);
        x->start();
    }
    b.m->set_idle_skip(false);
    int16_t aa[1024], ab[1024];
    int diff = 0;
    long ran_a = 0, ran_b = 0;
    for (int f = 0; f < 120; ++f) {
        const int na = a.m->run_frame(aa, 1024), nb = b.m->run_frame(ab, 1024);
        ran_a += a.m->main_cycles_run();
        ran_b += b.m->main_cycles_run();
        if (na != nb || std::memcmp(aa, ab, sizeof(int16_t) * size_t(na)) ||
            std::memcmp(a.m->ram(), b.m->ram(), 0x3000) || a.m->cpu().ip != b.m->cpu().ip)
            ++diff;
    }
    CHECK_EQ(diff, 0);
    CHECK(ran_a * 20 < ran_b);                      // and it skips nearly everything here
}

// --- the sound models ----------------------------------------------------------

void test_speech()
{
    qbert_sound::Speech s;
    s.init(24000, qbert::kSoundClock);
    CHECK(s.ready(0) && s.request_at() < 0);
    s.set_clock(0xa0);
    s.write(0x24, 0, 1000);                         // AH: 250 ms nominal
    CHECK(!s.ready(1001));
    const double ms = 250 / (0.6 + 0xa0 * 1.4 / 255);
    const int64_t want = 1000 + int64_t(ms * qbert::kSoundClock / 1000);
    CHECK(s.request_at() >= want - 2 && s.request_at() <= want + 2);
    CHECK(s.ready(s.request_at()));
    int peak = 0;
    for (int i = 0; i < 4800; ++i) {
        const int v = std::abs(s.sample());
        if (v > peak) peak = v;
    }
    CHECK(peak > 4000 && peak <= 14000);            // a vowel, not clipped
    s.acknowledge();
    CHECK(s.request_at() < 0);
    s.write(0x3f, 0, 0);                            // STOP: silence (after the fade)
    for (int i = 0; i < 2400; ++i) s.sample();
    peak = 0;
    for (int i = 0; i < 2400; ++i) {
        const int v = std::abs(s.sample());
        if (v > peak) peak = v;
    }
    CHECK(peak < 50);
    // Higher clock, shorter phonemes.
    s.set_clock(0xff);
    s.write(0x24, 0, 0);
    CHECK(s.request_at() < want - 1000);
    CHECK_EQ(s.phonemes(), 3);
}

void test_knocker()
{
    qbert_sound::Knocker k;
    k.init(24000);
    CHECK(!k.active() && k.sample() == 0);
    k.strike();
    int peak = 0, tail = 0, after = 0;
    for (int i = 0; i < 24000; ++i) {
        const int v = std::abs(k.sample());
        if (v > peak) peak = v;
        if (i > 9600 && i < 10800) tail = v > tail ? v : tail;
        if (i >= 10800) after = v > after ? v : after;
    }
    CHECK(peak > 8000 && peak <= 20000);
    CHECK(tail < 400);                              // faded by 0.4 s
    CHECK(after == 0 && !k.active());               // over at 0.45 s
}

} // namespace

int main()
{
    test_pad();
    test_board();
    test_watchdog();
    test_idle_skip();
    test_speech();
    test_knocker();
    std::printf("qbert tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
