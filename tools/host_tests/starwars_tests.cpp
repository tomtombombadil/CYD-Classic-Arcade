// Unit tests for the Star Wars board's parts, with hand-made programs and
// synthetic data - no ROMs: the TMS5220 (FIFO and status, the bus timing,
// frame parsing, voiced / unvoiced / silent / stop frames), the matrix
// processor on synthetic microcode PROMs (and old dumps' stray high
// nibbles), the divider, the random number LFSR's jump ahead, the 6532
// RIOT, the AVG's Star Wars variant, and the machine's wiring on two tiny
// 6809 programs (sound command and reply, ROM bank, inputs, yoke ADC,
// divider, watchdog).
//
//   starwars_tests
#include "../../emu/machines/atari_avg.h"
#include "../../emu/machines/riot6532.h"
#include "../../emu/machines/starwars.h"
#include "../../emu/machines/starwars_mathbox.h"
#include "../../emu/sound/tms5220.h"

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

// --- TMS5220 --------------------------------------------------------------------

// Packs LPC fields into bytes the way the chip reads them: each field MSB
// first, the bits filling each byte from its lowest bit.
struct BitPacker {
    std::vector<uint8_t> bytes;
    int bit = 0;
    void put(int v, int n)
    {
        for (int i = n - 1; i >= 0; --i) {
            if (bit == 0) bytes.push_back(0);
            bytes.back() = uint8_t(bytes.back() | (((v >> i) & 1) << bit));
            bit = (bit + 1) & 7;
        }
    }
    void voiced(int e, int p, const int* k)
    {
        put(e, 4);
        put(0, 1);
        put(p, 6);
        for (int i = 0; i < 10; ++i) put(k[i], tms5220::kKBits[i]);
    }
    void unvoiced(int e, const int* k)
    {
        put(e, 4);
        put(0, 1);
        put(0, 6);
        for (int i = 0; i < 4; ++i) put(k[i], tms5220::kKBits[i]);
    }
};

// Runs the chip on a byte stream (Speak External), keeping its FIFO fed.
std::vector<int16_t> speak(tms5220::Chip& c, const std::vector<uint8_t>& bytes, int max_samples)
{
    std::vector<int16_t> out;
    c.write(0x60);
    size_t i = 0;
    while (int(out.size()) < max_samples) {
        while (i < bytes.size() && c.fifo_count() < 16) c.write(bytes[i++]);
        out.push_back(c.step());
        if (i >= bytes.size() && !c.talking()) break;
    }
    return out;
}

void test_tms5220()
{
    static tms5220::Chip c;
    c.init(672000, 1512000, 24000);

    // Status: idle, buffer low and empty.
    CHECK_EQ(c.status(), 0x60);
    // Speak External: 8 bytes keep "buffer low"; the 9th starts the speech.
    c.write(0x60);
    CHECK(c.speak_external());
    for (int i = 0; i < 8; ++i) c.write(0x00);
    CHECK_EQ(c.status() & 0xe0, 0x40);
    CHECK(!c.talking());
    c.write(0x00);
    CHECK_EQ(c.status() & 0xe0, 0x80);
    CHECK(c.talking());
    // FIFO full at 16: more bytes are dropped.
    for (int i = 0; i < 10; ++i) c.write(0x00);
    CHECK_EQ(c.fifo_count(), 16);
    // Reset command (outside Speak External): idle again.
    c.reset();
    c.write(0x70);
    CHECK_EQ(c.status(), 0x60);

    // The bus: /WS low -> not ready; the byte goes in 16 chip clocks (36
    // CPU clocks at 1.512 MHz) later.
    c.reset();
    c.set_data(0x60);
    c.strobes(true, false, 1000);
    CHECK(!c.ready());
    CHECK_EQ(c.event_at(), 1036);
    c.fire();
    CHECK(c.ready());
    CHECK(c.speak_external());
    c.strobes(true, true, 1100);
    // /RS low: the status is latched 13 us later.
    c.strobes(false, true, 1200);
    CHECK(!c.ready());
    CHECK_EQ(c.event_at(), 1200 + 1512 * 13 / 1000 + 1);
    c.fire();
    CHECK(c.ready());
    CHECK_EQ(c.data(), 0x60);
    c.strobes(true, true, 1300);
    CHECK_EQ(c.data(), 0xff);

    // A voiced frame repeated, then stop: the output repeats every pitch
    // period once the frame has settled, and speech ends at the stop.
    const int k[10] = {20, 16, 9, 8, 7, 7, 7, 4, 4, 4};
    BitPacker b;
    for (int i = 0; i < 8; ++i) b.voiced(12, 40, k);
    b.put(15, 4);                               // stop
    for (int i = 0; i < 3; ++i) b.put(0, 8);
    c.reset();
    std::vector<int16_t> s = speak(c, b.bytes, 20000);
    CHECK(c.frames() >= 9);
    CHECK(!c.talking());
    CHECK(s.size() < 20000);
    int peak = 0;
    for (int16_t v : s) peak = std::max(peak, std::abs(int(v)));
    CHECK(peak > 2000);
    // Pitch: kPitch[40] = 68 samples; the chirp starts each period.
    const int period = tms5220::kPitch[40];
    CHECK_EQ(period, 68);
    int same = 0, total = 0;
    for (int i = 900; i < 1300; ++i) {
        ++total;
        if (s[size_t(i)] == s[size_t(i + period)]) ++same;
    }
    CHECK(same * 10 > total * 9);               // the lattice settles to a periodic output

    // Silent frames output nothing; unvoiced frames are noise (not periodic).
    BitPacker q;
    for (int i = 0; i < 4; ++i) q.put(0, 4);    // silence
    for (int i = 0; i < 6; ++i) q.unvoiced(10, k);
    q.put(15, 4);
    for (int i = 0; i < 3; ++i) q.put(0, 8);
    c.reset();
    s = speak(c, q.bytes, 20000);
    bool quiet = true;
    for (int i = 0; i < 400; ++i) quiet = quiet && s[size_t(i)] == 0;
    CHECK(quiet);
    int nz = 0;
    for (int16_t v : s) nz += v != 0;
    CHECK(nz > 500);
    CHECK(!c.talking());

    // Running out of data in Speak External ends the speech (buffer empty).
    BitPacker r;
    for (int i = 0; i < 3; ++i) r.voiced(12, 40, k);       // no stop frame
    c.reset();
    s = speak(c, r.bytes, 20000);
    CHECK(!c.talking());
    CHECK(!c.speak_external());
    CHECK(s.size() < 20000);

    // render(): 24 kHz from 8.4 kHz, gain applied.
    c.reset();
    int16_t buf[100];
    c.render(buf, 100, false, 256);
    bool zero = true;
    for (int16_t v : buf) zero = zero && v == 0;
    CHECK(zero);
}

// --- matrix processor, divider, LFSR -----------------------------------------------

// Writes microinstruction `w` at `addr` into four 1K x 4 PROM images (110
// = bits 15-12 ... 113 = bits 3-0), with `junk` in the high nibbles.
void put_micro(uint8_t* proms, int addr, uint16_t w, uint8_t junk = 0)
{
    proms[addr] = uint8_t((w >> 12) | junk);
    proms[0x400 + addr] = uint8_t(((w >> 8) & 15) | junk);
    proms[0x800 + addr] = uint8_t(((w >> 4) & 15) | junk);
    proms[0xc00 + addr] = uint8_t((w & 15) | junk);
}

uint16_t micro(uint8_t strobes, bool direct, int addr)
{
    return uint16_t(strobes << 8 | (direct ? 0x80 : 0) | (addr & 0x7f));
}

void put_word(uint8_t* ram, int ma, int16_t v)
{
    ram[ma * 2] = uint8_t(uint16_t(v) >> 8);
    ram[ma * 2 + 1] = uint8_t(v);
}

int16_t get_word(const uint8_t* ram, int ma) { return int16_t((ram[ma * 2] << 8) | ram[ma * 2 + 1]); }

void test_matrix()
{
    using namespace starwars_math;
    for (uint8_t junk : {uint8_t(0), uint8_t(0xf0)}) {
        static uint8_t proms[0x1000], ram[0x1000];
        std::memset(proms, 0, sizeof proms);
        std::memset(ram, 0, sizeof ram);
        // Command 1 (address 4): ACC = 0; A = [0]; B = [1]; C = [2] and
        // multiply-accumulate; store ACC's top to [3]; halt.
        put_micro(proms, 4, micro(kClearAcc | kLda, true, 0), junk);
        put_micro(proms, 5, micro(kLdb, true, 1), junk);
        put_micro(proms, 6, micro(kLdc, true, 2), junk);
        put_micro(proms, 7, micro(kStoreAcc | kHalt, true, 3), junk);
        // Command 2 (address 8), BIC addressing: [BIC x 4 + 1] loaded, ACC
        // stored to [BIC x 4 + 2], BIC + 1.
        put_micro(proms, 8, micro(kLoadAcc, false, 1), junk);
        put_micro(proms, 9, micro(kStoreAcc | kIncBic | kHalt, false, 2), junk);
        // Address 0 halts (command 0x3f below wraps to it).
        put_micro(proms, 0x00, micro(kHalt, true, 0), junk);
        // The microprogram is read from the PROMs once, at init.
        MatrixProcessor mp;
        mp.init(proms, ram);
        CHECK_EQ(mp.word(6), micro(kLdc, true, 2));
        put_word(ram, 0, 3000);
        put_word(ram, 1, -1000);
        put_word(ram, 2, 0x2000);                   // 0.25 in 1.15
        const int clocks = mp.run(1);
        CHECK_EQ(clocks, 4 * 5 + 33);
        // (3000 + 1000) x 0x2000 x 4 = 4000 x 2^15: the top 16 bits = 2000.
        CHECK_EQ(get_word(ram, 3), 2000);
        CHECK_EQ(mp.a(), 0);                        // A and B keep their signs
        CHECK_EQ(mp.b(), -1);

        // Command 2: BIC addressing.
        mp.set_bic_hi(1);
        mp.set_bic_lo(0x10);
        CHECK_EQ(mp.bic(), 0x110);
        put_word(ram, 0x110 * 4 + 1, 0x1234);
        mp.run(2);
        CHECK_EQ(get_word(ram, 0x110 * 4 + 2), 0x1234);
        CHECK_EQ(mp.bic(), 0x111);

        // The counter wraps inside its 256-word page: command 0x3f starts
        // at FC; FC-FF no-ops, 00 halts.
        CHECK_EQ(mp.run(0x3f), 5 * 5);
    }

    // Divider: dividend / divisor as a 14-bit fraction while the dividend
    // is below the divisor.
    Divider d;
    srand(5);
    for (int i = 0; i < 200; ++i) {
        const int divisor = 0x100 + rand() % 0x7f00;
        const int dividend = rand() % divisor;
        d.write(6, uint8_t(dividend >> 8));
        d.write(7, uint8_t(dividend));
        d.write(4, uint8_t(divisor >> 8));
        d.write(5, uint8_t(divisor));
        CHECK_EQ(d.quotient(), (int64_t(dividend) << 14) / divisor);
    }

    // The LFSR jumped ahead = stepped one at a time.
    uint32_t s = 0, t = 0;
    for (int i = 0; i < 5000; ++i) t = lfsr_step(t);
    CHECK_EQ(lfsr_advance(s, 5000), t);
    t = 0x123456;
    for (int i = 0; i < 77777; ++i) t = lfsr_step(t);
    CHECK_EQ(lfsr_advance(0x123456, 77777), t);
    // A whole period comes back (x^23 + x^5 + 1 is primitive).
    CHECK_EQ(lfsr_advance(0x2a, 0x7fffff), 0x2a);
}

// --- 6532 RIOT ------------------------------------------------------------------

void test_riot()
{
    riot6532::Riot r;
    r.reset();
    // Timer: 10 x 64 clocks, interrupt enabled (write 1E: A4, A3, /64).
    r.write(0x1e, 10, 1000);
    CHECK_EQ(r.next_event(), 1000 + 1 + 640);
    CHECK_EQ(r.timer(1001), 9);
    CHECK(!r.irq(1640));
    CHECK(r.irq(1641));
    CHECK_EQ(r.read(0x05, 1700) & 0x80, 0x80);  // flags: timer
    // Reading the timer clears its flag; past 0 it counts once a clock.
    CHECK_EQ(r.timer(1641), 0xff);
    CHECK_EQ(r.timer(1645), 0xfb);
    r.read(0x0c, 1700);
    CHECK(!r.irq(1700));
    // PA7 edge (rising, enabled: write 07).
    r.write(0x07, 0, 2000);
    r.set_pa_in(0x7f);
    CHECK(!r.irq(2000));
    r.set_pa_in(0xff);
    CHECK(r.irq(2000));
    CHECK_EQ(r.read(0x05, 2001) & 0x40, 0x40);
    CHECK(!r.irq(2002));                        // reading the flags cleared it
    // Ports: outputs where DDR = 1, pins high elsewhere.
    r.write(0x01, 0x0f, 0);
    r.write(0x00, 0x05, 0);
    CHECK_EQ(r.pa_pins(), 0xf5);
    r.set_pa_in(0x30);
    CHECK_EQ(r.read(0x00, 0), 0x35);
}

// --- the AVG's Star Wars variant ---------------------------------------------------

struct Line {
    int color, level;
};
std::vector<Line> g_lines;
uint8_t g_vmem[0x4000];

uint8_t vread(void*, uint16_t a) { return g_vmem[a & 0x3fff]; }
void vline(void*, int32_t, int32_t, int32_t, int32_t, int color, int level) { g_lines.push_back({color, level}); }

void test_avg_starwars()
{
    std::memset(g_vmem, 0, sizeof g_vmem);
    // Big-endian words: STAT colour 4 (red), intensity 255; VCTR z = 7;
    // VCTR z = 1; VCTR z = 0 (a move); STAT intensity 100; VCTR z = 4; HALT.
    const uint16_t list[] = {0x64ff, 0x0010, 0xe010, 0x0010, 0x2010, 0x0010, 0x0010,
                             0x6264, 0x0010, 0x8010, 0x2000};
    for (int i = 0; i < int(sizeof list / 2); ++i) {
        g_vmem[i * 2] = uint8_t(list[i] >> 8);
        g_vmem[i * 2 + 1] = uint8_t(list[i]);
    }
    atari_avg::Avg avg;
    avg.init(vread, vline, nullptr, 0, 125 << 16, 140 << 16, atari_avg::Variant::StarWars);
    g_lines.clear();
    avg.run();
    CHECK(avg.halted());
    CHECK_EQ(g_lines.size(), 3);
    if (g_lines.size() == 3) {
        CHECK_EQ(g_lines[0].color, 4);
        CHECK_EQ(g_lines[0].level, 255);
        CHECK_EQ(g_lines[1].level, 37);         // z = 1: the 47k resistor alone
        CHECK_EQ(g_lines[2].color, 2);
        CHECK_EQ(g_lines[2].level, 57);         // z = 4: 12k of 1.755
    }
    // Direct pages give the same.
    avg.map(0, g_vmem);
    g_lines.clear();
    avg.run();
    CHECK_EQ(g_lines.size(), 3);
}

// --- the machine's wiring ------------------------------------------------------------

void test_machine()
{
    using namespace starwars;
    static uint8_t rom0[0x4000], rom1[0x2000], rom2[0x2000], rom3[0x2000], rom4[0x2000];
    static uint8_t vec[0x1000], snd0[0x2000], snd1[0x2000], math[0x1000];
    rom0[0] = 0xa5;
    rom0[0x2000] = 0x5a;                        // bank 1
    // Main program at E000.
    const uint8_t main_prog[] = {
        0x10, 0xce, 0x4f, 0xff,                 // LDS #$4FFF
        0x86, 0x42, 0xb7, 0x44, 0x00,           // LDA #$42; STA $4400 (command)
        0xb6, 0x44, 0x01, 0x85, 0x40, 0x27, 0xf9,   // wait for the reply flag
        0xb6, 0x44, 0x00, 0xb7, 0x48, 0x00,     // reply -> 4800
        0x86, 0x80, 0xb7, 0x46, 0x84,           // bank 1
        0xb6, 0x60, 0x00, 0xb7, 0x48, 0x01,     // 6000 -> 4801
        0xb6, 0x43, 0x00, 0xb7, 0x48, 0x02,     // IN0 -> 4802
        0xb7, 0x46, 0xc1,                       // ADC channel 1
        0xb6, 0x43, 0x80, 0xb7, 0x48, 0x03,     // -> 4803
        0xcc, 0x10, 0x00, 0xfd, 0x47, 0x06,     // dividend 1000
        0xcc, 0x40, 0x00, 0xfd, 0x47, 0x04,     // divisor 4000
        0xfc, 0x47, 0x00, 0xfd, 0x48, 0x04,     // quotient -> 4804
        0xb7, 0x46, 0x40, 0x20, 0xfb,           // kick the watchdog forever
    };
    std::memcpy(rom4, main_prog, sizeof main_prog);
    rom4[0x1ffe] = 0xe0;
    rom4[0x1fff] = 0x00;
    // Sound program at 6000 (also E000): wait for the command, reply + 1.
    const uint8_t snd_prog[] = {
        0x10, 0xce, 0x27, 0xff,                 // LDS #$27FF
        0xb6, 0x10, 0x80, 0x2a, 0xfb,           // LDA $1080; BPL (PA7 = command)
        0xb6, 0x08, 0x00, 0x4c,                 // LDA $0800; INCA
        0xb7, 0x00, 0x00, 0x20, 0xfe,           // STA $0000; BRA *
    };
    std::memcpy(snd1, snd_prog, sizeof snd_prog);
    snd1[0x1ffe] = 0x60;
    snd1[0x1fff] = 0x00;
    static Machine m;
    m.init({{rom0, rom1, rom2, rom3, rom4}, vec, {snd0, snd1}, math}, 24000);
    m.set_inputs(kFire1 | kCoin);
    int16_t audio[1024];
    for (int f = 0; f < 10; ++f) m.run_frame(audio, 1024);
    CHECK_EQ(m.sound_commands(), 1);
    CHECK_EQ(m.cpu().rd(0x4800), 0x43);
    CHECK_EQ(m.cpu().rd(0x4801), 0x5a);
    CHECK_EQ(m.cpu().rd(0x4802), 0xdf & ~0x80 & ~0x02);
    CHECK_EQ(m.cpu().rd(0x4803), 0xa6);         // yoke centred
    CHECK_EQ((m.cpu().rd(0x4804) << 8) | m.cpu().rd(0x4805), 0x1000);
    CHECK_EQ(m.watchdog_resets(), 0);
    // The yoke: right / up raise yaw and lower pitch's reading.
    m.set_yoke(511, -512);
    CHECK_EQ(m.adc(1), 0xa6 + 127 * 0x59 / 127);
    CHECK_EQ(m.adc(0), 0xa6 + 127 * 0x59 / 127);
    m.set_yoke(-512, 511);
    CHECK_EQ(m.adc(1), 0x40);
    CHECK_EQ(m.adc(0), 0x41);                   // 511 / 4 = 127 of 128 down
    m.set_yoke(0, 0);
    m.set_inputs(kRight);
    for (int f = 0; f < 10; ++f) m.run_frame(audio, 1024);
    CHECK_EQ(m.adc(1), 0xa6 + 10 * Machine::kSightSpeed * 0x59 / 127);

    // Without watchdog kicks the board resets: a program that spins.
    static uint8_t rom4b[0x2000];
    rom4b[0] = 0x20;
    rom4b[1] = 0xfe;                            // BRA *
    rom4b[0x1ffe] = 0xe0;
    static Machine w;
    w.init({{rom0, rom1, rom2, rom3, rom4b}, vec, {snd0, snd1}, math}, 24000);
    for (int f = 0; f < 10; ++f) w.run_frame(nullptr, 0);
    CHECK(w.watchdog_resets() >= 2);
}

// The sound CPU's timer wait (7D4C, done by sound_idle without the CPU):
// a hand-made sound program around the game's wait loop, its RIOT timer
// IRQ setting bit 0 of 100B. With the timer due inside the loop's count
// (the loop ends at the LSR) and past it (X runs out: BEQ to 7D44), the
// board must end exactly as without idle skipping.
void test_sound_wait()
{
    using namespace starwars;
    for (uint8_t timer : {uint8_t(4), uint8_t(40)}) {
        static uint8_t rom0[0x4000], rom1[0x2000], rom2[0x2000], rom3[0x2000], rom4[0x2000];
        static uint8_t vec[0x1000], snd0[0x2000], snd1[0x2000], math[0x1000];
        // Main: kick the watchdog forever.
        const uint8_t main_prog[] = {0x10, 0xce, 0x4f, 0xff, 0xb7, 0x46, 0x40, 0x20, 0xfb};
        std::memcpy(rom4, main_prog, sizeof main_prog);
        rom4[0x1ffe] = 0xe0;
        rom4[0x1fff] = 0x00;
        const uint8_t start[] = {
            0x10, 0xce, 0x27, 0xff,             // 6000 LDS #$27FF
            0x86, 0x10, 0x1f, 0x8b,             //      LDA #$10; TFR A,DP
            0x86, timer, 0xb7, 0x10, 0x9f,      //      the timer: n x 1024, IRQ on
            0x86, 0x3f, 0x97, 0x00,             //      LDA #$3F; STA <$00
            0x0f, 0x0b,                         //      CLR <$0B
            0x7e, 0x7d, 0x47,                   //      JMP $7D47
        };
        std::memcpy(snd1, start, sizeof start);
        const uint8_t wait[] = {
            0x7e, 0x7e, 0x27,                   // 7D44 JMP $7E27 (X ran out)
            0x1c, 0xef, 0x8e, 0x01, 0x4d,       // 7D47 ANDCC #$EF; LDX #$014D
            0x96, 0x00, 0x81, 0x3f, 0x26, 0xf2, 0x1f, 0xb8, 0x81, 0x10,     // 7D4C the wait
            0x26, 0xec, 0x30, 0x1f, 0x27, 0xe8, 0x04, 0x0b, 0x24, 0xec,
            0x7c, 0x20, 0x00, 0x7e, 0x7d, 0x47, // 7D60 INC $2000; JMP $7D47
        };
        std::memcpy(snd1 + 0x1d44, wait, sizeof wait);
        const uint8_t out[] = {0x7c, 0x20, 0x01, 0x7e, 0x7d, 0x47};    // 7E27 INC $2001; JMP $7D47
        std::memcpy(snd1 + 0x1e27, out, sizeof out);
        const uint8_t irq[] = {
            0x86, timer, 0xb7, 0x10, 0x9f,      // 7E40 the timer again (clears its flag)
            0x0c, 0x0b, 0x3b,                   //      INC <$0B; RTI
        };
        std::memcpy(snd1 + 0x1e40, irq, sizeof irq);
        snd1[0x1ff8] = 0x7e;
        snd1[0x1ff9] = 0x40;
        snd1[0x1ffe] = 0x60;
        snd1[0x1fff] = 0x00;
        static Machine a, b;
        a.init({{rom0, rom1, rom2, rom3, rom4}, vec, {snd0, snd1}, math}, 24000);
        b.init({{rom0, rom1, rom2, rom3, rom4}, vec, {snd0, snd1}, math}, 24000);
        b.set_idle_skip(false);
        int16_t audio_a[1024], audio_b[1024];
        bool same = true;
        int ran_a = 0, ran_b = 0;
        for (int f = 0; f < 60; ++f) {
            const int na = a.run_frame(audio_a, 1024), nb = b.run_frame(audio_b, 1024);
            same = same && na == nb && std::memcmp(audio_a, audio_b, size_t(na) * 2) == 0;
            ran_a += a.sound_run();
            ran_b += b.sound_run();
        }
        const m6809::Cpu &sa = a.sound_cpu(), &sb = b.sound_cpu();
        CHECK(same);
        CHECK(std::memcmp(a.sound_ram(), b.sound_ram(), 0x800) == 0);
        CHECK_EQ(sa.pc, sb.pc);
        CHECK_EQ(sa.x, sb.x);
        CHECK_EQ(sa.d, sb.d);
        CHECK_EQ(sa.cc, sb.cc);
        CHECK_EQ(sa.s, sb.s);
        // The loop ended the one way or the other, many times.
        CHECK((timer == 4 ? a.sound_ram()[0] : a.sound_ram()[1]) > 20);
        CHECK(ran_a * 10 < ran_b);              // the wait wasn't run
    }
}

} // namespace

int main()
{
    test_tms5220();
    test_matrix();
    test_riot();
    test_avg_starwars();
    test_machine();
    test_sound_wait();
    std::printf("starwars tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
