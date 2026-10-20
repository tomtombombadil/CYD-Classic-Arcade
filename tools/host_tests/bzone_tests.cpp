// Unit tests for the Battlezone parts, with hand-made programs and display
// lists - no ROMs: the AVG's Battlezone variant (monochrome STAT, the clip
// window), the sound board models (shell / explosion envelopes, loud and
// soft, the engine's 555 and its rev ramp, the sound-on switch), the tread
// mapping (d-pad and sticks) and the machine's wiring on a tiny 6502
// program (inputs, switches, POKEY, math box, sound latch, the picture in
// the overlay's colours, NMIs, the self-test switch, the watchdog), and
// that idle skipping leaves a program's results exactly as running every
// cycle does.
//
//   bzone_tests
#include "../../emu/machines/atari_avg.h"
#include "../../emu/machines/bzone.h"
#include "../../emu/sound/bzone_sound.h"

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

// --- AVG, Battlezone variant ----------------------------------------------------

struct AvgRig {
    uint8_t mem[0x4000] = {};
    uint16_t at = 0;
    struct Line { int32_t x0, y0, x1, y1; int color, level; };
    std::vector<Line> lines;
    void w(uint16_t v)
    {
        mem[at * 2] = uint8_t(v);
        mem[at * 2 + 1] = uint8_t(v >> 8);
        ++at;
    }
    static uint8_t rd(void* c, uint16_t a) { return static_cast<AvgRig*>(c)->mem[(a - 0x2000) & 0x3fff]; }
    static void line(void* c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level)
    {
        static_cast<AvgRig*>(c)->lines.push_back({x0, y0, x1, y1, color, level});
    }
};

constexpr int32_t kU = 65536;      // one screen unit in the AVG's 16.16

// A long vector at binary scale 1: y (up) and x in units of ~1/2 screen unit.
void vctr(AvgRig& r, int y, int x, int z)
{
    r.w(uint16_t(y & 0x1fff));
    r.w(uint16_t((z << 13) | (x & 0x1fff)));
}

void test_avg_bzone()
{
    const int32_t cx = 290 * kU, cy = 200 * kU;
    AvgRig r;
    atari_avg::Avg avg;
    avg.init(AvgRig::rd, AvgRig::line, &r, 0x2000, cx, cy, atari_avg::Variant::Battlezone);
    r.w(0x7000);                        // SCAL binary 0, linear 0
    r.w(0x6800 | 0x0400 | 0x00c5);      // STAT: bit 11 set - still the intensity (12), no clip
    r.w(0x8000);                        // CNTR
    vctr(r, 0, 100, 1);                 // a line right, not clipped (no window yet)
    // Window: bottom left at (-100, -50) from the centre, top right at
    // (+100, +50) (AVG Y up).
    r.w(0x8000);
    vctr(r, -50, -100, 0);
    r.w(0x6000 | 0x00f0);               // STAT intensity 15, bit 10 clear, bit 9 clear: bottom left
    r.w(0x8000);
    vctr(r, 50, 100, 0);
    r.w(0x6200 | 0x00f0);               // bit 9 set: top right
    // A line from the centre to 200 units right: cut at +100.
    r.w(0x8000);
    vctr(r, 0, 200, 1);
    // One wholly above the window: dropped.
    r.w(0x8000);
    vctr(r, 80, 0, 0);
    vctr(r, 0, 20, 7);
    r.w(0x2000);                        // HALT
    avg.run();
    CHECK(avg.halted());
    CHECK_EQ(avg.intensity(), 15);
    CHECK_EQ(avg.color(), 0);           // monochrome: STAT never sets a colour
    CHECK_EQ(int(r.lines.size()), 2);
    if (r.lines.size() == 2) {
        const AvgRig::Line& a = r.lines[0];
        CHECK_EQ(a.level, 12);
        CHECK(std::abs((a.x1 - a.x0) - 100 * 65280) < 2 * kU);
        const AvgRig::Line& b = r.lines[1];
        CHECK_EQ(b.level, 15);
        CHECK_EQ(b.x0, cx);
        CHECK(std::abs(b.x1 - (cx + 100 * 65280)) < 2 * kU);       // the window's right edge
        CHECK_EQ(b.x1, avg.clip_x1());
        CHECK_EQ(b.y1, b.y0);
    }
    // The corners as latched: bottom left below / left of the centre.
    CHECK(avg.clip_x0() < cx && avg.clip_y0() > cy);
    CHECK(avg.clip_x1() > cx && avg.clip_y1() < cy);
    // A diagonal line cut at both ends, keeping its slope.
    int32_t got[4] = {};
    atari_avg::clip_line(0, 0, 400 * kU, 200 * kU, 100 * kU, 0, 200 * kU, 1000 * kU,
                         [&](int32_t a, int32_t b, int32_t c, int32_t d) { got[0] = a; got[1] = b; got[2] = c; got[3] = d; });
    CHECK_EQ(got[0], 100 * kU);
    CHECK_EQ(got[1], 50 * kU);
    CHECK_EQ(got[2], 200 * kU);
    CHECK_EQ(got[3], 100 * kU);
    bool called = false;
    atari_avg::clip_line(0, 0, 50 * kU, 0, 100 * kU, 0, 200 * kU, 10 * kU,
                         [&](int32_t, int32_t, int32_t, int32_t) { called = true; });
    CHECK(!called);

    // The window is off again at the next picture's start (until a corner).
    r.lines.clear();
    r.at = 0;
    r.w(0x8000);
    vctr(r, 0, 400, 1);
    r.w(0x2000);
    avg.run();
    CHECK_EQ(int(r.lines.size()), 1);
    if (r.lines.size() == 1) CHECK(r.lines[0].x1 - r.lines[0].x0 > 300 * kU);

    // Tempest's AVG ignores bits 9 / 10 (no clipping) and takes bit 11 as a
    // colour STAT.
    AvgRig t;
    atari_avg::Avg tav;
    tav.init(AvgRig::rd, AvgRig::line, &t, 0x2000, cx, cy);
    t.w(0x8000);
    t.w(0x6000 | 0x00f0);
    t.w(0x6800 | 0x0003);
    t.w(0x8000);
    vctr(t, 0, 300, 1);
    t.w(0x2000);
    tav.run();
    CHECK_EQ(tav.color(), 3);
    CHECK_EQ(int(t.lines.size()), 1);
    if (t.lines.size() == 1) CHECK(t.lines[0].x1 - t.lines[0].x0 > 250 * kU);
}

// --- sound board ------------------------------------------------------------------

struct Run {
    int peak = 0;
    double rms = 0;
};

// Renders `ms` with the latch at `v` (the POKEY silent), after `pre` ms
// at `pre_v`.
Run sound(bzone_sound::Board& b, uint8_t v, int ms, std::vector<int16_t>* keep = nullptr)
{
    std::vector<int16_t> buf(size_t(24 * ms), 0);
    b.write(v);
    b.render(buf.data(), int(buf.size()));
    Run r;
    double sum = 0;
    for (int16_t s : buf) {
        r.peak = std::max(r.peak, std::abs(int(s)));
        sum += double(s) * s;
    }
    r.rms = std::sqrt(sum / double(buf.size()));
    if (keep) *keep = buf;
    return r;
}

void test_sound()
{
    using namespace bzone_sound;
    Board b;
    b.init(24000);
    sound(b, kSoundOn, 6000);           // the engine's control voltage settles
    // Engine: idle ~350 Hz from the 555 (4.12 V), rev ~800 Hz (2.5 V),
    // reached through 100k / 10 uF - after 1 s about 63 % of the way.
    CHECK(std::fabs(b.engine_cv() - 4.12f) < 0.02f);
    CHECK(b.engine_hz() > 330 && b.engine_hz() < 380);
    Run idle = sound(b, kSoundOn | kEngineOn, 1000);
    CHECK(idle.peak > 4000 && idle.peak < 10000);
    sound(b, kSoundOn | kEngineOn | kEngineRev, 1000);
    const float cv1 = b.engine_cv(), want = 2.5f + (4.12f - 2.5f) * std::exp(-1.0f);
    CHECK(std::fabs(cv1 - want) < 0.05f);
    sound(b, kSoundOn | kEngineOn | kEngineRev, 5000);
    CHECK(b.engine_hz() > 750 && b.engine_hz() < 850);
    // Back to idle: fast while the diode conducts (0.18 s), the last half
    // volt at 1 s.
    sound(b, kSoundOn | kEngineOn, 300);
    CHECK(b.engine_cv() > 3.3f && b.engine_cv() < 3.8f);
    // The engine off: silence once the coupling cap has settled.
    sound(b, kSoundOn, 500);
    Run off = sound(b, kSoundOn, 500);
    CHECK_EQ(off.peak, 0);

    // Shell: loud ~14,000, soft about a quarter; it fades once its bit goes
    // (the cap discharges through 23k while the noise bit is high).
    Board s;
    s.init(24000);
    sound(s, kSoundOn, 500);
    const Run hit = sound(s, kSoundOn | kShell | kShellLoud, 20);
    CHECK(s.shell_cap() > 4.9f);
    std::vector<int16_t> tail;
    Run loud = sound(s, kSoundOn | kShellLoud, 1000, &tail);
    loud.peak = std::max(loud.peak, hit.peak);
    CHECK(loud.peak > 10000 && loud.peak < 18000);
    CHECK(s.shell_cap() < 0.1f);
    int late = 0;
    for (size_t i = 12000; i < tail.size(); ++i) late = std::max(late, std::abs(int(tail[i])));
    CHECK(late < loud.peak / 8);        // half a second on: mostly gone
    Board q;
    q.init(24000);
    sound(q, kSoundOn, 500);
    const Run soft_hit = sound(q, kSoundOn | kShell, 20);
    const Run soft = sound(q, kSoundOn, 1000);
    const int soft_peak = std::max(soft.peak, soft_hit.peak);
    CHECK(soft_peak * 2 < loud.peak && soft_peak * 8 > loud.peak);

    // Explosion: louder and longer than the shell.
    Board e;
    e.init(24000);
    sound(e, kSoundOn, 500);
    Run boom = sound(e, kSoundOn | kExplode | kExplodeLoud, 300);
    Run boom_tail = sound(e, kSoundOn | kExplodeLoud, 1000);
    CHECK(std::max(boom.peak, boom_tail.peak) > 12000 && std::max(boom.peak, boom_tail.peak) < 22000);
    CHECK(boom_tail.rms > loud.rms);

    // The POKEY: one channel at full volume (a 600 Hz square, 0 / 15 x
    // 256) peaks a few thousand; sound off mutes it with everything else.
    auto square = [] {
        std::vector<int16_t> io(4800, 15 * 256);
        for (size_t i = 0; i < io.size(); i += 40)
            for (size_t k = i; k < i + 20; ++k) io[k] = 0;
        return io;
    };
    Board p;
    p.init(24000);
    std::vector<int16_t> io = square();
    p.write(kSoundOn);
    p.render(io.data(), int(io.size()));
    io = square();
    p.render(io.data(), int(io.size()));
    int pk = 0;
    for (int16_t v : io) pk = std::max(pk, std::abs(int(v)));
    CHECK(pk > 1500 && pk < 6000);
    Board mute;
    mute.init(24000);
    io = square();
    mute.write(kEngineOn | kShell | kExplode);
    mute.render(io.data(), int(io.size()));
    bool silent = true;
    for (int16_t v : io) silent = silent && v == 0;
    CHECK(silent);
}

// --- the treads ---------------------------------------------------------------------

void test_treads()
{
    using namespace bzone;
    // Sticks centred: the d-pad's bits as they are.
    CHECK_EQ(treads(kLeftFwd | kRightFwd | kFire, 0, 0), kLeftFwd | kRightFwd | kFire);
    CHECK_EQ(treads(kLeftBack | kRightFwd, 100, -100), kLeftBack | kRightFwd);
    // A stick past a third: the sticks are the treads (the left stick's own
    // d-pad bits dropped), the other buttons stay.
    CHECK_EQ(treads(kLeftFwd | kRightFwd | kFire, -300, 0), kLeftFwd | kFire);
    CHECK_EQ(treads(kLeftFwd | kRightFwd, -300, 400), kLeftFwd | kRightBack);
    CHECK_EQ(treads(0, 0, -512), kRightFwd);
    CHECK_EQ(treads(kLeftBack | kRightBack, 511, 511), kLeftBack | kRightBack);
}

// --- the machine --------------------------------------------------------------------

struct Board {
    uint8_t cpu[0x3000];
    uint8_t vec[0x1000] = {};
    uint16_t at = 0x5000;
    Board() { std::memset(cpu, 0xea, sizeof cpu); }
    void put(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) cpu[at++ - 0x5000] = b;
    }
    void vectors(uint16_t nmi, uint16_t reset)
    {
        cpu[0x2ffa] = uint8_t(nmi); cpu[0x2ffb] = uint8_t(nmi >> 8);     // 7FFA, seen at FFFA
        cpu[0x2ffc] = uint8_t(reset); cpu[0x2ffd] = uint8_t(reset >> 8);
        cpu[0x2ffe] = uint8_t(nmi); cpu[0x2fff] = uint8_t(nmi >> 8);
    }
};

std::vector<int> lit(const std::vector<uint16_t>& row)
{
    std::vector<int> x;
    for (size_t i = 0; i < row.size(); ++i)
        if (row[i]) x.push_back(int(i));
    return x;
}

void test_machine()
{
    static Board b;
    b.put({0xa2, 0x7f, 0x9a,                    // 5000 LDX #7F; TXS
           0xa9, 0x07, 0x8d, 0x2f, 0x18,        //      POKEY SKCTL: out of reset
           0xad, 0x00, 0x08, 0x85, 0x10,        //      IN0 -> 10
           0xad, 0x00, 0x0a, 0x85, 0x11,        //      DSW0 -> 11
           0xad, 0x00, 0x0c, 0x85, 0x12,        //      DSW1 -> 12
           0xa9, 0x34, 0x8d, 0x75, 0x18,        //      math box 15: R7 low = 34
           0xa9, 0x12, 0x8d, 0x76, 0x18,        //               16: R7 high = 12
           0xad, 0x10, 0x18, 0x85, 0x13,        //      result low -> 13
           0xad, 0x18, 0x18, 0x85, 0x14,        //      result high -> 14
           0xa9, 0xb5, 0x8d, 0x40, 0x18,        //      sound latch = B5
           0x8d, 0x00, 0x12,                    //      AVG go
           0xa9, 0xff, 0x85, 0x18,              //      18 = FF
           0xad, 0x28, 0x18, 0x85, 0x15,        // loop (5037): ALLPOT -> 15
           0xad, 0x00, 0x08, 0x85, 0x16,        //      IN0 -> 16
           0x05, 0x17, 0x85, 0x17,              //      17 |= IN0
           0xa5, 0x16, 0x25, 0x18, 0x85, 0x18,  //      18 &= IN0
           0x4c, 0x37, 0x50});                  //      JMP loop
    b.at = 0x5100;
    b.put({0xe6, 0x20, 0x8d, 0x00, 0x14, 0x40});  // NMI: INC 20; watchdog; RTI
    b.vectors(0x5100, 0x5000);

    static bzone::Machine m;
    m.init({b.cpu, b.vec}, 24000, false);
    // The list in vector RAM: STAT intensity 15 (no clip change), CNTR, a
    // line from the centre 150 units up (binary scale 0: about 150 screen
    // units) - from y 200 to y 50, across the red / green edge at 80.
    const uint16_t list[] = {0x7000, 0x64f0, 0x8000, 150, 0x2000, 0x2000};
    for (int i = 0; i < 6; ++i) {
        m.cpu().wr(uint16_t(0x2000 + i * 2), uint8_t(list[i]));
        m.cpu().wr(uint16_t(0x2001 + i * 2), uint8_t(list[i] >> 8));
    }
    m.set_inputs(bzone::kLeftFwd | bzone::kRightBack | bzone::kFire | bzone::kCoin);
    int16_t audio[1024];
    for (int f = 0; f < 4; ++f) m.run_frame(audio, 1024);
    CHECK(m.cpu().rd(0x0020) >= 15);            // 4 NMIs a frame
    CHECK_EQ(m.watchdog_resets(), 0);
    CHECK_EQ(m.cpu().rd(0x0010) & 0x3f, 0x3e);  // coin 1 low; slam, self-test, step high
    CHECK_EQ(m.cpu().rd(0x0011), bzone::kDipDefault);
    CHECK_EQ(m.cpu().rd(0x0012), bzone::kPricing1Coin1Credit);
    CHECK_EQ(m.cpu().rd(0x0013), 0x34);         // math box: the register loaded
    CHECK_EQ(m.cpu().rd(0x0014), 0x12);
    CHECK_EQ(m.sound().latch(), 0xb5);
    // ALLPOT: left forward (bit 3), right back (bit 0), fire (bit 4) - high.
    CHECK_EQ(m.cpu().rd(0x0015), 0x19);
    CHECK(m.cpu().rd(0x0016) & 0x40);           // the AVG has finished
    // The 3 kHz clock toggles (bit 7 of IN0 = CPU cycles bit 8): read both
    // ways.
    CHECK(m.cpu().rd(0x0017) & 0x80);
    CHECK(!(m.cpu().rd(0x0018) & 0x80));
    // A tread pressed both ways counts as neither; the starts read high.
    m.set_inputs(bzone::kLeftFwd | bzone::kLeftBack | bzone::kRightFwd | bzone::kStart1 | bzone::kStart2);
    m.run_frame(audio, 1024);
    CHECK_EQ(m.cpu().rd(0x0015), 0x62);
    CHECK_EQ(m.in3(), 0x62);

    // The picture: one line up the middle, red above row 44 (the top fifth
    // of the 400 units), green below; 580 units across the 320 columns.
    CHECK(m.vg_runs() >= 1);
    CHECK_EQ(m.display().count(), 2);
    std::vector<uint16_t> row(bzone::kScreenW);
    auto pixel_at = [&](int y) -> uint16_t {
        m.render_line(y, row.data());
        const std::vector<int> px = lit(row);
        CHECK(!px.empty());
        if (px.empty()) return 0;
        CHECK(std::abs(px.front() - 160) <= 1);
        return row[size_t(px.front())];
    };
    const uint16_t red = pixel_at(36), green = pixel_at(60);
    CHECK((red >> 11) > 25 && ((red >> 5) & 63) < 12 && (red & 31) < 6);
    CHECK((green >> 11) < 6 && ((green >> 5) & 63) > 50 && (green & 31) < 6);
    CHECK(pixel_at(43) == red);
    CHECK(pixel_at(44) == green);
    m.render_line(20, row.data());              // above the line's end (y 50 units = row 27)
    CHECK(lit(row).empty());

    // The self-test switch holds the NMIs off.
    m.set_inputs(bzone::kTest);
    m.run_frame(audio, 1024);
    const uint8_t n0 = m.cpu().rd(0x0020);
    m.run_frame(audio, 1024);
    CHECK_EQ(m.cpu().rd(0x0020), n0);
    // ... so nothing feeds the watchdog: the board resets.
    for (int f = 0; f < 8; ++f) m.run_frame(audio, 1024);
    CHECK(m.watchdog_resets() >= 1);
    m.set_inputs(0);
}

// The two waits idle skipping knows (LSR zp; BCC and LDX zp; BEQ), entered
// at either instruction, with an NMI handler that ends them now and then
// and a main loop that logs the 3 kHz clock (time-dependent): the same RAM
// and registers with and without skipping.
void test_idle_skip()
{
    static Board b;
    b.put({0xa2, 0x7f, 0x9a,                    // 5000 LDX #7F; TXS
           0x46, 0x39, 0x90, 0xfc,              // 5003 LSR 39; BCC 5003
           0xa4, 0x30,                          //      LDY 30
           0xad, 0x00, 0x08,                    //      LDA 0800 (bit 7: 3 kHz)
           0x99, 0x00, 0x02,                    //      STA 0200,Y
           0xe6, 0x30,                          //      INC 30
           0xa6, 0x28, 0xf0, 0xfc,              // 5013 LDX 28; BEQ 5013
           0xa9, 0x00, 0x85, 0x28,              //      LDA #0; STA 28
           0xa2, 0x00,                          //      LDX #0
           0xe8, 0xe0, 0x3d, 0xd0, 0xfb,        //      INX; CPX #3D; BNE (a little work)
           0x4c, 0x03, 0x50});                  //      JMP 5003
    b.at = 0x5100;
    b.put({0x48,                                // NMI: PHA
           0xe6, 0x3a, 0xa5, 0x3a,              //      INC 3A; LDA 3A
           0x29, 0x03, 0xd0, 0x02,              //      AND #3; BNE +2
           0xe6, 0x39,                          //      INC 39 (every 4th)
           0xa5, 0x3a, 0x29, 0x05, 0xd0, 0x02,  //      LDA 3A; AND #5; BNE +2
           0xe6, 0x28,                          //      INC 28
           0x8d, 0x00, 0x14,                    //      watchdog
           0x68, 0x40});                        //      PLA; RTI
    b.vectors(0x5100, 0x5000);
    static bzone::Machine on, off;
    on.init({b.cpu, b.vec}, 24000, false);
    off.init({b.cpu, b.vec}, 24000, false);
    off.set_idle_skip(false);
    int16_t audio[1024];
    bool same = true;
    for (int f = 0; f < 120; ++f) {
        on.run_frame(audio, 1024);
        off.run_frame(audio, 1024);
        for (int a = 0; a < 0x400 && same; ++a) same = on.cpu().rd(uint16_t(a)) == off.cpu().rd(uint16_t(a));
        same = same && on.cpu().pc == off.cpu().pc && on.cpu().a == off.cpu().a && on.cpu().x == off.cpu().x &&
               on.cpu().y == off.cpu().y && on.cpu().p() == off.cpu().p();
    }
    CHECK(same);
    CHECK(on.cpu().rd(0x0030) > 50);            // the main loop went round
    CHECK(on.idle_cycles() > 120u * bzone::kCyclesPerFrame / 2);
    CHECK_EQ(off.idle_cycles(), 0u);
}

} // namespace

int main()
{
    test_avg_bzone();
    test_sound();
    test_treads();
    test_machine();
    test_idle_skip();
    std::printf("bzone tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
