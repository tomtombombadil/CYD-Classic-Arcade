// Host tests (no ROMs) for the 1942 machine: the main board on hand-made
// Z80 programs (ROM banks, inputs, the timing PROM's RST 08 / RST 10 held
// until taken, the sound command), the sound board (its IRQs, the latches,
// both AYs, held in reset by C804 bit 4), the picture against a plain
// per-pixel model of the board on synthetic graphics (scrolled, flipped
// background tiles and their palette bank, sprites 16 to 256 lines tall
// with their slot / line rules, characters, the turned monitor), the
// palette's resistor weights, and that the idle skips leave both CPUs and
// the RAM exactly as runs without them.
//
//   c1942_tests
#include "../../emu/machines/c1942.h"

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

struct Rng {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

// Synthetic ROMs. The timing PROM as the board's (sb-1: RST 08 at 109,
// RST 10 at 240, sound IRQs at 44 / 109 / 175 / 240).
struct Fake {
    std::vector<uint8_t> main0 = std::vector<uint8_t>(0x4000, 0), main1 = std::vector<uint8_t>(0x4000, 0);
    std::vector<uint8_t> bank0 = std::vector<uint8_t>(0x4000, 0xb0), bank1 = std::vector<uint8_t>(0x2000, 0xb1);
    std::vector<uint8_t> bank2 = std::vector<uint8_t>(0x4000, 0xb2);
    std::vector<uint8_t> sound = std::vector<uint8_t>(0x4000, 0);
    std::vector<uint8_t> chars = std::vector<uint8_t>(0x2000), tiles = std::vector<uint8_t>(0xc000);
    std::vector<uint8_t> sprites = std::vector<uint8_t>(0x10000), proms = std::vector<uint8_t>(c1942::kPromSize);

    explicit Fake(uint32_t seed)
    {
        Rng r{seed};
        for (uint8_t& b : chars) b = uint8_t(r.next() & r.next());
        for (uint8_t& b : tiles) b = uint8_t(r.next());
        for (uint8_t& b : sprites) b = uint8_t(r.next() | r.next());   // mostly 1s: pen 15 is clear
        for (int i = 0; i < 0x100; ++i) {
            proms[c1942::kPromChars + i] = uint8_t(r.next() & 15);
            proms[c1942::kPromTiles + i] = uint8_t(r.next() & 15);
            proms[c1942::kPromSprites + i] = uint8_t(r.next() & 15);
            proms[c1942::kPromRed + i] = uint8_t(i & 15);
            proms[c1942::kPromGreen + i] = uint8_t(i >> 4);
            proms[c1942::kPromBlue + i] = uint8_t((i * 7) & 15);
        }
        proms[c1942::kPromIrq + 44] = 0x06;
        proms[c1942::kPromIrq + 109] = 0x0d;
        proms[c1942::kPromIrq + 175] = 0x05;
        proms[c1942::kPromIrq + 240] = 0x0e;
        bank0[0] = 0x10;
        bank1[0] = 0x11;
        bank2[0] = 0x12;
        // The sound board: IM 1, its count cleared, the idle loop at 0064 (EI /
        // JR to it); the
        // IRQ copies command 0 to AY 1 register 0, command 1 to AY 2
        // register 1, and counts its calls at 4000.
        const uint8_t snd[] = {0xf3, 0x31, 0x00, 0x48, 0xed, 0x56, 0xaf, 0x32, 0x00, 0x40, 0xc3, 0x64, 0x00};
        std::memcpy(sound.data(), snd, sizeof snd);
        const uint8_t irq[] = {
            0xf5,                       // PUSH AF
            0x3e, 0x00, 0x32, 0x00, 0x80,   // LD A,0 ; LD (8000),A
            0x3a, 0x00, 0x60, 0x32, 0x01, 0x80,   // LD A,(6000) ; LD (8001),A
            0x3e, 0x01, 0x32, 0x00, 0xc0,   // LD A,1 ; LD (C000),A
            0x3a, 0x01, 0x60, 0x32, 0x01, 0xc0,   // LD A,(6001) ; LD (C001),A
            0x3a, 0x00, 0x40, 0x3c, 0x32, 0x00, 0x40,   // LD A,(4000) ; INC A ; LD (4000),A
            0xf1, 0xfb, 0xed, 0x4d,     // POP AF ; EI ; RETI
        };
        std::memcpy(sound.data() + 0x38, irq, sizeof irq);
        sound[0x64] = 0xfb;
        sound[0x65] = 0x18;
        sound[0x66] = 0xfd;
    }
    c1942::Roms roms() const
    {
        return {main0.data(), main1.data(), {bank0.data(), bank1.data(), bank2.data()}, sound.data(),
                chars.data(), tiles.data(), sprites.data(), proms.data()};
    }
    // RST 08 counts at E000, RST 10 at E001; the program from 0040.
    void program(const std::vector<uint8_t>& code)
    {
        const uint8_t start[] = {0xf3, 0xc3, 0x40, 0x00};
        std::memcpy(main0.data(), start, sizeof start);
        const uint8_t v8[] = {0xc3, 0x00, 0x01}, v10[] = {0xc3, 0x20, 0x01};
        std::memcpy(main0.data() + 0x08, v8, 3);
        std::memcpy(main0.data() + 0x10, v10, 3);
        const uint8_t h8[] = {0xf5, 0x3a, 0x00, 0xe0, 0x3c, 0x32, 0x00, 0xe0, 0xf1, 0xfb, 0xc9};
        const uint8_t h10[] = {0xf5, 0x3a, 0x01, 0xe0, 0x3c, 0x32, 0x01, 0xe0, 0xf1, 0xfb, 0xc9};
        std::memcpy(main0.data() + 0x100, h8, sizeof h8);
        std::memcpy(main0.data() + 0x120, h10, sizeof h10);
        std::memcpy(main0.data() + 0x40, code.data(), code.size());
    }
};

std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
std::vector<uint8_t> ld_a(uint8_t v) { return {0x3e, v}; }
std::vector<uint8_t> st(uint16_t a) { return {0x32, uint8_t(a), uint8_t(a >> 8)}; }
std::vector<uint8_t> ld(uint16_t a) { return {0x3a, uint8_t(a), uint8_t(a >> 8)}; }
const std::vector<uint8_t> kSetup = {0x31, 0x00, 0xf0, 0xed, 0x46};   // LD SP,F000 ; IM 0

// --- main board --------------------------------------------------------------------

void test_board()
{
    static Fake f(1);
    f.program(kSetup + ld_a(0) + st(0xc806) + ld(0x8000) + st(0xe010) + ld_a(1) + st(0xc806) + ld(0x8000) +
              st(0xe011) + ld(0xa000) + st(0xe012) + ld_a(2) + st(0xc806) + ld(0x8000) + st(0xe013) +
              ld_a(3) + st(0xc806) + ld(0x8000) + st(0xe014) + ld(0xc000) + st(0xe020) + ld(0xc001) +
              st(0xe021) + ld(0xc002) + st(0xe022) + ld(0xc003) + st(0xe023) + ld(0xc004) + st(0xe024) +
              ld_a(0x5a) + st(0xc800) + ld_a(0x0c) + st(0xc801) + std::vector<uint8_t>{0xfb, 0x18, 0xfe});
    static c1942::Machine m;
    m.init(f.roms(), 24000);
    m.set_dips(c1942::kDipLives5 | c1942::kDipBonus30k80k);
    m.set_inputs(c1942::kCoin | c1942::kStart1 | c1942::kFire | c1942::kLeft | c1942::kUp);
    for (int i = 0; i < 10; ++i) m.run_frame(nullptr, 0);
    const uint8_t* ram = m.ram();
    CHECK_EQ(ram[0x10], 0x10);              // bank 0 = M5
    CHECK_EQ(ram[0x11], 0x11);              // bank 1 = M6, 8 KB
    CHECK_EQ(ram[0x12], 0xff);              // its upper half empty
    CHECK_EQ(ram[0x13], 0x12);              // bank 2 = M7
    CHECK_EQ(ram[0x14], 0xff);              // bank 3 empty
    CHECK_EQ(ram[0x20], 0xff & ~0x80 & ~0x01);  // coin 1, start 1 (active low)
    CHECK_EQ(ram[0x21], 0xff & ~0x10 & ~0x02 & ~0x08);  // fire, left, up
    CHECK_EQ(ram[0x22], 0xff);              // P2 (cocktail)
    CHECK_EQ(ram[0x23], 0x17);              // lives 5, 30K / 80K, upright, 1 coin 1 credit
    CHECK_EQ(ram[0x24], 0xff);
    // RST 08 and RST 10 once a frame each (the first frame's EI came late)
    CHECK(ram[0x00] >= 9 && ram[0x00] <= 10);
    CHECK(ram[0x01] >= 9 && ram[0x01] <= 10);
    CHECK_EQ(m.main_irqs(), 20);
    // The sound board copied the commands into the AYs four times a frame
    CHECK_EQ(m.ay(0).reg(0), 0x5a);
    CHECK_EQ(m.ay(1).reg(1), 0x0c);
    CHECK_EQ(m.sound_irqs(), 40);
    CHECK_EQ(m.sound_cpu().rd(0x4000), 40);
}

// A raised IRQ waits for EI and is then taken once: with interrupts off for
// over a frame, RST 08 and RST 10 come and the later one's RST is taken.
void test_held_irq()
{
    static Fake f(2);
    // LD BC,n ; loop: DEC BC / LD A,B / OR C / JR NZ  (26 cycles a turn)
    f.program(kSetup + std::vector<uint8_t>{0x01, 0x00, 0x0a, 0x0b, 0x78, 0xb1, 0x20, 0xfb, 0xfb, 0x18, 0xfe});
    static c1942::Machine m;
    m.init(f.roms(), 24000);
    m.run_frame(nullptr, 0);                // 0x0a00 x 26 = 66,560 cycles: past one frame
    CHECK_EQ(m.ram()[0x00], 0);
    CHECK_EQ(m.ram()[0x01], 0);
    m.run_frame(nullptr, 0);                // frame 1's RST 08 was overtaken by its RST 10,
    CHECK_EQ(m.ram()[0x00], 1);             // taken at the EI (line ~85); then frame 2's own
    CHECK_EQ(m.ram()[0x01], 2);
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.ram()[0x00], 2);
    CHECK_EQ(m.ram()[0x01], 3);
}

// C804 bit 4 holds the sound CPU in reset; it starts again from 0.
void test_sound_reset()
{
    static Fake f(3);
    f.program(kSetup + std::vector<uint8_t>{0xfb} + ld(0xe100) + std::vector<uint8_t>{0xfe, 0x01, 0x20, 0xf9} +
              ld_a(0x10) + st(0xc804) + ld(0xe101) + std::vector<uint8_t>{0xfe, 0x01, 0x20, 0xf9} + ld_a(0) +
              st(0xc804) + std::vector<uint8_t>{0x18, 0xfe});
    static c1942::Machine m;
    m.init(f.roms(), 24000);
    for (int i = 0; i < 3; ++i) m.run_frame(nullptr, 0);
    const int before = m.sound_cpu().rd(0x4000);
    CHECK(before >= 11);
    m.ram()[0x100] = 1;                     // the program puts it in reset
    for (int i = 0; i < 3; ++i) m.run_frame(nullptr, 0);
    CHECK(m.sound_in_reset());
    const uint32_t irqs = m.sound_irqs();
    m.run_frame(nullptr, 0);
    CHECK_EQ(m.sound_irqs(), irqs);         // no IRQs while held
    m.ram()[0x101] = 1;                     // and lets it go
    for (int i = 0; i < 2; ++i) m.run_frame(nullptr, 0);
    CHECK(!m.sound_in_reset());
    CHECK(m.sound_irqs() > irqs);
    CHECK(m.sound_cpu().rd(0x4000) < before);    // it started again: its program cleared the count
}

// --- palette and picture -----------------------------------------------------------

uint16_t rgb(int nr, int ng, int nb)
{
    auto gun = [](int n) {
        const double g[4] = {1 / 2200.0, 1 / 1000.0, 1 / 470.0, 1 / 220.0};
        double v = 0, s = 0;
        for (int b = 0; b < 4; ++b) {
            s += g[b];
            if (n & (1 << b)) v += g[b];
        }
        return int(v / s * 255.0 + 0.5);
    };
    return uint16_t(((gun(nr) >> 3) << 11) | ((gun(ng) >> 2) << 5) | (gun(nb) >> 3));
}

// The board, a pixel at a time (x 0-255 along its line, y the line).
uint16_t model_pixel(const Fake& f, const c1942::VideoState& vs, int x, int y)
{
    auto pal = [&](int i) {
        return rgb(f.proms[c1942::kPromRed + i] & 15, f.proms[c1942::kPromGreen + i] & 15,
                   f.proms[c1942::kPromBlue + i] & 15);
    };
    // characters on top, pen 0 clear
    {
        const int idx = (y >> 3) * 32 + (x >> 3);
        const int attr = vs.fg[0x400 + idx];
        const int code = vs.fg[idx] | ((attr & 0x80) << 1);
        const int px = x & 7, py = y & 7;
        const uint8_t b = f.chars[code * 16 + py * 2 + (px >> 2)];
        const int v = ((b >> (7 - (px & 3))) & 1) | (((b >> (3 - (px & 3))) & 1) << 1);
        if (v) return pal(0x80 | f.proms[c1942::kPromChars + (attr & 0x3f) * 4 + v]);
    }
    // sprites: the lowest slot that draws here wins
    for (int i = 0; i < 32; ++i) {
        if (i >= 16 && i < 24 && y > 128) continue;
        if (i >= 24 && y < 129) continue;
        const uint8_t* o = vs.spr + i * 4;
        const int sx = o[3] - ((o[1] & 0x10) ? 256 : 0), sy = o[2];
        const int vlen = o[1] >> 6;
        const int height = vlen == 3 ? 256 : 16 << vlen;
        if (x < sx || x >= sx + 16 || y < sy || y >= sy + height) continue;
        const int rows = height / 16;
        const int code = ((o[0] & 0x7f) | ((o[1] & 0x20) << 2) | ((o[0] & 0x80) << 1)) / rows * rows + (y - sy) / 16;
        const int px = x - sx, py = (y - sy) & 15;
        // planes: half 1 bit offsets +4 / +0, half 0 +4 / +0 (MSB first)
        const int byte = code * 64 + (px >= 8 ? 32 : 0) + py * 2 + ((px >> 2) & 1);
        const int k = px & 3;
        const uint8_t lo = f.sprites[byte], hi = f.sprites[0x8000 + byte];
        const int v = (((hi >> (3 - k)) & 1) << 3) | (((hi >> (7 - k)) & 1) << 2) | (((lo >> (3 - k)) & 1) << 1) |
                      ((lo >> (7 - k)) & 1);
        if (v != 15) return pal(0x40 | f.proms[c1942::kPromSprites + (o[1] & 15) * 16 + v]);
    }
    // background
    const int mx = (x + vs.scroll) & 511;
    const int ti = (mx >> 4) * 32 + (y >> 4);
    const int attr = vs.bg[ti + 16];
    const int code = vs.bg[ti] | ((attr & 0x80) << 1);
    int px = mx & 15, py = y & 15;
    if (attr & 0x20) px = 15 - px;
    if (attr & 0x40) py = 15 - py;
    int v = 0;
    for (int p = 0; p < 3; ++p) {
        const uint8_t b = f.tiles[p * 0x4000 + code * 32 + (px >= 8 ? 16 : 0) + py];
        v |= ((b >> (7 - (px & 7))) & 1) << (2 - p);
    }
    return pal((vs.palette_bank << 4) | f.proms[c1942::kPromTiles + (attr & 0x1f) * 8 + v]);
}

void test_picture()
{
    static Fake f(4);
    static c1942::Machine m;
    m.init(f.roms(), 24000);
    CHECK_EQ(m.pen(0x0f), rgb(15, 0, 15 * 7 & 15));
    CHECK_EQ(m.pen(0xff), rgb(15, 15, 15 * 7 & 15));
    CHECK_EQ(m.pen(0x00), 0);
    // Full red through all four resistors = 255; 2.2k alone ~14
    CHECK_EQ(m.pen(0x0f) >> 11, 31);
    CHECK_EQ(m.pen(0x01) >> 11, 14 >> 3);
    Rng r{77};
    static c1942::VideoState vs;
    std::vector<uint16_t> line(c1942::kScreenW);
    for (int round = 0; round < 6; ++round) {
        for (uint8_t& b : vs.fg) b = uint8_t(r.next());
        // mostly blank characters (code 0 cleared below), some drawn
        for (int i = 0; i < 0x400; ++i)
            if (r.next() % 4) vs.fg[i] = 0, vs.fg[0x400 + i] &= 0x7f;
        for (uint8_t& b : vs.bg) b = uint8_t(r.next());
        // repeated tiles down some columns (the strip's copy path)
        for (int c = 0; c < 32; c += 3)
            for (int q = 2; q < 15; q += 2) vs.bg[c * 32 + q] = vs.bg[c * 32 + 1], vs.bg[c * 32 + 16 + q] = vs.bg[c * 32 + 17];
        for (int i = 0; i < 32; ++i) {
            uint8_t* o = vs.spr + i * 4;
            o[0] = uint8_t(r.next());
            o[1] = uint8_t(r.next());
            o[2] = uint8_t(r.next() % 260);
            o[3] = uint8_t(r.next());
        }
        vs.scroll = uint16_t(r.next() & 511);
        vs.palette_bank = uint8_t(round & 3);
        int bad = 0;
        for (int dy = 0; dy < c1942::kScreenH; ++dy) {
            m.render_line(vs, dy, line.data());
            for (int dx = 0; dx < c1942::kScreenW; ++dx)
                if (line[dx] != model_pixel(f, vs, 255 - dy, dx + 16)) ++bad;
        }
        CHECK_EQ(bad, 0);
    }
}

// --- idle skipping -------------------------------------------------------------------

// The task loop at 0087 as in the game (all slots below 20 so it waits),
// RST handlers that count; a task slot set to 20 makes it leave at 009C.
void put_task_loop(Fake& f)
{
    const uint8_t loop[] = {0xfb, 0x21, 0xc0, 0xef, 0x01, 0x00, 0x10, 0x11, 0x04, 0x00, 0x7e, 0xfe,
                            0x20, 0x30, 0x06, 0x0c, 0x19, 0x10, 0xf7, 0x18, 0xeb};
    std::memcpy(f.main0.data() + 0x87, loop, sizeof loop);
    f.main0[0x9c] = 0x18;                   // JR $ when a task is found
    f.main0[0x9d] = 0xfe;
}

void run_pair(Fake& f, int frames, int slot_at, uint64_t* skipped)
{
    static c1942::Machine a, b;
    a.init(f.roms(), 24000);
    b.init(f.roms(), 24000);
    b.set_idle_skip(false);
    static int16_t sa[1024], sb[1024];
    bool same = true;
    for (int i = 0; i < frames; ++i) {
        if (i == slot_at) a.ram()[0xfc0 + 4 * 7] = b.ram()[0xfc0 + 4 * 7] = 0x20;
        const int na = a.run_frame(sa, 1024), nb = b.run_frame(sb, 1024);
        z80::Cpu& x = a.cpu();
        z80::Cpu& y = b.cpu();
        same = same && na == nb && std::memcmp(sa, sb, size_t(na) * 2) == 0 && x.pc.w == y.pc.w &&
               x.af.w == y.af.w && x.bc.w == y.bc.w && x.de.w == y.de.w && x.hl.w == y.hl.w && x.sp.w == y.sp.w &&
               x.r_value() == y.r_value() && x.iff1 == y.iff1 &&
               std::memcmp(a.ram(), b.ram(), 0x1000) == 0 && a.sound_cpu().pc.w == b.sound_cpu().pc.w &&
               a.sound_cpu().r_value() == b.sound_cpu().r_value() && a.sound_cpu().af.w == b.sound_cpu().af.w &&
               a.sound_cpu().sp.w == b.sound_cpu().sp.w;
    }
    CHECK(same);
    *skipped = a.skipped_cycles();
    CHECK_EQ(b.skipped_cycles(), 0);
    if (slot_at >= 0) CHECK_EQ(a.cpu().pc.w, 0x9c);
}

void test_idle_skip()
{
    static Fake f(5);
    put_task_loop(f);
    // the stack below the task slots, as the game has it
    f.program(std::vector<uint8_t>{0x31, 0xbe, 0xef, 0xed, 0x46} + ld_a(0x33) + st(0xc800) +
              std::vector<uint8_t>{0xc3, 0x87, 0x00});
    uint64_t skipped = 0;
    run_pair(f, 40, -1, &skipped);
    CHECK(skipped > 40ull * 80000);         // most of both CPUs' time
    run_pair(f, 40, 25, &skipped);          // a task turns up: the loop is left the same way
    CHECK(skipped > 20ull * 80000);
}

} // namespace

int main()
{
    test_board();
    test_held_irq();
    test_sound_reset();
    test_picture();
    test_idle_skip();
    std::printf("1942 tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
