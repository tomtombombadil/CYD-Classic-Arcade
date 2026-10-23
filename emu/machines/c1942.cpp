// 1942 machine - see c1942.h.
//
// Main board (unmapped reads = FF):
//   0000-7FFF  ROM (M3 M4)
//   8000-BFFF  ROM bank, C806 bits 0-1: 0 = M5, 1 = M6 (8 KB, A000-BFFF
//              empty), 2 = M7, 3 = empty
//   C000-C004  read: SYSTEM, P1, P2, DSWA, DSWB (active low)
//   C800/C801  write: sound commands (latches the sound CPU reads at
//              6000 / 6001)
//   C802-C803  background scroll (9 bits)
//   C804       bit 7 flip screen (cocktail, not drawn), bit 4 sound CPU
//              reset (held while set), bit 0 coin counter
//   C805       background palette bank (bits 0-1)
//   C806       ROM bank
//   CC00-CC7F  sprites (write only)
//   D000-D7FF  characters: codes, then attributes (bit 7 = code bit 8,
//              bits 0-5 colour)
//   D800-DBFF  background: per tile column 16 codes then 16 attributes
//              (bit 7 code bit 8, bit 6 flip Y, bit 5 flip X, bits 0-4
//              colour)
//   E000-EFFF  RAM
// Sound board: 0000-3FFF ROM, 4000-47FF RAM, 6000 / 6001 the latches,
// 8000 / 8001 AY 1 address / data, C000 / C001 AY 2.
#include "c1942.h"

#include <cmath>
#include <cstring>

namespace c1942 {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"main0",   0x4000},
    {"main1",   0x4000},
    {"bank0",   0x4000},
    {"bank1",   0x2000},
    {"bank2",   0x4000},
    {"audiocpu", 0x4000},
    {"gfx1",    0x2000},
    {"gfx2",    0xc000},
    {"gfx3",    0x10000},
    {"proms",   kPromSize},
};

const char* const kVariants[] = {"1942", "1942a", "1942b"};

// CRCs as in MAME's "1942" (Revision B), "1942a" (Revision A - Tom's:
// 1-n3a.bin ... 1-n7.bin) and "1942b" (first version). The other chips are
// the same in all three. sb-2, sb-3 (tile palette selectors) and sb-9
// (video timing) aren't needed.
constexpr romset::Rom kRoms[] = {
    {kRegionMain0, 0, 0x4000, 0xd9dafcc3, "srb-03.m3", false, 1},
    {kRegionMain1, 0, 0x4000, 0xda0cf924, "srb-04.m4", false, 1},
    {kRegionBank0, 0, 0x4000, 0xd102911c, "srb-05.m5", false, 1},
    {kRegionBank1, 0, 0x2000, 0x466f8248, "srb-06.m6", false, 1},
    {kRegionBank2, 0, 0x4000, 0x0d31038c, "srb-07.m7", false, 1},
    {kRegionMain0, 0, 0x4000, 0x40201bab, "sra-03.m3", false, 2},
    {kRegionMain1, 0, 0x4000, 0xa60ac644, "sr-04.m4", false, 2},
    {kRegionBank0, 0, 0x4000, 0x835f7b24, "sr-05.m5", false, 2},
    {kRegionBank1, 0, 0x2000, 0x821c6481, "sr-06.m6", false, 2},
    {kRegionBank2, 0, 0x4000, 0x5df525e1, "sr-07.m7", false, 2},
    {kRegionMain0, 0, 0x4000, 0x612975f2, "sr-03.m3", false, 3},
    {kRegionMain1, 0, 0x4000, 0xa60ac644, "sr-04.m4", false, 3},
    {kRegionBank0, 0, 0x4000, 0x835f7b24, "sr-05.m5", false, 3},
    {kRegionBank1, 0, 0x2000, 0x821c6481, "sr-06.m6", false, 3},
    {kRegionBank2, 0, 0x4000, 0x5df525e1, "sr-07.m7", false, 3},
    {kRegionSound,   0x0000, 0x4000, 0xbd87f06b, "sr-01.c11", false},
    {kRegionChars,   0x0000, 0x2000, 0x6ebca191, "sr-02.f2", false},
    {kRegionTiles,   0x0000, 0x2000, 0x3884d9eb, "sr-08.a1", false},
    {kRegionTiles,   0x2000, 0x2000, 0x999cf6e0, "sr-09.a2", false},
    {kRegionTiles,   0x4000, 0x2000, 0x8edb273a, "sr-10.a3", false},
    {kRegionTiles,   0x6000, 0x2000, 0x3a2726c3, "sr-11.a4", false},
    {kRegionTiles,   0x8000, 0x2000, 0x1bd3d8bb, "sr-12.a5", false},
    {kRegionTiles,   0xa000, 0x2000, 0x658f02c4, "sr-13.a6", false},
    {kRegionSprites, 0x0000, 0x4000, 0x2528bec6, "sr-14.l1", false},
    {kRegionSprites, 0x4000, 0x4000, 0xf89287aa, "sr-15.l2", false},
    {kRegionSprites, 0x8000, 0x4000, 0x024418f8, "sr-16.n1", false},
    {kRegionSprites, 0xc000, 0x4000, 0xe2c7e489, "sr-17.n2", false},
    {kRegionProms, kPromChars,   0x100, 0x6047d91b, "sb-0.f1", false},
    {kRegionProms, kPromIrq,     0x100, 0x712ac508, "sb-1.k6", false},
    {kRegionProms, kPromTiles,   0x100, 0x4858968d, "sb-4.d6", false},
    {kRegionProms, kPromRed,     0x100, 0x93ab8153, "sb-5.e8", false},
    {kRegionProms, kPromGreen,   0x100, 0x8ab44f7d, "sb-6.e9", false},
    {kRegionProms, kPromBlue,    0x100, 0xf4ade9a4, "sb-7.e10", false},
    {kRegionProms, kPromSprites, 0x100, 0xf6fad943, "sb-8.k3", false},
};

// The six AY channels go through 220k each into the volume pot, AC-coupled
// (10 uF, far below hearing): an even mix. A 20 Hz coupling (as the other
// boards here) and a level for small speakers: play peaks ~19,500.
constexpr float kDcHz = 20.0f;
constexpr float kOutGain = 1.6f;

// Run length between checks for a CPU coming back into its wait loop.
constexpr int kSkipChunk = 1024;

uint16_t rgb565(int r, int g, int b, bool swap)
{
    const uint16_t v = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return swap ? uint16_t((v >> 8) | (v << 8)) : v;
}

// kSpread[b]: byte i = bit 7 - i of b (a bit plane's 8 pixels, the left
// one first, one a byte).
constexpr uint64_t spread(int b)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        if (b & (0x80 >> i)) v |= uint64_t(1) << (8 * i);
    return v;
}
#define C1942_S4(n) spread(n), spread(n + 1), spread(n + 2), spread(n + 3)
#define C1942_S16(n) C1942_S4(n), C1942_S4(n + 4), C1942_S4(n + 8), C1942_S4(n + 12)
#define C1942_S64(n) C1942_S16(n), C1942_S16(n + 16), C1942_S16(n + 32), C1942_S16(n + 48)
constexpr uint64_t kSpread[256] = {C1942_S64(0), C1942_S64(64), C1942_S64(128), C1942_S64(192)};
#undef C1942_S64
#undef C1942_S16
#undef C1942_S4

// A colour PROM nibble through 2.2k / 1k / 470 / 220 ohm (bit 0 .. 3).
int gun(uint8_t nibble)
{
    constexpr float g[4] = {1 / 2200.0f, 1 / 1000.0f, 1 / 470.0f, 1 / 220.0f};
    constexpr float sum = g[0] + g[1] + g[2] + g[3];
    float v = 0;
    for (int b = 0; b < 4; ++b)
        if (nibble & (1 << b)) v += g[b];
    return int(v / sum * 255.0f + 0.5f);
}

} // namespace

namespace {

constexpr uint8_t kMainLoop[] = {0xfb, 0x21, 0xc0, 0xef, 0x01, 0x00, 0x10, 0x11, 0x04, 0x00, 0x7e, 0xfe,
                                 0x20, 0x30, 0x06, 0x0c, 0x19, 0x10, 0xf7, 0x18, 0xeb};   // at 0087
constexpr uint16_t kMainLoopPc = 0x0087;
constexpr int kMainPass = 825;
constexpr uint8_t kSoundLoop[] = {0xfb, 0x18, 0xfd};                                   // at 0064
constexpr uint16_t kSoundLoopPc = 0x0064;
constexpr int kSoundPass = 16;

struct Regs {
    uint16_t r[12];
    uint8_t i, iff1, iff2, im;
    bool operator==(const Regs& o) const
    {
        return std::memcmp(r, o.r, sizeof r) == 0 && i == o.i && iff1 == o.iff1 && iff2 == o.iff2 && im == o.im;
    }
};

Regs regs(const z80::Cpu& c)
{
    return {{c.af.w, c.bc.w, c.de.w, c.hl.w, c.ix.w, c.iy.w, c.sp.w, c.pc.w, c.af2.w, c.bc2.w, c.de2.w, c.hl2.w},
            c.i, c.iff1, c.iff2, c.im};
}

// See above: the CPU, at an instruction of a pure wait loop with no
// interrupt to come before `target`, runs a pass (twice at most, the first
// may still hold values from before an interrupt); a pass that leaves
// every register as it found them repeats until the loop is left, so the
// whole passes still to come are skipped. False = too little time to try.
bool skip_passes(z80::Cpu& c, int& done, int target, int pass, uint64_t& skipped)
{
    if (target - done < 3 * pass) return false;
    for (int tries = 0; tries < 2; ++tries) {
        const Regs before = regs(c);
        const uint8_t r0 = c.r;
        const int n = c.run(pass);
        done += n;
        if (n != pass || !(regs(c) == before)) continue;
        const int k = (target - done) / pass;
        if (k > 0) {
            done += k * pass;
            c.r = uint8_t(c.r + k * uint8_t(c.r - r0));
            skipped += uint64_t(k) * uint64_t(pass);
        }
        return true;
    }
    return true;
}

} // namespace

const romset::Set kRomSet = {
    "1942", nullptr, "1942", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 3,
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = no_in;
    cpu_.port_out = no_out;
    cpu_.map_read(0x0000, 0x4000, roms.main0);
    cpu_.map_read(0x4000, 0x4000, roms.main1);
    cpu_.map_ram(0xd000, 0x800, fg_);
    cpu_.map_ram(0xd800, 0x400, bg_);
    cpu_.map_ram(0xe000, 0x1000, ram_);

    snd_.ctx = this;
    snd_.mem_read = snd_mem_read;
    snd_.mem_write = snd_mem_write;
    snd_.port_in = no_in;
    snd_.port_out = no_out;
    snd_.map_read(0x0000, 0x4000, roms.sound);
    snd_.map_ram(0x4000, 0x800, snd_ram_);

    const uint8_t* p = roms.proms;
    for (int i = 0; i < 256; ++i)
        palette_[i] = rgb565(gun(p[kPromRed + i] & 15), gun(p[kPromGreen + i] & 15), gun(p[kPromBlue + i] & 15),
                             swap_rgb565_bytes);
    for (int i = 0; i < 256; ++i) {
        char_pens_[i] = palette_[0x80 | (p[kPromChars + i] & 15)];
        spr_pens_[i] = palette_[0x40 | (p[kPromSprites + i] & 15)];
        tile_lut_[i] = uint8_t(p[kPromTiles + i] & 15);
    }

    // The timing PROM, indexed by the line counter's low 8 bits: bit 3 = an
    // RST for the main CPU (bits 0-1 pick RST 00 / 08 / 10 / 18), bit 2 =
    // the sound CPU's IRQ. VBLANK starts at line 240 whatever it says.
    event_count_ = 0;
    bool vblank = false;
    for (int line = 0; line < kLinesPerFrame && event_count_ < 7; ++line) {
        const uint8_t v = uint8_t(p[kPromIrq + (line & 0xff)] & 15);
        if (!(v & 0x0c) && line != kVblankLine) continue;
        if (line > kVblankLine && !vblank) {
            events_[event_count_++] = {int16_t(kVblankLine), 0, false};
            vblank = true;
        }
        if (line == kVblankLine) vblank = true;
        events_[event_count_++] = {int16_t(line), uint8_t((v & 8) ? 0xc7 | ((v << 3) & 0x18) : 0), (v & 4) != 0};
    }
    if (!vblank) events_[event_count_++] = {int16_t(kVblankLine), 0, false};

    // Which pixel columns of each character draw anything
    for (int c = 0; c < 512; ++c) {
        uint8_t m = 0;
        for (int py = 0; py < 8; ++py) {
            const uint8_t lo = roms.chars[c * 16 + py * 2], hi = roms.chars[c * 16 + py * 2 + 1];
            for (int k = 0; k < 4; ++k) {
                if ((lo >> (7 - k) | lo >> (3 - k)) & 1) m |= uint8_t(1 << k);
                if ((hi >> (7 - k) | hi >> (3 - k)) & 1) m |= uint8_t(1 << (4 + k));
            }
        }
        char_cols_[c] = m;
    }

    main_wait_ok_ = std::memcmp(roms.main0 + kMainLoopPc, kMainLoop, sizeof kMainLoop) == 0;
    snd_wait_ok_ = std::memcmp(roms.sound + kSoundLoopPc, kSoundLoop, sizeof kSoundLoop) == 0;

    dc_r_ = std::exp(-2.0f * 3.14159265f * kDcHz / float(audio_rate));
    strip_col_ = -1;
    reset();
}

void Machine::reset()
{
    cpu_.reset();
    snd_.reset();
    ay_[0].reset();
    ay_[1].reset();
    cpu_.irq(false);
    snd_.irq(false);
    main_irq_ = snd_irq_ = false;
    snd_reset_ = false;
    latch_[0] = latch_[1] = 0;
    scroll_[0] = scroll_[1] = 0;
    control_ = 0;
    palette_bank_ = 0;
    set_bank(0);
    main_done_ = snd_done_ = 0;
}

void Machine::set_bank(uint8_t v)
{
    bank_ = uint8_t(v & 3);
    const uint8_t* b = bank_ < 3 ? roms_.bank[bank_] : nullptr;
    if (bank_ == 1) {
        cpu_.map_read(0x8000, 0x2000, b);
        cpu_.map_read(0xa000, 0x2000, nullptr);
    } else {
        cpu_.map_read(0x8000, 0x4000, b);
    }
}

void Machine::control(uint8_t v)
{
    if ((v ^ control_) & 0x10) {
        sound_to(now());
        if (v & 0x10) {             // held in reset: starts again at 0 when let go
            snd_reset_ = true;
            snd_.reset();
            snd_irq_ = false;
            snd_.irq(false);
        } else {
            snd_reset_ = false;
        }
    }
    control_ = v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    const uint16_t b = m.buttons_;
    uint8_t v = 0xff;
    switch (addr) {
    case 0xc000:
        if (b & kStart1)  v &= uint8_t(~0x01);
        if (b & kStart2)  v &= uint8_t(~0x02);
        if (b & kService) v &= uint8_t(~0x10);
        if (b & kCoin2)   v &= uint8_t(~0x40);
        if (b & kCoin)    v &= uint8_t(~0x80);
        return v;
    case 0xc001:
        if (b & kRight) v &= uint8_t(~0x01);
        if (b & kLeft)  v &= uint8_t(~0x02);
        if (b & kDown)  v &= uint8_t(~0x04);
        if (b & kUp)    v &= uint8_t(~0x08);
        if (b & kFire)  v &= uint8_t(~0x10);
        if (b & kLoop)  v &= uint8_t(~0x20);
        return v;
    case 0xc003:
        return uint8_t((m.dips_ & 0xf0) | 0x07);   // upright (bit 3 clear), 1 coin 1 credit
    default:
        return 0xff;                                // P2 (cocktail), DSWB defaults, open bus
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xff80) == 0xcc00) {
        m.spr_[addr & 0x7f] = v;
        return;
    }
    switch (addr) {
    case 0xc800:
    case 0xc801:
        m.sound_to(m.now());
        m.latch_[addr & 1] = v;
        break;
    case 0xc802: m.scroll_[0] = v; break;
    case 0xc803: m.scroll_[1] = v; break;
    case 0xc804: m.control(v); break;
    case 0xc805: m.palette_bank_ = uint8_t(v & 3); break;
    case 0xc806: m.set_bank(v); break;
    default: break;
    }
}

// --- sound board -----------------------------------------------------------------

uint8_t Machine::snd_mem_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (addr == 0x6000 || addr == 0x6001) return m.latch_[addr & 1];
    return 0xff;
}

void Machine::snd_mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr != 0x8000 && addr != 0x8001 && addr != 0xc000 && addr != 0xc001) return;
    ay8910::Chip& ay = m.ay_[(addr & 0x4000) ? 1 : 0];
    if (addr & 1) {
        m.audio_to(m.sound_now());
        ay.write_data(v);
    } else {
        ay.write_address(v);
    }
}

// Both CPUs keep a raised IRQ until they take it (HOLD_LINE): while one is
// raised the CPU runs an instruction at a time, and the line drops when it
// has pushed its PC with interrupts going off.
void Machine::main_to(int cycle)
{
    while (main_done_ < cycle) {
        if (main_irq_) {
            const bool ready = cpu_.iff1 != 0;
            const uint16_t sp = cpu_.sp.w;
            main_done_ += cpu_.run(1);
            if (ready && !cpu_.iff1 && cpu_.sp.w == uint16_t(sp - 2)) {
                main_irq_ = false;
                cpu_.irq(false);
            }
            continue;
        }
        if (idle_skip_ && main_wait_ok_) {
            if (skip_main_wait(cycle)) continue;
            // Short runs, so the CPU is caught when it comes back into the
            // wait (the same instructions as one long run)
            const int left = cycle - main_done_;
            main_done_ += cpu_.run(left < kSkipChunk ? left : kSkipChunk);
            continue;
        }
        main_done_ += cpu_.run(cycle - main_done_);
    }
}

void Machine::sound_to(int cycle)
{
    if (cycle > kCyclesPerFrame) cycle = kCyclesPerFrame;
    if (snd_reset_) {
        if (snd_done_ < cycle) snd_done_ = cycle;
        return;
    }
    while (snd_done_ < cycle) {
        if (snd_irq_) {
            const bool ready = snd_.iff1 != 0;
            const uint16_t sp = snd_.sp.w;
            snd_done_ += snd_.run(1);
            if (ready && !snd_.iff1 && snd_.sp.w == uint16_t(sp - 2)) {
                snd_irq_ = false;
                snd_.irq(false);
            }
            continue;
        }
        if (idle_skip_ && snd_wait_ok_) {
            if (skip_sound_wait(cycle)) continue;
            const int left = cycle - snd_done_;
            snd_done_ += snd_.run(left < kSkipChunk ? left : kSkipChunk);
            continue;
        }
        snd_done_ += snd_.run(cycle - snd_done_);
    }
}

// --- idle skipping (exact) ---------------------------------------------------------
//
// Main CPU, the task loop at 0087 (all three program revisions): EI, then
// for the 16 task slots at EFC0 (4 bytes apart) LD A,(HL) / CP 20 / JR NC
// (a task) / INC C / ADD HL,DE / DJNZ, JR 0087 - 825 cycles a pass that
// writes nothing. While every slot holds less than 20 and no interrupt is
// raised, it can't leave the loop. Sound CPU, after its start-up: EI / JR
// to the EI (0064), 16 cycles a pass, until its IRQ.
//
// From any instruction of such a loop the CPU runs a pass itself (two at
// most); when one leaves every register as it found them, the passes to
// come until the run's end are the same: the whole ones are skipped, their
// time and R counted as if run, and the CPU runs the rest of the last.

bool Machine::skip_main_wait(int target)
{
    const uint16_t pc = cpu_.pc.w;
    if (main_irq_ || pc < kMainLoopPc || pc >= kMainLoopPc + sizeof kMainLoop) return false;
    for (int s = 0; s < 16; ++s)
        if (ram_[0xfc0 + 4 * s] >= 0x20) return false;
    return skip_passes(cpu_, main_done_, target, kMainPass, skipped_);
}

bool Machine::skip_sound_wait(int target)
{
    const uint16_t pc = snd_.pc.w;
    if (snd_irq_ || pc < kSoundLoopPc || pc >= kSoundLoopPc + sizeof kSoundLoop) return false;
    return skip_passes(snd_, snd_done_, target, kSoundPass, skipped_);
}

// --- audio -------------------------------------------------------------------------

int16_t Machine::mix_sample(uint32_t clocks)
{
    const float scale = clocks ? 1.0f / float(clocks) : 0;
    float sum = 0;
    for (int c = 0; c < 6; ++c) sum += float(acc_[c]);
    sum *= scale;
    const float out = dc_r_ * (dc_y_ + sum - dc_x_);
    dc_x_ = sum;
    dc_y_ = out;
    // A soft knee above 16,000, so a pile-up of sounds never clips.
    float s = out * kOutGain;
    if (s > 16000.0f) s = 16000.0f + (s - 16000.0f) / (1.0f + (s - 16000.0f) / 5500.0f);
    if (s < -16000.0f) s = -16000.0f + (s + 16000.0f) / (1.0f - (s + 16000.0f) / 5500.0f);
    return int16_t(s);
}

// The AYs count at half the sound CPU's clock.
void Machine::audio_to(int snd_cycle)
{
    if (snd_cycle > kCyclesPerFrame) snd_cycle = kCyclesPerFrame;
    while (audio_clock_ < snd_cycle) {
        const int end = audio_done_ < audio_n_ ? int(int64_t(audio_done_ + 1) * kCyclesPerFrame / audio_n_)
                                               : kCyclesPerFrame;
        const int to = snd_cycle < end ? snd_cycle : end;
        const uint32_t clocks = uint32_t(((snd_base_ + uint64_t(to)) >> 1) - ((snd_base_ + uint64_t(audio_clock_)) >> 1));
        if (clocks) {
            ay_[0].run(clocks, acc_);
            ay_[1].run(clocks, acc_ + 3);
        }
        audio_clock_ = to;
        if (to == end && audio_done_ < audio_n_) {
            const uint32_t span = uint32_t(((snd_base_ + uint64_t(end)) >> 1) - ((snd_base_ + uint64_t(sample_start_)) >> 1));
            const int16_t s = mix_sample(span);
            if (audio_) audio_[audio_done_] = s;
            ++audio_done_;
            sample_start_ = end;
            for (uint32_t& a : acc_) a = 0;
        }
    }
}

// --- frame -------------------------------------------------------------------------

int Machine::run_frame(int16_t* audio, int cap)
{
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = n > 0 ? n : 0;
    audio_done_ = 0;
    audio_clock_ = 0;
    sample_start_ = 0;
    for (uint32_t& a : acc_) a = 0;

    for (int e = 0; e < event_count_; ++e) {
        const Event& ev = events_[e];
        const int at = ev.line * kCyclesPerLine;
        main_to(at);
        sound_to(at);
        if (ev.line == kVblankLine) {
            std::memcpy(state_.fg, fg_, sizeof fg_);
            std::memcpy(state_.bg, bg_, sizeof bg_);
            std::memcpy(state_.spr, spr_, sizeof spr_);
            state_.scroll = uint16_t((scroll_[0] | (scroll_[1] << 8)) & 0x1ff);
            state_.palette_bank = palette_bank_;
        }
        if (ev.main_vector) {
            main_irq_ = true;
            cpu_.irq(true, ev.main_vector);
            ++main_irqs_;
        }
        if (ev.sound && !snd_reset_) {
            snd_irq_ = true;
            snd_.irq(true, 0xff);
            ++snd_irqs_;
        }
    }
    main_to(kCyclesPerFrame);
    sound_to(kCyclesPerFrame);
    audio_to(kCyclesPerFrame);
    ++frame_;

    main_done_ -= kCyclesPerFrame;
    snd_done_ -= kCyclesPerFrame;
    snd_base_ += kCyclesPerFrame;
    const int done = audio_ ? audio_n_ : 0;
    audio_ = nullptr;
    return done;
}

// --- video -------------------------------------------------------------------------

// One background tile column (16 pixels wide) over the shown lines 16-239
// (tile rows 1-14): palette indices, strip_[pixel column][line - 16].
void Machine::decode_strip(const VideoState& vs, int col) const
{
    const uint8_t* c = vs.bg + col * 32;
    const uint8_t bankbits = uint8_t(vs.palette_bank << 4);
    for (int r = 1; r < 15; ++r) {
        const uint8_t attr = c[16 + r];
        const int code = c[r] | ((attr & 0x80) << 1);
        // The same tile higher in the column (open sea): copied
        int same = 0;
        for (int q = 1; q < r && !same; ++q)
            if (c[q] == c[r] && c[16 + q] == attr) same = q;
        if (same) {
            for (int tx = 0; tx < 16; ++tx) std::memcpy(&strip_[tx][(r - 1) * 16], &strip_[tx][(same - 1) * 16], 16);
            continue;
        }
        const uint8_t* lut = tile_lut_ + (attr & 0x1f) * 8;
        const uint8_t* p2 = roms_.tiles + code * 32;          // bit 2
        const uint8_t* p1 = p2 + 0x4000;                      // bit 1
        const uint8_t* p0 = p2 + 0x8000;                      // bit 0
        const bool fx = attr & 0x20, fy = attr & 0x40;
        for (int ty = 0; ty < 16; ++ty) {
            const int sr = fy ? 15 - ty : ty;
            const int at = (r - 1) * 16 + ty;
            for (int h = 0; h < 2; ++h) {
                // 8 pixels: byte i = pixel i's 3 bits (left pixel first)
                const int src = fx ? 1 - h : h;
                uint64_t v8 = kSpread[p2[src * 16 + sr]] << 2 | kSpread[p1[src * 16 + sr]] << 1 |
                              kSpread[p0[src * 16 + sr]];
                if (fx) v8 = __builtin_bswap64(v8);
                for (int i = 0; i < 8; ++i, v8 >>= 8) strip_[h * 8 + i][at] = uint8_t(bankbits | lut[v8 & 7]);
            }
        }
    }
    std::memcpy(strip_key_, c, 32);
    strip_key_[32] = vs.palette_bank;
    strip_col_ = col;
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    if (y < 0 || y >= kScreenH) return;
    const int x = 255 - y;      // the board's pixel column this display line shows

    // Background
    const int mx = (x + vs.scroll) & 511;
    const int col = mx >> 4;
    const uint8_t* key = vs.bg + col * 32;
    if (strip_col_ != col || strip_key_[32] != vs.palette_bank || std::memcmp(strip_key_, key, 32) != 0)
        decode_strip(vs, col);
    const uint8_t* s = strip_[mx & 15];
    for (int i = 0; i < kScreenW; ++i) out[i] = palette_[s[i]];

    // Sprites: slot 0 on top (drawn last); 16-23 only on lines up to 128,
    // 24-31 only from 129.
    for (int i = 31; i >= 0; --i) {
        const uint8_t* o = vs.spr + i * 4;
        const int sx = o[3] - ((o[1] & 0x10) << 4);
        const int cx = x - sx;
        if (unsigned(cx) > 15) continue;
        const int vlen = o[1] >> 6;
        const int rows = vlen == 3 ? 16 : 1 << vlen;
        const int code = ((o[0] & 0x7f) | ((o[1] & 0x20) << 2) | ((o[0] & 0x80) << 1)) & ~(rows - 1);
        const int sy = o[2];
        int y0 = sy, y1 = sy + rows * 16 - 1;
        if (y0 < 16) y0 = 16;
        if (y1 > 239) y1 = 239;
        if (i >= 24 && y0 < 129) y0 = 129;
        else if (i >= 16 && i < 24 && y1 > 128) y1 = 128;
        if (y0 > y1) continue;
        const uint16_t* pens = spr_pens_ + (o[1] & 15) * 16;
        const int k = cx & 3;
        const int colofs = ((cx & 8) ? 32 : 0) + ((cx >> 2) & 1);
        for (int ly = y0; ly <= y1; ++ly) {
            const int r = ly - sy;
            const int base = ((code + (r >> 4)) & 511) * 64 + colofs + (r & 15) * 2;
            const uint8_t lo = roms_.sprites[base], hi = roms_.sprites[0x8000 + base];
            const int v = ((lo >> (7 - k)) & 1) | ((lo >> (3 - k)) & 1) << 1 | ((hi >> (7 - k)) & 1) << 2 |
                          ((hi >> (3 - k)) & 1) << 3;
            if (v != 15) out[ly - 16] = pens[v];
        }
    }

    // Characters (rows 2-29 are shown), pen 0 clear
    const int ccol = x >> 3, k = x & 3;
    const int half = (x >> 2) & 1;
    for (int crow = 2; crow < 30; ++crow) {
        const int idx = crow * 32 + ccol;
        const uint8_t attr = vs.fg[0x400 + idx];
        const int code = vs.fg[idx] | ((attr & 0x80) << 1);
        if (!(char_cols_[code] & (1 << (x & 7)))) continue;     // blank in this column
        const uint8_t* d = roms_.chars + code * 16 + half;
        const uint16_t* pens = char_pens_ + (attr & 0x3f) * 4;
        uint16_t* o = out + crow * 8 - 16;
        for (int py = 0; py < 8; ++py) {
            const uint8_t b = d[py * 2];
            const int v = ((b >> (7 - k)) & 1) | ((b >> (3 - k)) & 1) << 1;
            if (v) o[py] = pens[v];
        }
    }
}

} // namespace c1942
