// Unit tests for Namco's Mappy board (Dig Dug II), with hand-made data - no
// ROMs: the 56XX / 58XX I/O chip models (ports, coinage, coins and starts,
// the DIP switch reads, the start-up checks against the values the games
// expect), the board's wiring on tiny 6809 programs for both CPUs (latch,
// IRQs, the sound CPU's reset, scroll, I/O chips, the 15XX's registers in
// the shared RAM, sprite registers), the picture (tiles, fixed columns,
// scroll, sprites with sizes / flips, front characters) on synthetic
// graphics, and that idle skipping leaves everything as it is without it.
//
//   mappy_tests
#include "../../emu/machines/mappy.h"
#include "../../emu/machines/namco_io.h"

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

using namco_io::Chip5x;

// --- I/O chips ---------------------------------------------------------------------

void test_io_ports()
{
    Chip5x c58, c56;
    c58.init(Chip5x::Type::k58xx);
    c56.init(Chip5x::Type::k56xx);
    for (Chip5x* c : {&c58, &c56}) {
        c->set_ports(0, 0x0e, 0x0d, 0x0b, 0x07);     // one pin low on each port
        c->set_ports(1, 0x0c, 0x09, 0x03, 0x06);
        c->write(8, 1);
        c->run();
        CHECK_EQ(c->read(c == &c58 ? 4 : 0), 0xf0);  // held in reset: nothing done
        c->set_reset(false);
        c->run();
    }
    // Command 1: the ports, inverted, into 4-7 (58XX) / 0-3 (56XX).
    CHECK_EQ(c58.read(4), 0xf1);
    CHECK_EQ(c58.read(5), 0xf2);
    CHECK_EQ(c58.read(6), 0xf4);
    CHECK_EQ(c58.read(7), 0xf8);
    CHECK_EQ(c58.read(0), 0xf0);
    CHECK_EQ(c56.read(0), 0xf1);
    CHECK_EQ(c56.read(3), 0xf8);
    CHECK_EQ(c56.read(4), 0xf0);
    // Pin-13 reads: 58XX command 4, 56XX command 9.
    c58.write(8, 4);
    c58.run();
    c56.write(8, 9);
    c56.run();
    for (Chip5x* c : {&c58, &c56}) {
        CHECK_EQ(c->read(0), 0xf1);
        CHECK_EQ(c->read(1), 0xf3);
        CHECK_EQ(c->read(2), 0xf2);
        CHECK_EQ(c->read(3), 0xf6);
        CHECK_EQ(c->read(6), 0xf8);
        CHECK_EQ(c->read(7), 0xf9);
    }
    // The RAM is 4 bits wide.
    c58.write(12, 0xa7);
    CHECK_EQ(c58.read(12), 0xf7);
}

void test_io_coins()
{
    Chip5x c;
    c.init(Chip5x::Type::k58xx);
    c.set_reset(false);
    auto ports = [&](uint8_t coins, uint8_t buttons, uint8_t stick) {
        c.set_ports(0, coins, stick, 0x0f, buttons);
        c.set_ports(1, coins, stick, 0x0f, buttons);
    };
    // Coinage: 2 coins 1 credit on slot 1, 1 coin 3 credits on slot 2.
    c.write(8, 2);
    c.write(9, 2);
    c.write(10, 1);
    c.write(11, 1);
    c.write(12, 3);
    c.run();
    c.write(8, 3);
    c.write(9, 1);                                   // starts not taken yet
    ports(0x0f, 0x0f, 0x0f);
    c.run();
    CHECK_EQ(c.credits(), 0);
    ports(0x0e, 0x0f, 0x0f);                         // coin 1: one of two
    c.run();
    CHECK_EQ(c.credits(), 0);
    c.run();                                         // still held: no new coin
    ports(0x0f, 0x0f, 0x0f);
    c.run();
    ports(0x0e, 0x0f, 0x0f);
    c.run();
    CHECK_EQ(c.credits(), 1);
    CHECK_EQ(c.read(0) & 15, 1);                     // 58XX: credits added in 0
    ports(0x0d, 0x0f, 0x0f);                         // coin 2: 3 credits
    c.run();
    CHECK_EQ(c.credits(), 4);
    CHECK_EQ(c.read(2) & 15, 0);                     // 58XX: BCD credits in 2-3
    CHECK_EQ(c.read(3) & 15, 4);
    ports(0x07, 0x0f, 0x0f);                         // service credit
    c.run();
    CHECK_EQ(c.credits(), 5);
    // Start 1 while the game doesn't take starts: nothing.
    ports(0x0f, 0x0b, 0x0f);
    c.run();
    CHECK_EQ(c.credits(), 5);
    ports(0x0f, 0x0f, 0x0f);
    c.run();
    c.write(9, 0);
    ports(0x0f, 0x0b, 0x0f);                         // start 1: one credit
    c.run();
    CHECK_EQ(c.credits(), 4);
    CHECK_EQ(c.read(1) & 15, 1);                     // 58XX: credits taken in 1
    ports(0x0f, 0x07, 0x0f);                         // start 2: two
    c.run();
    CHECK_EQ(c.credits(), 2);
    // Buttons: bit 0 / 2 held + just pressed into 5, bits 1 / 3 into 7;
    // the stick (port B) into 4.
    ports(0x0f, 0x0e, 0x0e);
    c.run();
    CHECK_EQ(c.read(5) & 15, 0x3);                   // button 1 held (2) + new (1)
    CHECK_EQ(c.read(4) & 15, 1);
    c.run();
    CHECK_EQ(c.read(5) & 15, 0x2);                   // held, not new
    // A reset clears the credits and the coinage.
    c.set_reset(true);
    c.set_reset(false);
    CHECK_EQ(c.credits(), 0);
    // 56XX coin mode (4): credits in 0-1.
    Chip5x d;
    d.init(Chip5x::Type::k56xx);
    d.set_reset(false);
    d.write(8, 4);
    d.set_ports(0, 0x0e, 0x0f, 0x0f, 0x0f);
    d.run();
    CHECK_EQ(d.credits(), 1);
    CHECK_EQ(d.read(1) & 15, 1);
    CHECK_EQ(d.read(2) & 15, 1);
}

void test_io_checks()
{
    // 58XX command 5 with the arguments Mappy, Grobda and Phozon send at
    // power-on and the answers they check for.
    Chip5x c;
    c.init(Chip5x::Type::k58xx);
    c.set_reset(false);
    auto args = [&](std::initializer_list<int> v) {
        int i = 9;
        for (int n : v) c.write(uint8_t(i++), uint8_t(n));
        c.write(8, 5);
        c.run();
    };
    args({3, 6, 5, 0xf, 0xa, 0xc, 0xe});
    const int mappy[7] = {8, 4, 6, 0xe, 0xd, 9, 0xd};
    for (int i = 0; i < 7; ++i) CHECK_EQ(c.read(uint8_t(1 + i)) & 15, mappy[i]);
    args({2, 3, 4, 5, 6, 7, 8});
    CHECK_EQ(c.read(2) & 15, 0xf);
    CHECK_EQ(c.read(6) & 15, 0xc);
    args({0, 1, 2, 3, 4, 5, 6});
    const int phozon[8] = {0, 2, 3, 4, 5, 6, 0xc, 0xa};
    for (int i = 0; i < 8; ++i) CHECK_EQ(c.read(uint8_t(i)) & 15, phozon[i]);
    // 56XX command 8: the sum of 9-15 (Super Pac-Man: 7 x F = 0x69).
    Chip5x d;
    d.init(Chip5x::Type::k56xx);
    d.set_reset(false);
    for (int i = 9; i < 16; ++i) d.write(uint8_t(i), 0xf);
    d.write(8, 8);
    d.run();
    CHECK_EQ(d.read(0) & 15, 6);
    CHECK_EQ(d.read(1) & 15, 9);
}

// --- the board ---------------------------------------------------------------------

struct Roms {
    std::vector<uint8_t> main = std::vector<uint8_t>(0x8000, 0x12);    // NOP
    std::vector<uint8_t> sub = std::vector<uint8_t>(0x2000, 0x12);
    std::vector<uint8_t> tiles = std::vector<uint8_t>(0x1000, 0xff);   // inverted: all pixels 0
    std::vector<uint8_t> spr0 = std::vector<uint8_t>(0x4000, 0);
    std::vector<uint8_t> spr1 = std::vector<uint8_t>(0x4000, 0);
    std::vector<uint8_t> proms = std::vector<uint8_t>(0x220, 0);
    std::vector<uint8_t> sound = std::vector<uint8_t>(0x100, 0);
    const uint8_t* regions[mappy::kRegionCount];

    void put_main(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) main[addr++ - 0x8000] = b;
    }
    void put_sub(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) sub[addr++ - 0xe000] = b;
    }

    Roms()
    {
        // Main CPU: IRQ on, sound CPU running, sound on, I/O chips running,
        // scroll 0x45; then waits for its IRQ's flag in $1000 (counting the
        // passes through in $1001); the IRQ handler acknowledges (IRQ enable
        // off and on), sets the flag and counts in $1002.
        put_main(0x8000, {0x10, 0xce, 0x27, 0x00,       // LDS #$2700
                          0x7f, 0x10, 0x00,             // CLR $1000
                          0xb7, 0x50, 0x03,             // STA $5003 (main IRQ on)
                          0xb7, 0x50, 0x0b,             // STA $500B (sound CPU runs)
                          0xb7, 0x50, 0x07,             // STA $5007 (sound on)
                          0xb7, 0x50, 0x09,             // STA $5009 (I/O chips run)
                          0xb7, 0x3a, 0x28,             // STA $3A28 (scroll 0x45)
                          0x1c, 0xef,                   // ANDCC #$EF
                          0xb6, 0x10, 0x00,             // 8018: LDA $1000
                          0x27, 0xfb,                   // BEQ 8018
                          0x7f, 0x10, 0x00,             // CLR $1000
                          0x7c, 0x10, 0x01,             // INC $1001
                          0x20, 0xf3});                 // BRA 8018
        put_main(0x8030, {0xb7, 0x50, 0x02,             // STA $5002 (main IRQ off)
                          0xb7, 0x50, 0x03,             // STA $5003 (on)
                          0x7c, 0x10, 0x00,             // INC $1000
                          0x7c, 0x10, 0x02,             // INC $1002
                          0x3b});                       // RTI
        put_main(0xfff8, {0x80, 0x30});
        put_main(0xfffe, {0x80, 0x00});
        // Sound CPU: the same with a direct-page flag at $00A1.
        put_sub(0xe000, {0x10, 0xce, 0x03, 0xf0,        // LDS #$03F0
                         0x4f, 0x1f, 0x8b,              // CLRA ; TFR A,DP
                         0xb7, 0x20, 0x01,              // STA $2001 (sound CPU IRQ on)
                         0x1c, 0xef,                    // ANDCC #$EF
                         0x96, 0xa1,                    // E00C: LDA <$A1
                         0x27, 0xfc,                    // BEQ E00C
                         0x0f, 0xa1,                    // CLR <$A1
                         0x0c, 0xa2,                    // INC <$A2
                         0x20, 0xf6});                  // BRA E00C
        put_sub(0xe020, {0xb7, 0x20, 0x00,              // STA $2000 (off)
                         0xb7, 0x20, 0x01,              // STA $2001 (on)
                         0x0c, 0xa1,                    // INC <$A1
                         0x0c, 0xa3,                    // INC <$A3
                         0x3b});                        // RTI
        put_sub(0xfff8, {0xe0, 0x20});
        put_sub(0xfffe, {0xe0, 0x00});

        // Graphics: tile 1 all pixels 3, tile 2 only (x 0, y 0) = 2.
        for (int i = 16; i < 32; ++i) tiles[i] = 0x00;
        tiles[32 + 8] = uint8_t(~0x80);
        // Sprite 3 all pixels 14 (planes 0-2 set), sprite 4 only (0, 0) = 8.
        for (int i = 3 * 64; i < 4 * 64; ++i) {
            spr0[i] = 0xff;
            spr1[i] = 0xf0;
        }
        spr0[4 * 64] = 0x80;
        // 32 different colours; character pen = (colour * 4 + p) & 15 (15 =
        // see-through), sprite pen = p, but 0 -> 15 (transparent).
        for (int e = 0; e < 32; ++e) proms[e] = uint8_t(e * 37 + 1);
        for (int i = 0; i < 256; ++i) proms[0x20 + i] = uint8_t(i & 15);
        for (int i = 0; i < 256; ++i) proms[0x120 + i] = uint8_t((i & 15) ? (i & 15) : 15);
        for (int i = 0; i < 256; ++i) sound[i] = uint8_t((i & 31) < 16 ? 15 : 0);   // square waves

        regions[mappy::kRegionMain] = main.data();
        regions[mappy::kRegionSub] = sub.data();
        regions[mappy::kRegionTiles] = tiles.data();
        regions[mappy::kRegionSprites0] = spr0.data();
        regions[mappy::kRegionSprites1] = spr1.data();
        regions[mappy::kRegionProms] = proms.data();
        regions[mappy::kRegionSound] = sound.data();
    }
};

mappy::Board test_board()
{
    mappy::Board b = mappy::digdug2::kBoard;
    b.set = nullptr;
    return b;
}

void test_wiring()
{
    static Roms roms;
    static mappy::Machine m;
    m.init(test_board(), roms.regions, 24000, false);
    CHECK_EQ(m.latch(), 0);
    static int16_t audio[1024];
    for (int f = 0; f < 10; ++f) m.run_frame(audio, 1024);
    CHECK_EQ(m.scroll(), 0x45);
    CHECK_EQ(m.latch(), 0x3b);                       // IRQs on, sound on, I/O and sound CPU run
    CHECK_EQ(m.ram()[0x002], 10);                    // one main IRQ a frame
    CHECK_EQ(m.ram()[0x001], 10);
    CHECK_EQ(m.shared()[0xa3], 10);                  // and the sound CPU's
    CHECK_EQ(m.shared()[0xa2], 10);
    CHECK(m.main_cpu().pc == 0x8018 || m.main_cpu().pc == 0x801b);    // waiting

    // The I/O chips through the main CPU's bus: chip 1 (56XX, DSW) command
    // 9 (both pin-13 halves), chip 0 (58XX) command 1 with the stick.
    m.set_dips(0x5a);
    m.set_inputs(mappy::kLeft | mappy::kButton1);
    m.main_cpu().wr(0x4818, 9);
    m.main_cpu().wr(0x4808, 1);
    m.run_frame(audio, 1024);
    CHECK_EQ(m.main_cpu().rd(0x4812), 0xf0 | (~0x5a & 15));    // DSW1 bits 0-3, inverted
    CHECK_EQ(m.main_cpu().rd(0x4814), 0xf0 | (~0x5 & 15));     // bits 4-7
    CHECK_EQ(m.main_cpu().rd(0x4805), 0xf8);                    // stick: left
    CHECK_EQ(m.main_cpu().rd(0x4807), 0xf1);                    // button 1
    m.set_inputs(mappy::kButton2);
    m.run_frame(audio, 1024);
    CHECK_EQ(m.main_cpu().rd(0x4816), 0xf1);                    // DSW0 bit 0 = button 2
    m.set_inputs(0);

    // The 15XX's registers in the shared RAM (written by either CPU).
    uint8_t* r = m.shared() + 3 * 8;
    r[3] = 0x0a;
    r[4] = 0x34;
    r[5] = 0x12;
    r[6] = 0x51;
    m.run_frame(audio, 1024);
    CHECK_EQ(m.wsg().volume(3), 10);
    CHECK_EQ(m.wsg().frequency(3), 0x11234);
    int peak = 0;
    for (int f = 0; f < 3; ++f) {
        const int n = m.run_frame(audio, 1024);
        for (int i = 0; i < n; ++i) peak = audio[i] > peak ? audio[i] : peak;
    }
    CHECK(peak > 1000);
    m.sub_cpu().wr(0x2006, 0);                       // sound off (the latch from the sound CPU)
    m.run_frame(audio, 1024);
    peak = 0;
    const int n = m.run_frame(audio, 1024);
    for (int i = 0; i < n; ++i) peak = audio[i] > peak ? audio[i] : peak;
    CHECK_EQ(peak, 0);

    // Holding the sound CPU in reset stops it; letting it go restarts it.
    const uint8_t seen = m.shared()[0xa3];
    m.main_cpu().wr(0x500a, 0);
    for (int f = 0; f < 3; ++f) m.run_frame(audio, 1024);
    CHECK_EQ(m.shared()[0xa3], seen);
    m.main_cpu().wr(0x500b, 0);
    for (int f = 0; f < 3; ++f) m.run_frame(audio, 1024);
    CHECK(m.shared()[0xa3] > seen);

    // Sprite registers -> the frame's sprite list.
    uint8_t* s1 = m.ram() + 0x780;
    uint8_t* s2 = m.ram() + 0xf80;
    uint8_t* s3 = m.ram() + 0x1780;
    for (int o = 0; o < 0x80; o += 2) s3[o + 1] = 0x02;    // all off
    s1[10] = 0x27;                                    // sprite 5: code 0x27, colour 3
    s1[11] = 0x03;
    s2[10] = 0x80;                                    // Y
    s2[11] = 0x50;                                    // X
    s3[10] = 0x0f;                                    // double wide and tall, both flips
    s3[11] = 0x00;                                    // on, X bit 8 clear
    m.run_frame(audio, 1024);
    const mappy::VideoState& vs = m.video();
    CHECK_EQ(vs.sprite_count, 1);
    CHECK_EQ(vs.sprite[0].x, 0x50 - 40);
    CHECK_EQ(vs.sprite[0].y, ((256 - 0x80 + 1 - 16) & 0xff) - 32);
    CHECK_EQ(vs.sprite[0].w, 32);
    CHECK_EQ(vs.sprite[0].h, 32);
    CHECK_EQ(vs.sprite[0].code, 0x24);               // size bits cleared
    CHECK_EQ(vs.sprite[0].colour, 3);
    CHECK_EQ(vs.sprite[0].flip, 3);
    s2[11] = 0x10;
    s3[11] = 0x01;                                    // X bit 8
    m.run_frame(audio, 1024);
    CHECK_EQ(m.video().sprite_count, 1);
    CHECK_EQ(m.video().sprite[0].x, 0x110 - 40);
    s2[11] = 0x50;                                    // X 0x150 - 40: off the board's 288 columns
    m.run_frame(audio, 1024);
    CHECK_EQ(m.video().sprite_count, 0);
    CHECK_EQ(m.watchdog_resets(), 0);
}

// The same program with and without idle skipping: every frame's RAM,
// registers and sound the same.
void test_idle_skip()
{
    static Roms roms;
    static mappy::Machine a, b;
    a.init(test_board(), roms.regions, 24000, false);
    b.init(test_board(), roms.regions, 24000, false);
    b.set_idle_skip(false);
    static int16_t wa[1024], wb[1024];
    bool same = true;
    for (int f = 0; f < 120 && same; ++f) {
        if (f == 40) {
            for (mappy::Machine* m : {&a, &b}) {
                uint8_t* r = m->shared();
                r[3] = 0x0f;
                r[4] = 0x00;
                r[5] = 0x40;
                r[6] = 0x20;
            }
        }
        const int na = a.run_frame(wa, 1024), nb = b.run_frame(wb, 1024);
        same = na == nb && std::memcmp(wa, wb, sizeof(int16_t) * size_t(na)) == 0 &&
               std::memcmp(a.ram(), b.ram(), 0x1800) == 0 && std::memcmp(a.shared(), b.shared(), 0x400) == 0 &&
               a.main_cpu().pc == b.main_cpu().pc && a.main_cpu().d == b.main_cpu().d &&
               a.main_cpu().cc == b.main_cpu().cc && a.sub_cpu().pc == b.sub_cpu().pc &&
               a.sub_cpu().d == b.sub_cpu().d && a.sub_cpu().cc == b.sub_cpu().cc;
        if (!same) std::printf("idle skipping differs at frame %d\n", f);
    }
    CHECK(same);
    CHECK_EQ(a.ram()[0x002], 120);
}

// --- the picture ---------------------------------------------------------------------

uint16_t g_line[mappy::kScreenW];

void test_video()
{
    static Roms roms;
    static mappy::Machine m;
    m.init(test_board(), roms.regions, 24000, false);
    static mappy::VideoState vs;
    std::memset(&vs, 0, sizeof vs);
    auto px = [&](int y, int x) {
        m.render_line(vs, y, g_line);
        return g_line[x];
    };
    const uint16_t bg = m.char_pen(0, 0);

    // Tile 1, colour 2, at map column 3 (board column 5 = display lines
    // 40-47), row 4 (board lines 32-39 = display x 184-191).
    vs.vram[3 + 4 * 32] = 1;
    vs.vram[0x800 + 3 + 4 * 32] = 2;
    CHECK_EQ(px(40, 184), m.char_pen(2, 3));
    CHECK_EQ(px(47, 191), m.char_pen(2, 3));
    CHECK_EQ(px(40, 183), bg);
    CHECK_EQ(px(40, 192), bg);
    CHECK_EQ(px(39, 188), bg);
    CHECK_EQ(px(48, 188), bg);
    // Scrolled 8 lines: one row further right on the display.
    vs.scroll = 8;
    CHECK_EQ(px(40, 192), m.char_pen(2, 3));
    CHECK_EQ(px(40, 199), m.char_pen(2, 3));
    CHECK_EQ(px(40, 191), bg);
    // The map's last row (59, lines 472-479) at the bottom of the board
    // when scrolled 250: board lines 222-223 = display x 1-0.
    vs.vram[3 + 59 * 32] = 1;
    vs.vram[0x800 + 3 + 59 * 32] = 2;
    vs.scroll = 250;
    CHECK_EQ(px(40, 0), m.char_pen(2, 3));
    CHECK_EQ(px(40, 1), m.char_pen(2, 3));
    CHECK_EQ(px(40, 2), bg);
    vs.scroll = 0;
    // Tile 2: one pixel (x 0, y 0) of value 2.
    vs.vram[10 + 20 * 32] = 2;
    vs.vram[0x800 + 10 + 20 * 32] = 7;
    CHECK_EQ(px(96, 223 - 160), m.char_pen(7, 2));
    CHECK_EQ(px(97, 223 - 160), m.char_pen(7, 0));
    CHECK_EQ(px(96, 223 - 161), m.char_pen(7, 0));
    // Fixed column 0 (display lines 0-7), row 3: not scrolled.
    const int fixed = ((3 + 2) & 0x0f) + (3 & 0x10) + (((0 - 2) & 3) << 5) + 0x780;
    vs.vram[fixed] = 1;
    vs.vram[0x800 + fixed] = 5;
    vs.scroll = 100;
    CHECK_EQ(px(0, 223 - 24), m.char_pen(5, 3));
    CHECK_EQ(px(7, 223 - 31), m.char_pen(5, 3));
    CHECK_EQ(px(0, 223 - 32), bg);
    vs.scroll = 0;

    // Sprite 3 (all pixels 14) at board (100, 50): display lines 100-115,
    // x 158-173.
    mappy::Sprite& s = vs.sprite[0];
    s = {100, 50, 16, 16, 3, 1, 0};
    vs.sprite_count = 1;
    CHECK_EQ(px(100, 173), m.sprite_pen(1, 14));
    CHECK_EQ(px(115, 158), m.sprite_pen(1, 14));
    CHECK_EQ(px(99, 165), bg);
    CHECK_EQ(px(116, 165), bg);
    CHECK_EQ(px(100, 157), bg);
    CHECK_EQ(px(100, 174), bg);
    // A front character over it: its opaque pens hide the sprite, its
    // see-through pen (lookup 15) doesn't. Colour 3 pen 3 = lookup 15.
    vs.vram[12 + 6 * 32] = 1;                         // board column 14 (lines 112-119), lines 48-55
    vs.vram[0x800 + 12 + 6 * 32] = 0x40 | 2;
    CHECK_EQ(px(112, 223 - 52), m.char_pen(2, 3));
    vs.vram[0x800 + 12 + 6 * 32] = 0x40 | 3;
    CHECK_EQ(px(112, 223 - 52), m.sprite_pen(1, 14));
    vs.vram[0x800 + 12 + 6 * 32] = 2;                 // a back character: the sprite is in front
    CHECK_EQ(px(112, 223 - 52), m.sprite_pen(1, 14));
    vs.vram[12 + 6 * 32] = 0;
    // Sprite 4: only its (0, 0) pixel, value 8; flips.
    s = {200, 100, 16, 16, 4, 2, 0};
    CHECK_EQ(px(200, 123), m.sprite_pen(2, 8));
    CHECK_EQ(px(201, 123), bg);
    s.flip = 1;
    CHECK_EQ(px(215, 123), m.sprite_pen(2, 8));
    CHECK_EQ(px(200, 123), bg);
    s.flip = 2;
    CHECK_EQ(px(200, 108), m.sprite_pen(2, 8));
    // Double size: cells code, +1 (to the right on the board), +2 (below);
    // flipped they swap.
    s = {200, 100, 32, 32, 2, 2, 0};                  // cell 4 = code + 2: board (200, 116)
    CHECK_EQ(px(200, 223 - 116), m.sprite_pen(2, 8));
    s.flip = 2;                                       // flipped Y: cell 4 on top, its pixel at the bottom
    CHECK_EQ(px(200, 223 - 115), m.sprite_pen(2, 8));
    s = {200, 100, 32, 32, 2, 2, 1};                  // flipped X: cell 4 on the right, its pixel last
    CHECK_EQ(px(231, 223 - 116), m.sprite_pen(2, 8));
    CHECK_EQ(px(200, 223 - 116), bg);
    // Off the top of the board: only the visible part drawn.
    s = {200, -10, 16, 16, 3, 1, 0};
    CHECK_EQ(px(200, 223), m.sprite_pen(1, 14));
    CHECK_EQ(px(200, 223 - 5), m.sprite_pen(1, 14));
    CHECK_EQ(px(200, 223 - 6), bg);
}

} // namespace

int main()
{
    test_io_ports();
    test_io_coins();
    test_io_checks();
    test_wiring();
    test_idle_skip();
    test_video();
    std::printf("mappy_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
