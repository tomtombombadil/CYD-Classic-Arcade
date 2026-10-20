// Unit tests for the Galaxian-family parts, with hand-made data - no ROMs:
// the shared video (tiles with column scroll and colour, sprites with
// flips, clip and the sprite 0-2 line, bullets and the one-shell-a-line
// rule, the star field against a brute-force LFSR table, the background
// colour), the Galaxian sound model (555s, tone counter, HIT, FIRE),
// Galaxian's and Amidar's wiring on tiny Z80 programs (inputs, NMI,
// watchdog, sound latch / IRQ / AYs / filters, the counter chain), and
// that Amidar's timer-wait skipping leaves everything as it is without it.
//
//   galaxian_tests
#include "../../emu/machines/amidar.h"
#include "../../emu/machines/galaxian.h"
#include "../../emu/machines/galaxian_video.h"
#include "../../emu/sound/galaxian_sound.h"

#include <cmath>
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

using galaxian_video::Video;
using galaxian_video::VideoState;

uint16_t g_line[galaxian_video::kScreenW];

// Display line y's pixel at display x.
uint16_t pixel(const Video& v, const VideoState& vs, int y, int x)
{
    v.render_line(vs, y, g_line);
    return g_line[x];
}

// --- video ---------------------------------------------------------------------

void test_video_layers()
{
    static uint8_t prom[32], gfx[0x1000], vram[0x400], obj[0x100];
    for (int e = 0; e < 32; ++e) prom[e] = uint8_t(e * 37 + 1);     // 32 different colours
    std::memset(gfx, 0, sizeof gfx);
    std::memset(gfx + 8, 0xff, 8);                  // tile 1: every pixel value 3
    std::memset(gfx + 0x808, 0xff, 8);
    gfx[16] = 0x80;                                 // tile 2: only (x 0, line 0), value 2
    std::memset(gfx + 0x800 + 64, 0xff, 32);        // sprite 2: every pixel value 1
    gfx[96] = 0x80;                                 // sprite 3: only (0, 0), value 2
    std::memset(vram, 0, sizeof vram);
    std::memset(obj, 0, sizeof obj);

    Video v;
    v.init(prom, gfx, {false, true, false}, false);
    static VideoState vs;
    const uint16_t black = 0;

    // Tile 1 at tile row 5, column 3, colour 2: hardware lines 40-47, pixels
    // 24-31 = display lines 24-31, display x 192-199.
    vram[5 * 32 + 3] = 1;
    obj[3 * 2 + 1] = 2;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 24, 192), v.pen(2 * 4 + 3));
    CHECK_EQ(pixel(v, vs, 31, 199), v.pen(2 * 4 + 3));
    CHECK_EQ(pixel(v, vs, 24, 191), black);
    CHECK_EQ(pixel(v, vs, 24, 200), black);
    CHECK_EQ(pixel(v, vs, 23, 195), black);         // column 2: nothing
    CHECK_EQ(pixel(v, vs, 32, 195), black);
    // The column's scroll moves it 8 lines up the board: display x 200-207.
    obj[3 * 2] = 8;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 28, 200), v.pen(11));
    CHECK_EQ(pixel(v, vs, 28, 207), v.pen(11));
    CHECK_EQ(pixel(v, vs, 28, 199), black);
    // Scroll wraps round the 256-line map: 0xF8 moves it 8 lines down.
    obj[3 * 2] = 0xf8;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 28, 184), v.pen(11));
    CHECK_EQ(pixel(v, vs, 28, 191), v.pen(11));
    obj[3 * 2] = 0;
    // Tile 2 (one pixel) at row 10, column 6, colour 1: line 80, pixel 48.
    vram[10 * 32 + 6] = 2;
    obj[6 * 2 + 1] = 1;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 48, 159), v.pen(1 * 4 + 2));
    CHECK_EQ(pixel(v, vs, 48, 158), black);
    CHECK_EQ(pixel(v, vs, 49, 159), black);
    // Lines 0-15 and 240-255 aren't shown: a tile at row 1 is off screen.
    vram[1 * 32 + 6] = 1;
    v.capture(vs, vram, obj, 0, false, 0);
    for (int x = 0; x < galaxian_video::kScreenW; ++x) CHECK(pixel(v, vs, 50, x) == black || x == 159);
    std::memset(vram, 0, sizeof vram);

    // Sprite 4: code 2, colour 3, Y byte 100, X byte 50: lines 140-155,
    // pixels 51-66 = display lines 51-66, x 84-99.
    uint8_t* sp = obj + 0x40;
    sp[4 * 4 + 0] = 100; sp[4 * 4 + 1] = 2; sp[4 * 4 + 2] = 3; sp[4 * 4 + 3] = 50;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 51, 84), v.pen(13));
    CHECK_EQ(pixel(v, vs, 51, 99), v.pen(13));
    CHECK_EQ(pixel(v, vs, 66, 84), v.pen(13));
    CHECK_EQ(pixel(v, vs, 50, 90), black);
    CHECK_EQ(pixel(v, vs, 67, 90), black);
    CHECK_EQ(pixel(v, vs, 58, 83), black);
    CHECK_EQ(pixel(v, vs, 58, 100), black);
    // Sprites 0-2 sit one line lower on the board (one display pixel left).
    sp[4 * 4 + 1] = 0;
    sp[0] = 100; sp[1] = 2; sp[2] = 3; sp[3] = 50;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 58, 83), v.pen(13));
    CHECK_EQ(pixel(v, vs, 58, 98), v.pen(13));
    CHECK_EQ(pixel(v, vs, 58, 99), black);
    // Lower numbers on top: sprite 1 (colour 5) under sprite 0.
    sp[4] = 100; sp[5] = 2; sp[6] = 5; sp[7] = 50;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 58, 90), v.pen(13));
    sp[4] = sp[5] = sp[6] = sp[7] = 0;
    // The first 17 pixels of a scanline show no sprites: X 8 -> pixels 9-24.
    sp[3] = 8;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 16, 90), black);
    CHECK_EQ(pixel(v, vs, 17, 90), v.pen(13));
    CHECK_EQ(pixel(v, vs, 24, 90), v.pen(13));
    // Flips: sprite 3's one pixel at (0, 0); X flip -> pixel 15, Y flip ->
    // line 15. Sprite 5 at Y byte 200 (line 40), X byte 100 (pixels 101-116).
    sp[0] = sp[1] = sp[2] = sp[3] = 0;
    sp[20] = 200; sp[21] = 3; sp[22] = 1; sp[23] = 100;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 101, 239 - 40), v.pen(6));
    sp[21] = 3 | 0x40;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 101, 239 - 40), black);
    CHECK_EQ(pixel(v, vs, 116, 239 - 40), v.pen(6));
    sp[21] = 3 | 0xc0;
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 116, 239 - 55), v.pen(6));
    CHECK_EQ(pixel(v, vs, 116, 239 - 40), black);
    std::memset(sp, 0, 32);

    // Bullets: the missile (7) at X byte 100 (pixels 151-154), Y byte 0x87
    // (line 120 = display x 119), yellow.
    uint8_t* b = obj + 0x60;
    b[7 * 4 + 1] = 0x87; b[7 * 4 + 3] = 100;
    v.capture(vs, vram, obj, 0, false, 0);
    const uint16_t yellow = pixel(v, vs, 151, 119);
    CHECK(yellow != black);
    CHECK_EQ(pixel(v, vs, 154, 119), yellow);
    CHECK_EQ(pixel(v, vs, 150, 119), black);
    CHECK_EQ(pixel(v, vs, 155, 119), black);
    CHECK_EQ(pixel(v, vs, 152, 118), black);
    // Shells: 1 (Y byte 0x100 - line) and 4 (0xFF - line) on line 60: only
    // the later one, 4, shows; white.
    b[1 * 4 + 1] = uint8_t(0x100 - 60); b[1 * 4 + 3] = 30;     // pixels 221-224
    b[4 * 4 + 1] = uint8_t(0xff - 60); b[4 * 4 + 3] = 60;      // pixels 191-194
    v.capture(vs, vram, obj, 0, false, 0);
    const uint16_t white = pixel(v, vs, 191, 179);
    CHECK(white != black && white != yellow);
    CHECK_EQ(pixel(v, vs, 222, 179), black);
    b[4 * 4 + 1] = uint8_t(0xff - 61);                          // 4 moves a line: 1 shows
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(v, vs, 222, 179), white);
    CHECK_EQ(pixel(v, vs, 191, 178), white);
    // A board without bullets draws none.
    Video nb;
    nb.init(prom, gfx, {false, false, true}, false);
    nb.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(pixel(nb, vs, 191, 178), black);
    // The background colour (Konami): bits red, green, blue.
    nb.capture(vs, vram, obj, 5, false, 0);
    const uint16_t magenta = pixel(nb, vs, 10, 10);
    CHECK_EQ(magenta, uint16_t(((0x55 >> 3) << 11) | (0x55 >> 3)));
    nb.capture(vs, vram, obj, 2, false, 0);
    CHECK_EQ(pixel(nb, vs, 10, 10), uint16_t((0x47 >> 2) << 5));
}

void test_stars()
{
    using galaxian_video::kStarPeriod;
    // The generator's period, and the states that light: 256 of them.
    std::vector<uint8_t> table(kStarPeriod);
    uint32_t s = 0;
    int lit = 0;
    for (uint32_t i = 0; i < kStarPeriod; ++i) {
        const bool on = (s & 0x1fe01) == 0x1fe00;
        table[i] = uint8_t(on ? 0x80 | ((~s >> 3) & 0x3f) : 0);
        lit += on;
        s = galaxian_video::star_step(s);
    }
    CHECK_EQ(s, 0u);                                    // back where it started
    CHECK_EQ(lit, 256);

    static uint8_t prom[32], gfx[0x1000], vram[0x400], obj[0x100];
    std::memset(prom, 0, sizeof prom);
    Video v;
    v.init(prom, gfx, {true, true, false}, false);
    static VideoState vs;
    for (uint32_t origin : {0u, 12345u, kStarPeriod - 1}) {
        v.capture(vs, vram, obj, 0, true, origin);
        // Brute force: line L, pixel x reads states origin + 512 L + 2x and
        // + 1; lit where (L ^ x / 8) is odd; the second state wins.
        int expected = 0, wrong = 0;
        for (int y = 0; y < galaxian_video::kScreenH; ++y) {
            v.render_line(vs, y, g_line);
            for (int line = galaxian_video::kFirstLine; line <= galaxian_video::kLastLine; ++line) {
                uint16_t want = 0;
                if ((line ^ (y >> 3)) & 1)
                    for (int k = 0; k < 2; ++k) {
                        const uint8_t t = table[(origin + uint32_t(line) * 512 + uint32_t(2 * y + k)) % kStarPeriod];
                        if (t & 0x80) want = v.star_colour(t & 0x3f);
                    }
                if (want) ++expected;
                if (g_line[galaxian_video::display_x(line)] != want) ++wrong;
            }
        }
        CHECK_EQ(wrong, 0);
        CHECK(expected > 60);
        CHECK(vs.star_count >= expected);
    }
    // Stars off: none.
    v.capture(vs, vram, obj, 0, false, 0);
    CHECK_EQ(vs.star_count, 0);
}

// --- Galaxian sound ----------------------------------------------------------------

std::vector<int16_t> render(galaxian_sound::Sound& s, int n)
{
    std::vector<int16_t> out(size_t(n), 0);
    for (int i = 0; i < n; i += 400) s.render(out.data() + i, std::min(400, n - i));
    return out;
}

int peak(const std::vector<int16_t>& v, int from, int to)
{
    int p = 0;
    for (int i = from; i < to; ++i) p = std::max(p, std::abs(int(v[size_t(i)])));
    return p;
}

void test_galaxian_sound()
{
    using galaxian_sound::Sound;
    // A 555 at its usual 2/3 Vcc: high (Ra + Rb) C ln 2, low Rb C ln 2.
    galaxian_sound::Astable a;
    const float dt = 1.0f / 24000;
    a.init(100e3f, 470e3f, 0.01e-6f, dt);
    int edges = 0;
    bool was = false;
    for (int i = 0; i < 24000 * 2; ++i) {
        const float h = a.run(5.0f * 2 / 3, dt);
        const bool now = h > 0.5f;
        if (now && !was) ++edges;
        was = now;
    }
    const double hz = 1.0 / ((570e3 + 470e3) * 0.01e-6 * std::log(2.0));
    CHECK(std::abs(edges / 2.0 - hz) < hz * 0.02);
    // A higher control voltage: lower pitch.
    galaxian_sound::Astable b;
    b.init(100e3f, 470e3f, 0.01e-6f, dt);
    int edges_b = 0;
    was = false;
    for (int i = 0; i < 24000; ++i) {
        const bool now = b.run(4.5f, dt) > 0.5f;
        if (now && !was) ++edges_b;
        was = now;
    }
    CHECK(edges_b < edges / 2 - 20);

    // The background's slow 555 runs faster for higher codes; its sawtooth
    // gives the FS control voltage 2.6-5 V.
    for (int c = 1; c < 16; ++c) CHECK(Sound::lfo_hz(c) > Sound::lfo_hz(c - 1));
    CHECK(Sound::lfo_hz(0) > 14 && Sound::lfo_hz(15) < 23);
    CHECK(std::abs(Sound::fs_cv(5.0f / 3) - 1.31f) < 0.02f);
    CHECK(std::abs(Sound::fs_cv(10.0f / 3) - 3.69f) < 0.02f);
    CHECK_EQ(Sound::fs_cv(0), 0.0f);

    // Tone: pitch 0x80 = 1.536 MHz / 128 = 12 kHz into the counter, QD at
    // 750 Hz: with VOL2 on the wave repeats every 32 samples (24 kHz), and
    // not every 16.
    Sound s;
    s.init(24000);
    s.pitch(0x80);
    s.control(galaxian_sound::kVol2, true);
    std::vector<int16_t> t = render(s, 24000);
    int same = 0, half = 0;
    for (int i = 12000; i < 13000; ++i) {
        same = std::max(same, std::abs(t[size_t(i)] - t[size_t(i + 32)]));
        half = std::max(half, std::abs(t[size_t(i)] - t[size_t(i + 16)]));
    }
    CHECK(same < 100);
    CHECK(half > 2000);
    CHECK(peak(t, 12000, 24000) > 3000);
    // Pitch FF (the game's "off"): the counter runs at 1.5 MHz - silence.
    s.pitch(0xff);
    t = render(s, 24000);
    CHECK(peak(t, 12000, 24000) < 50);

    // HIT: noise while on, gone a few seconds after.
    s.reset();
    s.control(galaxian_sound::kHit, true);
    t = render(s, 2400);
    CHECK(peak(t, 0, 2400) > 1000);
    s.control(galaxian_sound::kHit, false);
    t = render(s, 24000 * 4);
    CHECK(peak(t, 24000 * 3, 24000 * 4) < 50);
    // FIRE: the shot while on, rung out 1 s after.
    s.reset();
    s.control(galaxian_sound::kFire, true);
    t = render(s, 4800);
    CHECK(peak(t, 0, 4800) > 3000);
    s.control(galaxian_sound::kFire, false);
    t = render(s, 24000 * 2);
    CHECK(peak(t, 24000, 48000) < 50);
    // FS1: the swarm.
    s.reset();
    s.control(galaxian_sound::kFs1, true);
    t = render(s, 24000);
    CHECK(peak(t, 12000, 24000) > 600);
    s.control(galaxian_sound::kFs1, false);
    t = render(s, 24000);
    CHECK(peak(t, 12000, 24000) < 50);
}

// --- Galaxian board ------------------------------------------------------------------

void test_galaxian_machine()
{
    static uint8_t cpu[0x2800], gfx[0x1000], prom[32];
    std::memset(cpu, 0, sizeof cpu);
    // 0000: set up, then read the inputs and the watchdog in a loop; the
    // NMI counts at 4000.
    const uint8_t code[] = {
        0x31, 0x00, 0x44,               // LD SP,4400
        0x3e, 0x01,                     // LD A,1
        0x32, 0x01, 0x70,               // LD (7001),A   NMI on
        0x32, 0x04, 0x70,               // LD (7004),A   stars on
        0x32, 0x06, 0x68,               // LD (6806),A   VOL1
        0x32, 0x07, 0x68,               // LD (6807),A   VOL2
        0x32, 0x00, 0x68,               // LD (6800),A   FS1
        0x32, 0x04, 0x60,               // LD (6004),A   LFO bit 0
        0x32, 0x06, 0x60,               // LD (6006),A   LFO bit 2
        0x3e, 0x40,                     // LD A,40
        0x32, 0x00, 0x78,               // LD (7800),A   pitch
        // loop (001F):
        0x3a, 0x00, 0x60, 0x32, 0x10, 0x40,     // IN0 -> 4010
        0x3a, 0x00, 0x68, 0x32, 0x11, 0x40,     // IN1 -> 4011
        0x3a, 0x00, 0x70, 0x32, 0x12, 0x40,     // IN2 -> 4012
        0x3a, 0x00, 0x78,                       // watchdog
        0x18, 0xe9,                             // JR loop
    };
    std::memcpy(cpu, code, sizeof code);
    CHECK_EQ(sizeof code, 0x1f + 23);
    // 0066: NMI: INC (4000); RETN.
    const uint8_t nmi[] = {0xf5, 0x3a, 0x00, 0x40, 0x3c, 0x32, 0x00, 0x40, 0xf1, 0xed, 0x45};
    std::memcpy(cpu + 0x66, nmi, sizeof nmi);
    std::memset(gfx, 0, sizeof gfx);
    std::memset(prom, 0, sizeof prom);

    static galaxian::Machine m;
    m.init({cpu, gfx, prom}, 24000);
    int16_t audio[1024];
    int samples = 0, loud = 0;
    for (int f = 0; f < 30; ++f) {
        const int n = m.run_frame(audio, 1024);
        samples += n;
        for (int i = 0; i < n; ++i) loud = std::max(loud, std::abs(int(audio[i])));
    }
    z80::Cpu& c = m.cpu();
    CHECK(samples >= 30 * 395 && samples <= 30 * 397);   // 24000 Hz / 60.606 Hz
    CHECK_EQ(c.rd(0x4000), 30);                          // one NMI a frame
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK(m.nmi_enabled());
    CHECK_EQ(m.pitch(), 0x40);
    CHECK_EQ(m.sound_control(), 0xc1);
    CHECK_EQ(m.lfo(), 0x05);
    CHECK(loud > 2000);
    CHECK(m.video().star_count > 0);
    // Inputs, active high.
    m.set_inputs(galaxian::kLeft | galaxian::kFire | galaxian::kCoin);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x4010), 0x15);
    CHECK_EQ(c.rd(0x4011), 0x00);
    CHECK_EQ(c.rd(0x4012), galaxian::kDipDefault);
    m.set_inputs(galaxian::kRight | galaxian::kStart1 | galaxian::kStart2);
    m.set_dips(galaxian::kDipBonus12000);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x4010), 0x08);
    CHECK_EQ(c.rd(0x4011), 0x03);
    CHECK_EQ(c.rd(0x4012), 0x02);
    // Mirrors: RAM at 4400, tiles at 5400, object RAM every 256 to 5FFF.
    c.wr(0x4401, 0x77);
    CHECK_EQ(c.rd(0x4001), 0x77);
    c.wr(0x5402, 0x33);
    CHECK_EQ(c.rd(0x5002), 0x33);
    c.wr(0x5f43, 0x99);
    CHECK_EQ(c.rd(0x5843), 0x99);
    CHECK_EQ(c.rd(0x2800), 0xff);                        // no chips there
    // The object RAM reaches the picture at VBLANK.
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.video().obj[0x43], 0x99);
    CHECK_EQ(m.video().vram[2], 0x33);

    // A program that never reads 7800: the watchdog resets it every 8 frames.
    static uint8_t cpu2[0x2800];
    std::memset(cpu2, 0, sizeof cpu2);
    cpu2[0] = 0x18; cpu2[1] = 0xfe;                      // JR $
    static galaxian::Machine m2;
    m2.init({cpu2, gfx, prom}, 24000);
    for (int f = 0; f < 20; ++f) m2.run_frame(nullptr, 0);
    CHECK_EQ(m2.watchdog_resets(), 2);
}

// --- Amidar board ------------------------------------------------------------------------

void test_amidar_timer()
{
    using amidar::timer_bits;
    CHECK_EQ(timer_bits(0), 0x0e);
    CHECK_EQ(timer_bits(2047), 0x0e);
    CHECK_EQ(timer_bits(2048), 0x1e);                   // the divide-by-8's top bit
    CHECK_EQ(timer_bits(8192), 0x2e);                   // the divide-by-5: 2
    CHECK_EQ(timer_bits(16384), 0x4e);                  // 4
    CHECK_EQ(timer_bits(20480), 0x8e);                  // the last divide-by-2
    CHECK_EQ(timer_bits(20480 + 16384 + 2048), 0xde);
    CHECK_EQ(timer_bits(40960), 0x0e);                  // a whole turn
    CHECK_EQ(amidar::kTimerPeriod, 40960u);
}

// The sound program for the board test: the game's timer wait loops at
// 01E2 / 01E9 (so the machine skips them), counting periods at 8002; the
// IRQ handler takes the command (port A) to 8000, counts at 8001, and
// writes both AYs and the filters.
void sound_program(uint8_t* snd)
{
    std::memset(snd, 0xff, 0x2000);
    const uint8_t start[] = {0x31, 0x00, 0x84, 0xed, 0x56, 0xc3, 0x00, 0x01};   // LD SP,8400; IM 1; JP 0100
    std::memcpy(snd, start, sizeof start);
    const uint8_t rst08[] = {0xd3, 0x40, 0xdb, 0x80, 0xc9};      // OUT (40),A; IN A,(80); RET
    std::memcpy(snd + 0x08, rst08, sizeof rst08);
    const uint8_t isr[] = {
        0xf5,                           // PUSH AF
        0x3e, 0x0e, 0xcf,               // LD A,0E; RST 08: port A
        0x32, 0x00, 0x80,               // LD (8000),A
        0x3a, 0x01, 0x80, 0x3c, 0x32, 0x01, 0x80,   // INC (8001)
        0x3e, 0x08, 0xd3, 0x40, 0x3e, 0x0f, 0xd3, 0x80,   // AY 1 R8 = 0F
        0x3e, 0x00, 0xd3, 0x40, 0x3e, 0x40, 0xd3, 0x80,   // AY 1 R0 = 40
        0x3e, 0x07, 0xd3, 0x10, 0x3e, 0x3e, 0xd3, 0x20,   // AY 2 R7 = 3E
        0x32, 0x03, 0x90,               // LD (9003),A: filters 003
        0xf1, 0xfb, 0xed, 0x4d,         // POP AF; EI; RETI
    };
    std::memcpy(snd + 0x38, isr, sizeof isr);
    const uint8_t main_code[] = {
        0x3e, 0x07, 0xd3, 0x40, 0x3e, 0x3e, 0xd3, 0x80,   // AY 1 R7 = 3E: tone A on
        0xc3, 0xe1, 0x01,               // JP 01E1
    };
    std::memcpy(snd + 0x100, main_code, sizeof main_code);
    const uint8_t wait[] = {
        0xfb,                                       // 01E1 EI
        0x3e, 0x0f, 0xcf, 0xe6, 0x80, 0x20, 0xf9,   // 01E2 wait while bit 7 set
        0x3e, 0x0f, 0xcf, 0xe6, 0x80, 0x28, 0xf9,   // 01E9 wait while clear
        0x21, 0x02, 0x80, 0x34,                     // 01F0 INC (8002)
        0xc3, 0xe2, 0x01,                           // JP 01E2
    };
    std::memcpy(snd + 0x1e1, wait, sizeof wait);
}

void test_amidar_machine()
{
    static uint8_t cpu[0x4000], snd[0x2000], gfx[0x1000], prom[32];
    std::memset(cpu, 0, sizeof cpu);
    const uint8_t code[] = {
        0x31, 0x00, 0x88,               // LD SP,8800
        0x3e, 0x01,                     // LD A,1
        0x32, 0x08, 0xa0,               // LD (A008),A  NMI on
        0x32, 0x00, 0xa0,               // LD (A000),A  background red
        0x32, 0x28, 0xa0,               // LD (A028),A  background blue
        0x3e, 0x5a, 0x32, 0x00, 0xb8,   // the command 5A (PPI 2 port A)
        0x3e, 0x08, 0x32, 0x10, 0xb8,   // control bit 3 up (port B)...
        0xaf, 0x32, 0x10, 0xb8,         // ... and down: the sound IRQ
        // loop (001C):
        0x3a, 0x00, 0xb0, 0x32, 0x10, 0x80,     // IN0 -> 8010
        0x3a, 0x10, 0xb0, 0x32, 0x11, 0x80,     // IN1 -> 8011
        0x3a, 0x20, 0xb0, 0x32, 0x12, 0x80,     // IN2 -> 8012
        0x3a, 0x20, 0xb8, 0x32, 0x13, 0x80,     // IN3 -> 8013
        0x3a, 0x00, 0xa8,                       // watchdog
        0x18, 0xe3,                             // JR loop
    };
    CHECK_EQ(sizeof code, 0x1c + 29);
    std::memcpy(cpu, code, sizeof code);
    const uint8_t nmi[] = {0xf5, 0x3a, 0x00, 0x80, 0x3c, 0x32, 0x00, 0x80, 0xf1, 0xed, 0x45};
    std::memcpy(cpu + 0x66, nmi, sizeof nmi);
    sound_program(snd);
    std::memset(gfx, 0, sizeof gfx);
    std::memset(prom, 0, sizeof prom);

    static amidar::Machine m, plain;
    m.init({cpu, snd, gfx, prom}, 24000);
    plain.init({cpu, snd, gfx, prom}, 24000);
    plain.set_idle_skip(false);
    int16_t audio[1024], audio2[1024];
    int loud = 0;
    bool same = true;
    for (int f = 0; f < 120; ++f) {
        const int n = m.run_frame(audio, 1024);
        const int n2 = plain.run_frame(audio2, 1024);
        // Skipping the waits leaves everything as it is without.
        same = same && n == n2 && std::memcmp(audio, audio2, size_t(n) * 2) == 0 &&
               m.sound_cpu().pc.w == plain.sound_cpu().pc.w && m.sound_cpu().af.w == plain.sound_cpu().af.w &&
               m.sound_cpu().r_value() == plain.sound_cpu().r_value() &&
               m.sound_cpu().rd(0x8002) == plain.sound_cpu().rd(0x8002);
        for (int i = 0; i < n; ++i) loud = std::max(loud, std::abs(int(audio[i])));
    }
    CHECK(same);
    CHECK(m.skipped_cycles() > 120 * 20000ull);
    CHECK_EQ(plain.skipped_cycles(), 0ull);

    z80::Cpu& c = m.cpu();
    z80::Cpu& s = m.sound_cpu();
    CHECK_EQ(c.rd(0x8000), 120);                        // one NMI a frame
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(m.video().background, 5);
    // The command reached the sound CPU once, by its IRQ (held until taken).
    CHECK_EQ(m.sound_irqs(), 1u);
    CHECK_EQ(s.rd(0x8000), 0x5a);
    CHECK_EQ(s.rd(0x8001), 1);
    CHECK_EQ(m.ay(0).reg(8), 0x0f);
    CHECK_EQ(m.ay(0).reg(0), 0x40);
    CHECK_EQ(m.ay(0).reg(7), 0x3e);
    CHECK_EQ(m.ay(1).reg(7), 0x3e);
    CHECK_EQ(m.filters(), 0x003);
    CHECK(loud > 2000);
    // The waits: one count per turn of the chain (5120 sound cycles); 120
    // frames are 120 x 29531.3 cycles: 692 counts (a byte: 692 - 512).
    const int periods = s.rd(0x8002) + 512;
    CHECK(periods >= 120 * 29531 / 5120 - 2 && periods <= 120 * 29531 / 5120 + 1);
    // RAM mirrors (main C000, sound A000 / E400).
    c.wr(0xc005, 0x42);
    CHECK_EQ(c.rd(0x8005), 0x42);
    s.wr(0xe405, 0x24);
    CHECK_EQ(s.rd(0x8005), 0x24);

    // Inputs, active low.
    m.set_inputs(0);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x8010), 0xff);
    CHECK_EQ(c.rd(0x8011), 0xff);                       // 3 lives
    CHECK_EQ(c.rd(0x8012), 0xf1);                       // demo sounds on, bonus 30000, upright
    CHECK_EQ(c.rd(0x8013), 0xff);
    m.set_inputs(amidar::kJump | amidar::kLeft | amidar::kCoin | amidar::kStart1 | amidar::kUp);
    m.set_dips(amidar::kDipLives5 | amidar::kDipBonus50000);
    m.set_demo_sounds(false);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x8010), 0xff & ~0xa8);
    CHECK_EQ(c.rd(0x8011), 0xfd & ~0x80);
    CHECK_EQ(c.rd(0x8012), 0xff & ~0x08 & ~0x10);
    m.set_inputs(amidar::kDown | amidar::kRight | amidar::kStart2);
    m.run_frame(nullptr, 0);
    CHECK_EQ(c.rd(0x8010), 0xff & ~0x10);
    CHECK_EQ(c.rd(0x8011), 0xfd & ~0x40);
    CHECK_EQ(c.rd(0x8012), 0xff & ~0x08 & ~0x40);

    // Mute (control bit 4): silence.
    c.wr(0xb810, 0x10);
    CHECK(m.muted());
    for (int f = 0; f < 3; ++f) m.run_frame(audio, 1024);  // the coupling's tail
    const int n = m.run_frame(audio, 1024);
    int quiet = 0;
    for (int i = 0; i < n; ++i) quiet = std::max(quiet, std::abs(int(audio[i])));
    CHECK(quiet < 300);
}

} // namespace

int main()
{
    test_video_layers();
    test_stars();
    test_galaxian_sound();
    test_galaxian_machine();
    test_amidar_timer();
    test_amidar_machine();
    std::printf("Galaxian / Amidar tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
