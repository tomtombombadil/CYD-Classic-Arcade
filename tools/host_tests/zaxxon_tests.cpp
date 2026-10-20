// Host tests (no ROMs) for the Zaxxon machine and its sound models: the
// palette's resistor network, the picture (skewed scrolling background,
// 32 x 32 sprites with flips and wrapping, characters with their colours
// by position, both colour banks, the background switch) against a plain
// per-pixel model of the board on synthetic graphics, the VBLANK interrupt
// held until the enable is turned off, the inputs and coin latches, the
// 8255's outputs reaching the sound models (one shots on falling edges,
// held sounds, the mode write, port C's bit set / reset, mirrors), the
// sound levels, and that idle skipping stays off for a program that isn't
// set 1's.
//
//   zaxxon_tests
#include "../../emu/machines/zaxxon.h"
#include "../../emu/sound/zaxxon_sound.h"

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

// A small deterministic generator for synthetic graphics.
struct Rng {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

// Synthetic ROMs: a program, random graphics and tile map, and a palette
// PROM whose entry e is e (every entry a different colour).
struct Fake {
    std::vector<uint8_t> cpu = std::vector<uint8_t>(0x5000, 0);
    std::vector<uint8_t> chars = std::vector<uint8_t>(0x1000), bg = std::vector<uint8_t>(0x6000);
    std::vector<uint8_t> sprites = std::vector<uint8_t>(0x6000), map = std::vector<uint8_t>(0x8000);
    std::vector<uint8_t> proms = std::vector<uint8_t>(0x200);

    explicit Fake(uint32_t seed)
    {
        Rng r{seed};
        // Sparse characters and sprites (some see-through pixels), dense
        // background tiles.
        for (uint8_t& b : chars) b = uint8_t(r.next() & r.next());
        for (int i = 0; i < 8 * 8; ++i) chars[0x10 * 8 + (i & 7) + (i >> 3 ? 0x800 : 0)] = 0xff;  // char 0x10 solid
        for (uint8_t& b : bg) b = uint8_t(r.next());
        for (uint8_t& b : sprites) b = uint8_t(r.next() & r.next() & r.next());
        for (uint8_t& b : map) b = uint8_t(r.next());
        for (int e = 0; e < 0x100; ++e) proms[e] = uint8_t(e);
        for (int e = 0x100; e < 0x200; ++e) proms[e] = uint8_t(r.next());
        cpu[0] = 0xf3;                  // DI
        cpu[1] = 0x18; cpu[2] = 0xfe;   // JR $
    }
    zaxxon::Roms roms() const
    {
        return {cpu.data(), chars.data(), bg.data(), sprites.data(), map.data(), proms.data()};
    }
};

// --- palette -------------------------------------------------------------------

void test_palette()
{
    static Fake b(1);
    static zaxxon::Machine m;
    m.init(b.roms(), 24000);
    // Full red / green: 255; full blue 470 / 220 against 470 down: ~244.
    CHECK_EQ(m.pen(0x00), 0x0000);
    CHECK_EQ(m.pen(0x07), 0xf800);
    CHECK_EQ(m.pen(0x38), 0x07e0);
    CHECK_EQ(m.pen(0xc0), 30);                  // 244 >> 3
    // Red's 1K resistor alone: 1/1000 of the network's 1/1000 + 1/470 +
    // 1/220 + 1/470 conductance, scaled so all three give 255: 33.
    CHECK_EQ(m.pen(0x01), (33 >> 3) << 11);
    CHECK_EQ(m.pen(0x04), (149 >> 3) << 11);
    CHECK_EQ(m.pen(0x40), 78 >> 3);
    // Byte-swapped for SPI panels.
    static zaxxon::Machine s;
    s.init(b.roms(), 24000, true);
    CHECK_EQ(s.pen(0x07), 0x00f8);
}

// --- the picture against a per-pixel model -------------------------------------

// Palette entry at display (x, y), straight from the board's description:
// display line y = board column y, display x = board line 239 - x.
int model_pixel(const Fake& b, const uint8_t* fg, const uint8_t* spr, uint16_t pos, bool bg_on, int bg_bank,
                int fg_bank, int x, int y)
{
    const int hx = y, hy = 239 - x;
    int e = 0;
    if (bg_on) {
        const int sx = (hx - hy / 2 + 63) & 255, sy = (hy - 2 * pos) & 4095;
        const int idx = (sy / 8) * 32 + sx / 8;
        const int code = b.map[idx] | ((b.map[0x4000 + idx] & 3) << 8), colour = b.map[0x4000 + idx] >> 4;
        int v = 0;
        for (int p = 0; p < 3; ++p) v |= ((b.bg[p * 0x2000 + code * 8 + (sy & 7)] >> (7 - (sx & 7))) & 1) << p;
        e = bg_bank + colour * 8 + v;
    } else {
        e = -1;                                 // black (not a palette entry)
    }
    for (int offs = 0x7c; offs >= 0; offs -= 4) {
        const int top = (0xef - spr[offs]) & 255, left = (spr[offs + 3] + 0xf0) & 255;
        int px = (hx - left) & 255, py = (hy - top) & 255;
        if (px >= 32 || py >= 32) continue;
        if (spr[offs + 1] & 0x40) px = 31 - px;
        if (spr[offs + 1] & 0x80) py = 31 - py;
        const int code = spr[offs + 1] & 0x3f;
        const int byte = code * 128 + (px / 8) * 8 + (py & 7) + (py / 8) * 32;
        int v = 0;
        for (int p = 0; p < 3; ++p) v |= ((b.sprites[p * 0x2000 + byte] >> (7 - (px & 7))) & 1) << p;
        if (v) e = (spr[offs + 2] & 0x1f) * 8 + v;
    }
    const int tx = hx / 8, ty = hy / 8;
    const int code = fg[ty * 32 + tx];
    const int v = ((b.chars[code * 8 + (hy & 7)] >> (7 - (hx & 7))) & 1)
                | (((b.chars[0x800 + code * 8 + (hy & 7)] >> (7 - (hx & 7))) & 1) << 1);
    if (v) e = fg_bank + (b.proms[0x100 + tx + 32 * (ty / 4)] & 0x0f) * 8 + v;
    return e;
}

void scene(uint32_t seed, bool bg_on, bool banks, bool hud)
{
    static Fake b(seed);
    b = Fake(seed);
    static zaxxon::Machine m;
    m.init(b.roms(), 24000);
    Rng r{seed * 7 + 1};
    uint8_t fg[0x400], spr[0x100];
    for (int i = 0; i < 0x400; ++i) fg[i] = uint8_t(r.next() & 3 ? 0 : r.next());   // mostly see-through
    if (hud) {
        // Solid characters along both ends of some lines (score / fuel lines).
        for (int ty = 0; ty < 32; ++ty)
            for (int tx = 0; tx < 32; ++tx)
                if (tx < 2 || tx > 29 || ty < 5 || ty > 27) fg[ty * 32 + tx] = 0x10;
    }
    for (int i = 0; i < 0x100; ++i) spr[i] = uint8_t(r.next());
    // Some sprites wrap round the right / bottom edges.
    spr[0x00] = 0xef - 250; spr[0x03] = uint8_t(240 - 0xf0);
    spr[0x04] = 0xef - 10;  spr[0x07] = uint8_t(250 - 0xf0);
    const uint16_t pos = uint16_t(r.next() & 0x7ff);
    for (int i = 0; i < 0x400; ++i) m.cpu().wr(uint16_t(0x8000 + i), fg[i]);
    for (int i = 0; i < 0x100; ++i) m.cpu().wr(uint16_t(0xa000 + i), spr[i]);
    m.cpu().wr(0xe0f8, uint8_t(pos));
    m.cpu().wr(0xe0f9, uint8_t(pos >> 8));
    m.cpu().wr(0xe0fb, bg_on ? 1 : 0);
    m.cpu().wr(0xe0fa, banks ? 1 : 0);
    m.cpu().wr(0xe0f1, banks ? 1 : 0);
    CHECK_EQ(m.bg_pos(), pos);
    m.run_frame(nullptr, 0);
    int wrong = 0;
    uint16_t line[zaxxon::kScreenW];
    for (int y = 0; y < zaxxon::kScreenH; ++y) {
        m.render_line(y, line);
        for (int x = 0; x < zaxxon::kScreenW; ++x) {
            const int e = model_pixel(b, fg, spr, pos, bg_on, banks ? 0x80 : 0, banks ? 0x80 : 0, x, y);
            const uint16_t want = e < 0 ? 0 : m.pen(e);
            if (line[x] != want && ++wrong <= 3) std::printf("  pixel (%d, %d): %04x, want %04x\n", x, y, line[x], want);
        }
    }
    CHECK_EQ(wrong, 0);
    // Background tiles were decoded as drawn, each once while it stays cached.
    if (bg_on) CHECK(m.bg_misses() > 0);
}

void test_picture()
{
    scene(11, true, false, false);
    scene(12, true, true, false);
    scene(13, false, false, false);
    scene(14, true, false, true);
    scene(15, false, true, true);
    // Twice in a row: the second picture draws from the cache.
    static Fake b(16);
    static zaxxon::Machine m;
    m.init(b.roms(), 24000);
    m.cpu().wr(0xe0fb, 1);
    m.run_frame(nullptr, 0);
    uint16_t line[zaxxon::kScreenW];
    for (int y = 0; y < zaxxon::kScreenH; ++y) m.render_line(y, line);
    const uint32_t first = m.bg_misses();
    for (int y = 0; y < zaxxon::kScreenH; ++y) m.render_line(y, line);
    CHECK(m.bg_misses() - first < first);
}

// --- interrupts ------------------------------------------------------------------

// IM 1, enable on, EI, JR $; the handler at 0038 counts in 6000, turns the
// enable off and on again (acknowledging the IRQ) when `ack`.
void irq_program(Fake& b, bool enable, bool ack)
{
    const uint8_t main[] = {0x31, 0x00, 0x70, 0xed, 0x56, 0x3e, uint8_t(enable ? 1 : 0), 0x32, 0xf0, 0xe0,
                            0xfb, 0x18, 0xfe};
    std::memcpy(b.cpu.data(), main, sizeof main);
    std::vector<uint8_t> h = {0xf5, 0x3a, 0x00, 0x60, 0x3c, 0x32, 0x00, 0x60};
    if (ack) h.insert(h.end(), {0xaf, 0x32, 0xf0, 0xe0, 0x3c, 0x32, 0xf0, 0xe0});
    h.insert(h.end(), {0xf1, 0xfb, 0xc9});
    std::memcpy(b.cpu.data() + 0x38, h.data(), h.size());
}

void test_interrupts()
{
    {
        static zaxxon::Machine m;
        static Fake b(2);
        irq_program(b, true, true);
        m.init(b.roms(), 24000);
        for (int f = 0; f < 10; ++f) m.run_frame(nullptr, 0);
        CHECK_EQ(m.ram(0x6000), 10);          // one IRQ a frame
        CHECK(!m.cpu().irq_line());
    }
    {
        static zaxxon::Machine m;
        static Fake b(3);
        irq_program(b, false, true);
        m.init(b.roms(), 24000);
        for (int f = 0; f < 10; ++f) m.run_frame(nullptr, 0);
        CHECK_EQ(m.ram(0x6000), 0);           // enable off: none
    }
    {
        // Not acknowledged: the line stays up and the handler runs again
        // after each EI.
        static zaxxon::Machine m;
        static Fake b(4);
        irq_program(b, true, false);
        m.init(b.roms(), 24000);
        m.run_frame(nullptr, 0);
        CHECK(m.ram(0x6000) > 10);
        CHECK(m.cpu().irq_line());
    }
    CHECK_EQ(zaxxon::kCyclesPerFrame, 50688);
    CHECK(zaxxon::kCpuClock / double(zaxxon::kCyclesPerFrame) > 59.99);
    CHECK(zaxxon::kCpuClock / double(zaxxon::kCyclesPerFrame) < 60.01);
}

// --- inputs ------------------------------------------------------------------------

void test_inputs()
{
    using namespace zaxxon;
    static Fake b(5);
    static Machine m;
    m.init(b.roms(), 24000);
    m.set_inputs(kUp | kFire);
    CHECK_EQ(m.cpu().rd(0xc000), 0x18);       // active high: up = bit 3, fire = bit 4
    m.set_inputs(kRight | kDown);
    CHECK_EQ(m.cpu().rd(0xc000), 0x05);
    CHECK_EQ(m.cpu().rd(0xd8fc), 0x05);       // mirrored
    m.set_inputs(kLeft);
    CHECK_EQ(m.cpu().rd(0xc000), 0x02);
    CHECK_EQ(m.cpu().rd(0xc002), kDipDefault);
    CHECK_EQ(m.cpu().rd(0xc003), 0x33);
    m.set_dips(0x00);
    CHECK_EQ(m.cpu().rd(0xc002), 0x00);
    m.set_inputs(kStart1);
    CHECK_EQ(m.cpu().rd(0xc100), 0x04);
    m.set_inputs(kStart2);
    CHECK_EQ(m.cpu().rd(0xc1ff), 0x08);
    m.set_inputs(0);

    // Coins: latched only while the slot is enabled, cleared by turning it off.
    m.set_inputs(kCoin);
    m.set_inputs(0);
    CHECK_EQ(m.cpu().rd(0xc100) & 0xe0, 0);
    m.cpu().wr(0xc000, 1);                    // coin A enable
    m.cpu().wr(0xc001, 1);                    // coin B enable
    m.cpu().wr(0xc002, 1);                    // service enable
    CHECK_EQ(m.latch1() & 7, 7);
    m.set_inputs(kCoin);
    CHECK_EQ(m.cpu().rd(0xc100) & 0xe0, 0x20);
    m.set_inputs(kCoin);                      // held: no second pulse
    m.set_inputs(0);
    CHECK_EQ(m.cpu().rd(0xc100) & 0xe0, 0x20);    // stays latched
    m.set_inputs(kCoin2 | kService);
    CHECK_EQ(m.cpu().rd(0xc100) & 0xe0, 0xe0);
    m.cpu().wr(0xc000, 0);
    CHECK_EQ(m.cpu().rd(0xc100) & 0xe0, 0xc0);
    m.cpu().wr(0xd8f9, 0);                    // mirror of C001
    m.cpu().wr(0xc002, 0);
    CHECK_EQ(m.cpu().rd(0xc100) & 0xe0, 0);
    m.cpu().wr(0xc006, 1);                    // flip screen bit
    CHECK_EQ(m.latch1() & 0x40, 0x40);
}

// --- the 8255 and the sound board ------------------------------------------------------

void test_sound_wiring()
{
    using zaxxon_sound::Sound;
    static Fake b(6);
    static zaxxon::Machine m;
    m.init(b.roms(), 24000);
    const zaxxon_sound::Board& s = m.sound();
    z80::Cpu& c = m.cpu();
    // The game's own power-on: mode 0 (outputs cleared), then FF to all
    // three ports through the FF3C mirror. Nothing starts.
    c.wr(0xff3f, 0x80);
    c.wr(0xff3c, 0xff);
    c.wr(0xff3d, 0xff);
    c.wr(0xff3e, 0xff);
    for (int k = 0; k < zaxxon_sound::kSoundCount; ++k) {
        CHECK_EQ(s.triggers(Sound(k)), 0);
        CHECK(!s.active(Sound(k)));
    }
    // One shots start on a falling edge, once.
    c.wr(0xe03d, 0xef);                       // B4 small explosion
    CHECK_EQ(s.triggers(zaxxon_sound::kSmallExp), 1);
    c.wr(0xe03d, 0xef);
    CHECK_EQ(s.triggers(zaxxon_sound::kSmallExp), 1);
    c.wr(0xe03d, 0x5f);                       // B5 big explosion, B7 cannon
    CHECK_EQ(s.triggers(zaxxon_sound::kBigExp), 1);
    CHECK_EQ(s.triggers(zaxxon_sound::kCannon), 1);
    c.wr(0xe03d, 0xff);
    c.wr(0xe03d, 0xdf);                       // the big explosion is still sounding: not again
    CHECK_EQ(s.triggers(zaxxon_sound::kBigExp), 1);
    c.wr(0xe03c, 0xdf);                       // A5 base missile
    CHECK_EQ(s.triggers(zaxxon_sound::kBaseMissile), 1);
    CHECK(s.active(zaxxon_sound::kBaseMissile));
    // Held sounds: on while low.
    c.wr(0xe03c, 0x2e);                       // A0 = 0, A1 = 1: volume 2; A4 homing, A6 laser, A7 battleship
    CHECK(!s.active(zaxxon_sound::kEngineA));
    CHECK(s.active(zaxxon_sound::kHoming));
    CHECK(s.active(zaxxon_sound::kLaser));
    CHECK(s.active(zaxxon_sound::kBattleship));
    CHECK_EQ(s.engine_volume(), 2);
    c.wr(0xe03c, 0xf3);                       // A2 / A3 engines
    CHECK(s.active(zaxxon_sound::kEngineA));
    CHECK(s.active(zaxxon_sound::kEngineB));
    CHECK(!s.active(zaxxon_sound::kHoming));
    // Port C through its bit set / reset: C0 shot, C2 alarm 2, C3 alarm 3.
    c.wr(0xe03f, 0x00);                       // reset C0
    CHECK_EQ(s.triggers(zaxxon_sound::kShot), 1);
    c.wr(0xe03f, 0x04);                       // reset C2
    CHECK_EQ(s.triggers(zaxxon_sound::kAlarm2), 1);
    c.wr(0xe03f, 0x07);                       // set C3 (it was 1): nothing
    c.wr(0xe03f, 0x06);                       // reset C3
    CHECK_EQ(s.triggers(zaxxon_sound::kAlarm3), 1);
    c.wr(0xe03e, 0xff);
    c.wr(0xe03e, 0xf7);                       // still sounding: not again
    CHECK_EQ(s.triggers(zaxxon_sound::kAlarm3), 1);
    CHECK_EQ(c.rd(0xe03e), 0xf7);
    // Not the 8255: E0F0-E0FB are the second latch.
    c.wr(0xe0f0, 1);
    CHECK_EQ(m.latch2() & 1, 1);
}

// Peak of n samples of a sound board.
int peak(zaxxon_sound::Board& s, int n, int* last = nullptr)
{
    std::vector<int16_t> v(static_cast<size_t>(n));
    s.render(v.data(), n);
    int p = 0;
    for (int16_t x : v) p = x > p ? x : (-x > p ? -x : p);
    if (last) *last = v.back();
    return p;
}

void test_sound_models()
{
    using namespace zaxxon_sound;
    static zaxxon_sound::Board s;
    s.init(24000);
    CHECK_EQ(peak(s, 24000), 0);              // silent until something starts
    // Every sound at once: the soft knee keeps the peak under ~21,500.
    s.write_a(0x00);
    s.write_b(0x00);
    s.write_c(0x00);
    const int all = peak(s, 24000);
    CHECK(all > 16000);
    CHECK(all < 21500);
    // Each one alone sounds, and none clips.
    struct One { uint8_t a, b, c; };
    const One ones[] = {{0xfb, 0xff, 0xff}, {0xf7, 0xff, 0xff}, {0xef, 0xff, 0xff}, {0xdf, 0xff, 0xff},
                        {0xbf, 0xff, 0xff}, {0x7f, 0xff, 0xff}, {0xff, 0xef, 0xff}, {0xff, 0xdf, 0xff},
                        {0xff, 0x7f, 0xff}, {0xff, 0xff, 0xfe}, {0xff, 0xff, 0xfb}, {0xff, 0xff, 0xf7}};
    for (const One& o : ones) {
        s.init(24000);
        s.write_a(o.a);
        s.write_b(o.b);
        s.write_c(o.c);
        const int p = peak(s, 12000);
        CHECK(p > 1500);
        CHECK(p < 21500);
    }
    // One shots end; held sounds stop when let go; the output settles to 0.
    s.init(24000);
    s.write_b(0xef);
    s.write_c(0xfe);
    peak(s, 48000);
    CHECK(!s.active(kSmallExp));
    CHECK(!s.active(kShot));
    s.write_a(0xef);
    peak(s, 2400);
    s.write_a(0xff);
    int last = 1;
    peak(s, 48000, &last);
    CHECK_EQ(last, 0);
    // The engine's level: bits 0-1 (louder with the value).
    s.init(24000);
    s.write_a(0xf8);
    peak(s, 4800);
    const int quiet = peak(s, 24000);
    s.write_a(0xfb);
    peak(s, 4800);
    const int loud = peak(s, 24000);
    CHECK(loud > quiet);
}

// --- idle skipping ---------------------------------------------------------------

void test_idle_skip()
{
    // A program that isn't set 1's: the main loop's check fails, nothing is
    // skipped, and the frames run as they would.
    static Fake b(7);
    irq_program(b, true, true);
    static zaxxon::Machine m;
    m.init(b.roms(), 24000);
    for (int f = 0; f < 20; ++f) m.run_frame(nullptr, 0);
    CHECK_EQ(m.idle_cycles(), 0);
    CHECK_EQ(m.ram(0x6000), 20);
}

} // namespace

int main()
{
    test_palette();
    test_picture();
    test_interrupts();
    test_inputs();
    test_sound_wiring();
    test_sound_models();
    test_idle_skip();
    std::printf("Zaxxon tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
