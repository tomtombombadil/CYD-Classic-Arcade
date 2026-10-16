// Unit tests for the Qix board that need no ROMs: hand-assembled 6809 /
// 6802 programs check the video RAM latch and window, the palette and the
// picture, the FIRQs between the CPUs, the scanline register, the data
// CPU's VSYNC interrupt, the sound command / DAC / volume, the first
// boot's restart, and that idle-loop skipping (pure, scanline and counting
// waits) leaves the programs' results as they are without it.
//
//   qix_tests
#include "../../emu/machines/qix.h"

#include <cstdio>
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
        else { ++g_fail; std::printf("FAIL %s:%d: %s = %lld (0x%llx), want %lld (0x%llx)\n", \
                                     __FILE__, __LINE__, #a, va_, va_, vb_, vb_); }        \
    } while (0)

// The three ROM regions as the machine sees them: data C000-FFFF, video
// C800-FFFF, sound F800-FFFF.
struct Roms {
    std::vector<uint8_t> data = std::vector<uint8_t>(0x4000, 0x12);     // NOPs
    std::vector<uint8_t> video = std::vector<uint8_t>(0x3800, 0x12);
    std::vector<uint8_t> sound = std::vector<uint8_t>(0x800, 0x01);
    uint8_t* at(int region, uint16_t addr)
    {
        switch (region) {
        case 0:  return &data[addr - 0xc000];
        case 1:  return &video[addr - 0xc800];
        default: return &sound[addr - 0xf800];
        }
    }
    void put(int region, uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) *at(region, addr++) = b;
    }
    void vec(int region, uint16_t vector, uint16_t to) { put(region, vector, {uint8_t(to >> 8), uint8_t(to)}); }
    // Idle programs: both 6809s and the 6802 wait in a branch to itself.
    Roms()
    {
        put(0, 0xc000, {0x20, 0xfe});
        vec(0, 0xfffe, 0xc000);
        put(1, 0xc800, {0x20, 0xfe});
        vec(1, 0xfffe, 0xc800);
        put(2, 0xf800, {0x20, 0xfe});
        vec(2, 0xfffe, 0xf800);
    }
};

std::unique_ptr<qix::Machine> make(Roms& r, bool idle_skip = true)
{
    std::unique_ptr<qix::Machine> m(new qix::Machine);
    const uint8_t* regions[3] = {r.data.data(), r.video.data(), r.sound.data()};
    m->init(regions, 24000);
    m->set_idle_skipping(idle_skip);
    return m;
}

void run(qix::Machine& m, int frames)
{
    int16_t audio[1024];
    for (int i = 0; i < frames; ++i) m.run_frame(audio, 1024);
}

// --- timing ------------------------------------------------------------------------------

void test_scanline()
{
    // Frames start at line 256 (VSYNC); line 0 is the 17th.
    CHECK_EQ(qix::Machine::beam_line(0), 256);
    CHECK_EQ(qix::Machine::beam_line(17 * 82), 0);
    CHECK_EQ(qix::Machine::scanline_at(0), 255);
    CHECK_EQ(qix::Machine::scanline_at(16 * 82 + 81), 255);
    CHECK_EQ(qix::Machine::scanline_at(17 * 82), 0);
    CHECK_EQ(qix::Machine::scanline_at(17 * 82 + 81), 0);
    CHECK_EQ(qix::Machine::scanline_at(18 * 82), 1);
    CHECK_EQ(qix::Machine::scanline_at((17 + 255) * 82), 255);
    CHECK_EQ(qix::kCyclesPerFrame, 22386);
}

// --- video RAM, palette, FIRQs -----------------------------------------------------------

void test_video_and_firq()
{
    Roms r;
    // Video CPU.
    r.put(1, 0xc800, {
        0x10, 0xce, 0x83, 0x80,         // LDS #8380
        0xcc, 0x12, 0x34,               // LDD #1234
        0xfd, 0x94, 0x02,               // STD 9402 (latch)
        0x86, 0xab, 0xb7, 0x94, 0x00,   // LDA #AB ; STA 9400 -> video 1234
        0xcc, 0x85, 0x10,               // LDD #8510 (upper half)
        0xfd, 0x94, 0x02,
        0x86, 0xcd, 0xb7, 0x94, 0x00,   // video 8510 = CD
        0x86, 0xef, 0xb7, 0x05, 0x11,   // STA 0511: the window is the upper half -> 8511
        0xcc, 0x85, 0x11, 0xfd, 0x94, 0x02,
        0xb6, 0x94, 0x00,               // LDA 9400 (8511)
        0xb7, 0x80, 0x00,               // STA 8000 (shared)
        0x86, 0x01, 0xb7, 0x88, 0x00,   // palette bank 1
        0x86, 0xff, 0xb7, 0x91, 0xab,   // bank 1 entry AB = white
        0x86, 0x00, 0xb7, 0x94, 0x02,   // latch back to the lower half
        0xb6, 0x00, 0x00,               // LDA 0000 (window: lower half)
        0xb7, 0x80, 0x04,
        0x1c, 0xbf,                     // ANDCC #BF (FIRQ on)
        0xb7, 0x8c, 0x00,               // STA 8C00: the data CPU's FIRQ
        0x20, 0xfe,
    });
    r.put(1, 0xc900, {
        0xb7, 0x8c, 0x01,               // ack own FIRQ
        0x7c, 0x80, 0x01,               // INC 8001
        0x3b,                           // RTI
    });
    r.vec(1, 0xfff6, 0xc900);
    // Data CPU: FIRQ on, wait; its handler copies the shared byte and
    // raises the video CPU's FIRQ.
    r.put(0, 0xc000, {0x10, 0xce, 0x88, 0x00, 0x1c, 0xbf, 0x20, 0xfe});
    r.put(0, 0xc100, {
        0x7d, 0x8c, 0x01,               // TST 8C01: a read acks too
        0x7c, 0x80, 0x02,               // INC 8002
        0xb6, 0x80, 0x00, 0xb7, 0x80, 0x03,
        0xb7, 0x8c, 0x00,               // the video CPU's FIRQ
        0x3b,
    });
    r.vec(0, 0xfff6, 0xc100);
    auto m = make(r);
    run(*m, 2);
    CHECK_EQ(m->video_byte(0x1234), 0xab);
    CHECK_EQ(m->video_byte(0x8510), 0xcd);
    CHECK_EQ(m->video_byte(0x8511), 0xef);
    CHECK_EQ(m->video_byte(0x0511), 0x00);
    CHECK_EQ(m->shared_ram()[0], 0xef);
    CHECK_EQ(m->shared_ram()[4], 0x00);
    CHECK_EQ(m->shared_ram()[2], 1);    // the data CPU's FIRQ once
    CHECK_EQ(m->shared_ram()[3], 0xef);
    CHECK_EQ(m->shared_ram()[1], 1);    // and the video CPU's
    CHECK_EQ(m->palette_bank(), 1);
    CHECK_EQ(m->palette_ram()[0x1ab], 0xff);
    // The picture: video 1234 = CRTC line 0x12, column 0x34 -> the
    // player's row 255 - 0x34, column 0x12 - 8.
    std::vector<uint16_t> line(qix::kScreenW);
    m->render_line(255 - 0x34, line.data());
    CHECK_EQ(line[0x12 - 8], 0xffff);
    CHECK_EQ(line[0x12 - 7], 0x0000);
    // Palette levels (our own guess): red at full intensity is 255 / 38 /
    // 38 (the intensity's grey), white 255s, intensity 0 black.
    m->palette_ram()[0x100] = 0xc3;
    run(*m, 1);
    m->render_line(0, line.data());
    CHECK_EQ(line[5], (31 << 11) | (9 << 5) | 4);
}

// --- interrupts and idle skipping --------------------------------------------------------

// The data CPU's counting wait on its VSYNC interrupt, as Qix's: the count
// and the wake-ups must come out the same with and without skipping.
Roms counting_rom()
{
    Roms r;
    r.put(0, 0xc000, {
        0x10, 0xce, 0x88, 0x00,         // LDS #8800
        0x86, 0x80, 0x1f, 0x8b,         // DP = 80
        0x86, 0x35, 0xb7, 0x90, 0x03,   // sound PIA CRB = 35: CB1 (VSYNC) falling edge IRQ
        0x1c, 0xef,                     // ANDCC #EF
        0x86, 0x01, 0xb7, 0x85, 0x20,   // loop: LDA #1 ; STA 8520
        0x0c, 0x25,                     // w: INC <25
        0xb6, 0x85, 0x20,               //    LDA 8520
        0x26, 0xf9,                     //    BNE w
        0x7c, 0x84, 0x00,               // INC 8400
        0x20, 0xef,                     // BRA loop
    });
    r.put(0, 0xc100, {
        0xb6, 0x90, 0x02,               // LDA 9002: clears the flag
        0x7f, 0x85, 0x20,               // CLR 8520
        0x3b,
    });
    r.vec(0, 0xfff8, 0xc100);
    return r;
}

void test_counting_wait()
{
    Roms r = counting_rom();
    auto a = make(r, true), b = make(r, false);
    run(*a, 10);
    run(*b, 10);
    CHECK_EQ(a->data_ram()[0], 10);     // woken by every VSYNC
    CHECK_EQ(b->data_ram()[0], 10);
    CHECK_EQ(a->shared_ram()[0x25], b->shared_ram()[0x25]);
    CHECK(a->shared_ram()[0x25] != 0);
    CHECK(a->idle_skips() > 0);
    CHECK_EQ(b->idle_skips(), 0);
    CHECK(a->cycles_run(qix::Machine::kData) * 10 < b->cycles_run(qix::Machine::kData));
    CHECK(a->cycles_run(qix::Machine::kVideo) * 10 < b->cycles_run(qix::Machine::kVideo));
}

// The video CPU waits for scanline 100 and bumps a shared counter; the data
// CPU waits on that counter (a pure loop on shared RAM) and counts its
// changes. Both sleep: the shared write must wake the data CPU, the
// scanline must wake the video CPU.
void test_shared_and_scanline_waits()
{
    Roms r;
    r.put(1, 0xc800, {
        0x10, 0xce, 0x83, 0x80,
        0xb6, 0x98, 0x00, 0x81, 0x64, 0x26, 0xf9,  // w1: LDA 9800 ; CMPA #100 ; BNE w1
        0x7c, 0x80, 0x10,                          // INC 8010
        0xb6, 0x98, 0x00, 0x81, 0x64, 0x27, 0xf9,  // w2: LDA 9800 ; CMPA #100 ; BEQ w2
        0x20, 0xeb,                                // BRA w1
    });
    r.put(0, 0xc000, {
        0x10, 0xce, 0x88, 0x00,
        0xb6, 0x80, 0x10,               // w: LDA 8010
        0xb1, 0x80, 0x11,               //    CMPA 8011
        0x27, 0xf8,                     //    BEQ w
        0xb7, 0x80, 0x11,               // STA 8011
        0x7c, 0x80, 0x12,               // INC 8012
        0x20, 0xf0,
    });
    auto a = make(r, true), b = make(r, false);
    run(*a, 20);
    run(*b, 20);
    CHECK_EQ(a->shared_ram()[0x10], 20);
    CHECK_EQ(b->shared_ram()[0x10], 20);
    CHECK_EQ(a->shared_ram()[0x12], 20);
    CHECK_EQ(b->shared_ram()[0x12], 20);
    CHECK(a->idle_skips() > 20);
    // A scanline wait wakes every line and finds its loop again (~30 %).
    CHECK(a->cycles_run(qix::Machine::kVideo) * 2 < b->cycles_run(qix::Machine::kVideo));
    CHECK(a->cycles_run(qix::Machine::kData) * 10 < b->cycles_run(qix::Machine::kData));
}

// --- sound -------------------------------------------------------------------------------

void test_sound()
{
    Roms r;
    r.put(2, 0xf800, {
        0x8e, 0x00, 0x7f,               // LDS #7F
        0xce, 0x40, 0x00,               // LDX #4000
        0x6f, 0x01, 0x6f, 0x00,         // CRA = 0, DDRA = 0
        0x86, 0x07, 0xa7, 0x01,         // CRA = 07: CA1 rising edge IRQ
        0x6f, 0x03, 0x86, 0xff, 0xa7, 0x02,  // CRB = 0, DDRB = FF
        0x86, 0x04, 0xa7, 0x03,         // CRB = 04
        0x0e, 0x20, 0xfe,               // CLI ; BRA *
    });
    r.put(2, 0xf880, {
        0x8e, 0x00, 0x7f, 0x0e,         // LDS #7F ; CLI (the IRQ waits one instruction)
        0xb6, 0x40, 0x00,               // LDAA 4000: the command, clears the IRQ
        0xb7, 0x40, 0x02,               // STAA 4002: the DAC
        0x7c, 0x00, 0x10,               // INC 0010
        0x20, 0xfe,
    });
    r.vec(2, 0xfff8, 0xf880);
    r.put(0, 0xc000, {
        0x86, 0xff, 0xb7, 0x90, 0x00,   // DDRA = FF
        0x86, 0xff, 0xb7, 0x90, 0x02,   // DDRB = FF
        0x86, 0x04, 0xb7, 0x90, 0x03,   // CRB data
        0x86, 0x11, 0xb7, 0x90, 0x02,   // volume 11
        0x86, 0x34, 0xb7, 0x90, 0x01,   // CA2 low
        0x86, 0xda, 0xb7, 0x90, 0x00,   // command DA
        0x86, 0x3c, 0xb7, 0x90, 0x01,   // CA2 high: the sound CPU's interrupt
        0x20, 0xfe,
    });
    auto m = make(r);
    int16_t audio[1024];
    int peak = 0;
    for (int f = 0; f < 3; ++f) {
        const int n = m->run_frame(audio, 1024);
        CHECK(n == 429 || n == 430);    // 24,000 / 55.84
        for (int i = 0; i < n; ++i) peak = audio[i] > peak ? audio[i] : peak;
    }
    CHECK_EQ(m->sound().dac(), 0xda);
    CHECK_EQ(m->sound_commands(), 1);
    CHECK_EQ(m->sound().cpu().rd(0x10), 1);      // one interrupt, taken once
    // Volume 11: 22.25k across the bottom of the divider on both sides.
    const float want = 22250.0f / 32250.0f;
    CHECK(m->sound().gain() > want - 0.001f && m->sound().gain() < want + 0.001f);
    CHECK(peak > 2000);                 // the step from 80 to DA, high-passed
}

// --- the first boot ----------------------------------------------------------------------

void test_cmos_restart()
{
    Roms r;
    r.put(1, 0xc800, {
        0x7c, 0x85, 0x00,               // INC 8500: boots
        0xcc, 0x55, 0xaa, 0xfd, 0x86, 0xa9,   // 86A9 = 55AA (8700 = sum of zeros = 0)
        0x20, 0xfe,
    });
    auto m = make(r);
    run(*m, 10);
    CHECK_EQ(m->cmos()[0x100], 1);
    CHECK(m->cmos_valid());
    CHECK_EQ(m->restarted_at(), 0u);
    run(*m, 40);
    CHECK(m->restarted_at() > 0);
    CHECK_EQ(m->cmos()[0x100], 2);      // started again, the CMOS kept
    run(*m, 60);
    CHECK_EQ(m->cmos()[0x100], 2);      // only once
}

} // namespace

int main()
{
    test_scanline();
    test_video_and_firq();
    test_counting_wait();
    test_shared_and_scanline_waits();
    test_sound();
    test_cmos_restart();
    std::printf("qix tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
