// Unit tests for the Missile Command board, on hand-assembled 6502
// programs - no ROMs: pixels through MADSEL (the two bit planes, the third
// bit of lines 224-255, reads, a (zp),Y access left alone, MADSEL held off
// while /IRQH is active), the colour RAM and the picture, the IRQ from 32V
// and its acknowledge, VBLANK, the inputs, the trackball counters (stick
// and d-pad), the watchdog, the snapshot, and that idle skipping leaves a
// timing-sensitive program exactly as it runs without it.
//
//   missile_tests
#include "../../emu/machines/missile.h"

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

// The program ROM (5000-7FFF) with the reset and IRQ vectors.
struct Rom {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x3000, 0xea);
    void at(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t v : bytes) b[addr++ - 0x5000] = v;
    }
    void vectors(uint16_t reset, uint16_t irq)
    {
        at(0x7ffc, {uint8_t(reset), uint8_t(reset >> 8), uint8_t(irq), uint8_t(irq >> 8)});
    }
};

void run(missile::Machine& m, int frames)
{
    for (int i = 0; i < frames; ++i) m.run_frame(nullptr, 0);
}

// Pixels through MADSEL, the colour RAM, the picture.
void test_pixels()
{
    Rom r;
    r.at(0x5000, {0x78, 0x8d, 0x00, 0x4d,                   // SEI; acknowledge line 0's IRQ
                  0xa2, 0xff, 0x9a, 0xd8,                   // LDX #$FF; TXS; CLD
                  0xa2, 0x07,                               // LDX #7
                  0x8a, 0x0a, 0x49, 0x0e, 0x9d, 0x00, 0x4b,  // pen X = ~(X << 1): bits 3-1 low = R G B on
                  0xca, 0x10, 0xf6,                         // DEX; BPL
                  0xa9, 0x05, 0x85, 0x10, 0xa9, 0x64, 0x85, 0x11,  // ($10) = $6405: X 5, Y 100
                  0xa2, 0x00,                               // LDX #0
                  0xa9, 0xc0, 0x81, 0x10,                   // colour 6 (bits 7, 6)
                  0xa9, 0x09, 0x85, 0x10, 0xa9, 0xf0, 0x85, 0x11,  // ($10) = $F009: X 9, Y 240
                  0xa9, 0xa0, 0x81, 0x10,                   // colour 5 (bits 7, 5)
                  0xa1, 0x10, 0x8d, 0x00, 0x07,             // read it back -> 0700
                  0xa9, 0x05, 0x85, 0x10, 0xa9, 0x64, 0x85, 0x11,
                  0xa1, 0x10, 0x8d, 0x01, 0x07,             // (5, 100) -> 0701
                  0xe6, 0x10, 0xa1, 0x10, 0x8d, 0x02, 0x07,  // (6, 100), empty -> 0702
                  0xa9, 0x00, 0x85, 0x12, 0xa9, 0x03, 0x85, 0x13,  // ($12) = $0300
                  0xa0, 0x00, 0xa9, 0x5a, 0x91, 0x12,       // STA ($12),Y: plain RAM
                  0xa9, 0x11, 0x85, 0x10, 0xa9, 0x03, 0x85, 0x11,  // ($10) = $0311: a pixel (X 17, Y 3)
                  0xa9, 0xff, 0x81, 0x10,
                  0x8d, 0x00, 0x4c, 0x4c, 0x61, 0x50});     // STA $4C00; JMP *-3
    r.vectors(0x5000, 0x5061);
    static missile::Machine m;
    m.init(r.b.data(), 24000);
    run(m, 1);
    const uint8_t* v = m.vram();
    CHECK_EQ(m.madsel_count(), 6);
    CHECK_EQ(v[0x6405 >> 2], 0x22);     // pixel 1 of the byte: bits 1 and 5
    CHECK_EQ(v[0xf009 >> 2], 0x20);     // colour 5: high plane only ...
    CHECK_EQ(v[0x203], 0x02);           // ... and the third bit: Y 240 -> 0203, bit 1
    CHECK_EQ(v[0x700], 0xbf);           // read back: bits 7 and 5 set, 6 clear
    CHECK_EQ(v[0x701], 0xff);           // above line 224 the third bit reads 1
    CHECK_EQ(v[0x702], 0x3f);
    CHECK_EQ(v[0x300], 0x5a);           // (zp),Y is no MADSEL opcode
    CHECK_EQ(v[0x0311 >> 2], 0x22);     // a MADSEL write to a RAM address is a pixel
    CHECK_EQ(v[0x311], 0x00);
    CHECK_EQ(m.pen(0), 0x0000);
    CHECK_EQ(m.pen(6), 0xffe0);         // red + green
    CHECK_EQ(m.pen(5), 0xf81f);         // red + blue
    CHECK_EQ(m.pen(7), 0xffff);
    std::vector<uint16_t> line(missile::kScreenW);
    m.render_line(100 - missile::kFirstLine, line.data());
    CHECK_EQ(line[5], 0xffe0);
    CHECK_EQ(line[4], 0x0000);
    CHECK_EQ(line[6], 0x0000);
    m.render_line(240 - missile::kFirstLine, line.data());
    CHECK_EQ(line[9], 0xf81f);
    CHECK_EQ(line[8], 0x0000);
    CHECK_EQ(line[17], 0x0000);
    // Where the third bits live: lines 224-231 / 240-247 at 0200-03FF,
    // 232-239 / 248-255 at 0400-05FF; odd bytes for lines 240 and up.
    CHECK_EQ(missile::Machine::bit3_addr(0xe000), 0x200);
    CHECK_EQ(missile::Machine::bit3_addr(0xe800), 0x400);
    CHECK_EQ(missile::Machine::bit3_addr(0xff08), 0x5c3);
    CHECK_EQ(m.watchdog_resets(), 0);

    // The snapshot keeps the picture as it was.
    CHECK(m.snapshot());
    m.vram()[0x6405 >> 2] = 0;
    m.render_snapshot_line(100 - missile::kFirstLine, line.data());
    CHECK_EQ(line[5], 0xffe0);
    m.render_line(100 - missile::kFirstLine, line.data());
    CHECK_EQ(line[5], 0x0000);
}

// MADSEL is held off for an opcode fetched while /IRQH (the IRQ pin as
// latched at the opcode before) is active: with I set, the IRQ asserted at
// line 0 stays pending, and STA ($10,X) writes plain RAM.
void test_madsel_irqh()
{
    Rom r;
    r.at(0x5000, {0x78, 0xa2, 0xff, 0x9a,                   // SEI ...
                  0xa9, 0x40, 0x85, 0x10, 0xa9, 0x03, 0x85, 0x11,  // ($10) = $0340
                  0xa2, 0x00, 0xa9, 0x77, 0x81, 0x10,       // STA ($10,X)
                  0x8d, 0x00, 0x4c, 0x4c, 0x12, 0x50});
    r.vectors(0x5000, 0x5012);
    static missile::Machine m;
    m.init(r.b.data(), 24000);
    run(m, 1);
    CHECK_EQ(m.madsel_count(), 0);
    CHECK_EQ(m.vram()[0x340], 0x77);
}

// Four IRQs a frame when acknowledged; without the acknowledge the line
// stays up for 32 lines and the handler runs again and again.
void test_irq()
{
    Rom r;
    r.at(0x5000, {0x78, 0xa2, 0xff, 0x9a, 0xa9, 0x00, 0x85, 0x20, 0x85, 0x21, 0x85, 0x22,
                  0x58,                                     // CLI
                  0x8d, 0x00, 0x4c, 0x4c, 0x0d, 0x50});     // STA $4C00; JMP *-3
    r.at(0x5100, {0x48, 0xe6, 0x20, 0x8d, 0x00, 0x4d, 0x68, 0x40});    // INC $20; ack; RTI
    r.at(0x5200, {0x48, 0xe6, 0x21, 0xd0, 0x02, 0xe6, 0x22, 0x68, 0x40});  // 16-bit count, no ack
    r.vectors(0x5000, 0x5100);
    static missile::Machine m;
    m.init(r.b.data(), 24000);
    run(m, 10);
    CHECK_EQ(m.vram()[0x20], 40);

    r.vectors(0x5000, 0x5200);
    static missile::Machine n;
    n.init(r.b.data(), 24000);
    run(n, 1);
    const int count = n.vram()[0x21] | n.vram()[0x22] << 8;
    CHECK(count > 100);         // ~4 x 2560 cycles / ~30 a round
    CHECK_EQ(n.watchdog_resets(), 0);
}

// VBLANK (4900 bit 7 before line 24), the buttons, the trackball counters.
void test_inputs()
{
    Rom r;
    r.at(0x5000, {0x78, 0xa2, 0xff, 0x9a, 0xa9, 0x00, 0x85, 0x30, 0x58,
                  0x8d, 0x00, 0x4c, 0x4c, 0x09, 0x50});
    r.at(0x5100, {0x48, 0x8a, 0x48, 0xa6, 0x30,
                  0xad, 0x00, 0x49, 0x9d, 0x00, 0x07,       // IN1 -> 0700,X
                  0xa9, 0x01, 0x8d, 0x00, 0x48,             // CTRLD = 1 (flip bit low)
                  0xad, 0x00, 0x48, 0x9d, 0x40, 0x07,       // trackball -> 0740,X
                  0xa9, 0x40, 0x8d, 0x00, 0x48,             // CTRLD = 0, not flipped
                  0xad, 0x00, 0x48, 0x9d, 0x80, 0x07,       // IN0 -> 0780,X
                  0xe6, 0x30, 0x8d, 0x00, 0x4d,
                  0x68, 0xaa, 0x68, 0x40});
    r.vectors(0x5000, 0x5100);
    static missile::Machine m;
    m.init(r.b.data(), 24000);
    m.set_inputs(missile::kFireLeft | missile::kStart1 | missile::kCoin2);
    m.set_trackball(3 * 256, 2 * 256);
    run(m, 6);
    const uint8_t* v = m.vram();
    CHECK_EQ(v[0x700], 0xe3);           // IRQ at line 0: VBLANK, left fire low
    CHECK_EQ(v[0x701], 0x63);           // line 64: no VBLANK
    CHECK_EQ(v[0x702], 0x63);
    CHECK_EQ(v[0x780], 0xaf);           // start 1 and the centre coin low
    CHECK(!m.flipped());
    for (int f = 1; f < 5; ++f) {
        const uint8_t a = v[0x740 + 4 * f], b = v[0x740 + 4 * (f + 1)];
        CHECK_EQ((b - a) & 0x0f, 3);            // X: +3 a frame
        CHECK_EQ(((b >> 4) - (a >> 4)) & 0x0f, 2);  // Y: +2 a frame (up)
    }
    // Within a frame the count moves on: the four reads see a quarter
    // frame's worth each (3 counts a frame: never back, at most +1).
    for (int k = 1; k < 4; ++k) {
        const int d = (v[0x750 + k] - v[0x750 + k - 1]) & 0x0f;
        CHECK(d == 0 || d == 1);
    }
    // The d-pad: left, 1 count the first frame, 1 + 8/30 the next.
    m.set_trackball(0, 0);
    m.set_inputs(missile::kLeft);
    run(m, 2);
    const uint8_t a = v[0x740 + 4 * 6], b = v[0x740 + 4 * 7];
    CHECK_EQ((a - b) & 0x0f, missile::Machine::kButtonSpeed / 256);
    CHECK_EQ(b >> 4, a >> 4);
    CHECK_EQ(missile::Machine::button_speed(0), 256);
    CHECK_EQ(missile::Machine::button_speed(15), 1280);
    CHECK_EQ(missile::Machine::button_speed(30), 2304);
    CHECK_EQ(missile::Machine::button_speed(99), 2304);
}

// No watchdog write: the board resets after 8 frames.
void test_watchdog()
{
    Rom r;
    r.at(0x5000, {0x4c, 0x00, 0x50});
    r.vectors(0x5000, 0x5000);
    static missile::Machine m;
    m.init(r.b.data(), 24000);
    run(m, 20);
    CHECK_EQ(m.watchdog_resets(), 2);
}

// Idle skipping: the main loop's LSR / BCC wait and the IRQ handler's
// BIT 4900 / BMI wait for VBLANK's end, with POKEY RANDOM (read by the
// clock) stored after each - every byte must be as without skipping.
void test_idle_skip()
{
    Rom r;
    r.at(0x5000, {0x78, 0xa2, 0xff, 0x9a, 0xd8,
                  0xa9, 0x03, 0x8d, 0x0f, 0x40,             // SKCTL: RANDOM runs
                  0xa9, 0x00, 0x85, 0x9f, 0x85, 0x40, 0x85, 0x41,
                  0xa0, 0x00, 0x58,
                  0x46, 0x9f, 0x90, 0xfc,                   // 5015: LSR $9F; BCC
                  0xe6, 0x40, 0xad, 0x0a, 0x40, 0x99, 0x00, 0x07, 0xc8,  // RANDOM -> 0700,Y
                  0x8d, 0x00, 0x4c, 0x4c, 0x15, 0x50});
    r.at(0x5100, {0x48,
                  0x2c, 0x00, 0x49, 0x30, 0xfb,             // 5101: BIT $4900; BMI
                  0x8a, 0x48, 0xe6, 0x41, 0xa6, 0x41,
                  0xad, 0x0a, 0x40, 0x9d, 0x00, 0x06,       // RANDOM -> 0600,X
                  0xa9, 0x01, 0x85, 0x9f, 0x8d, 0x00, 0x4d,
                  0x68, 0xaa, 0x68, 0x40});
    r.vectors(0x5000, 0x5100);
    static missile::Machine a, b;
    a.init(r.b.data(), 24000);
    b.init(r.b.data(), 24000);
    b.set_idle_skip(false);
    run(a, 30);
    run(b, 30);
    CHECK(std::memcmp(a.vram(), b.vram(), 0x4000) == 0);
    CHECK_EQ(a.cpu().pc, b.cpu().pc);
    CHECK_EQ(a.cpu().a, b.cpu().a);
    CHECK_EQ(a.cpu().y, b.cpu().y);
    CHECK_EQ(a.vram()[0x41], 120);      // 4 IRQs a frame
    CHECK(a.idle_cycles_skipped() > 30u * 10000);
    CHECK_EQ(b.idle_cycles_skipped(), 0);
}

} // namespace

int main()
{
    test_pixels();
    test_madsel_irqh();
    test_irq();
    test_inputs();
    test_watchdog();
    test_idle_skip();
    std::printf("missile_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
