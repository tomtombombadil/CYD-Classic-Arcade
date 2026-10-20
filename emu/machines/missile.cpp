// Missile Command machine - see missile.h.
//
// Memory map (A15 ignored: 8000-FFFF = 0000-7FFF; a MADSEL access goes to
// the pixel logic wherever it points):
//   0000-3FFF  RAM: 0000-01FF work RAM and stack, 0200-05FF the third
//              colour bits of lines 224-255, 0600-063F work RAM, 0640-3FFF
//              lines 25-255 (64 bytes a line; RAM line y = 64 * y)
//   4000-47FF  POKEY (4008 ALLPOT = the game option switches at R8)
//   4800  read: IN0 (CTRLD = 0) or the trackball (CTRLD = 1: Y count in
//         bits 4-7, X in bits 0-3); write: bit 0 CTRLD, 1-2 start lamps,
//         3-5 coin counters, 6 flip (low = flipped)
//   4900  IN1: 0-2 right / centre / left fire (low = pressed), 3-4 the
//         trackball's direction lines, 5 slam, 6 self-test (low = on), 7
//         VBLANK (high)
//   4A00  the pricing switches (R10)     4B00-4B07  colour RAM (write)
//   4C00  watchdog     4D00  IRQ acknowledge     5000-7FFF  program
// Facts from MAME's driver (read for facts only).
#include "missile.h"

#include "../util/alloc.h"

#include <cstring>

namespace missile {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x3000},
};

const char* const kVariants[] = {"missile", "missile2", "missile1"};

// CRCs as in MAME's "missile" (rev 3, variant 1), "missile2" (rev 2, Tom's:
// one chip differs, variant 2) and "missile1" (rev 1, variant 3).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu, 0x0000, 0x0800, 0x7a62ce6a, "035820-02.h1",   false, 1},
    {kRegionCpu, 0x0800, 0x0800, 0xdf3bd57f, "035821-02.jk1",  false, 1},
    {kRegionCpu, 0x1000, 0x0800, 0x1a2f599a, "035822-03e.kl1", false, 1},
    {kRegionCpu, 0x1800, 0x0800, 0x82e552bb, "035823-02.lm1",  false, 1},
    {kRegionCpu, 0x2000, 0x0800, 0x606e42e0, "035824-02.np1",  false, 1},
    {kRegionCpu, 0x2800, 0x0800, 0xf752eaeb, "035825-02.r1",   false, 1},
    {kRegionCpu, 0x0000, 0x0800, 0x7a62ce6a, "035820-02.h1",   false, 2},
    {kRegionCpu, 0x0800, 0x0800, 0xdf3bd57f, "035821-02.jk1",  false, 2},
    {kRegionCpu, 0x1000, 0x0800, 0xa1cd384a, "035822-02.kl1",  false, 2},
    {kRegionCpu, 0x1800, 0x0800, 0x82e552bb, "035823-02.lm1",  false, 2},
    {kRegionCpu, 0x2000, 0x0800, 0x606e42e0, "035824-02.np1",  false, 2},
    {kRegionCpu, 0x2800, 0x0800, 0xf752eaeb, "035825-02.r1",   false, 2},
    {kRegionCpu, 0x0000, 0x0800, 0x41cbb8f2, "35820-01.h1",    false, 3},
    {kRegionCpu, 0x0800, 0x0800, 0x728702c8, "35821-01.jk1",   false, 3},
    {kRegionCpu, 0x1000, 0x0800, 0x28f0999f, "35822-01.kl1",   false, 3},
    {kRegionCpu, 0x1800, 0x0800, 0xbcc93c94, "35823-01.lm1",   false, 3},
    {kRegionCpu, 0x2000, 0x0800, 0x0ca089c8, "35824-01.np1",   false, 3},
    {kRegionCpu, 0x2800, 0x0800, 0x428cf0d5, "35825-01.r1",    false, 3},
};

// The sound's scale: the POKEY's 0 .. 60 x 256 times this.
constexpr float kGain = 5.0f;

// The snapshot: the RAM from 0200 to 3FFF, then the 8 pens.
constexpr size_t kShotRam = 0x4000 - 0x200;

} // namespace

const romset::Set kRomSet = {
    "missile", nullptr, "Missile Command", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 3,
};

Machine::~Machine()
{
    emu::free(shot_);
}

void Machine::init(const uint8_t* rom, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    rom_ = rom;
    audio_rate_ = audio_rate ? audio_rate : 24000;
    swap_ = swap_rgb565_bytes;
    pokey_.init(kClock, audio_rate_);
    dc_r_ = 1.0f - 6.2831853f * 15.0f / float(audio_rate_);

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.opcode_hook = on_opcode;
    cpu_.map_ram(0x0000, 0x4000, vram_);
    cpu_.map_ram(0x8000, 0x4000, vram_);
    cpu_.map_read(0x5000, 0x3000, rom_);
    cpu_.map_read(0xd000, 0x3000, rom_);
    reset();
}

void Machine::reset()
{
    pokey_.reset();
    pokey_.set_allpot(kGameOptions);
    ctrld_ = false;
    flip_ = false;
    madsel_ = false;
    cpu_.indexed_io = false;
    irq_state_ = irq_pin_ = false;
    watchdog_frames_ = 0;
    cpu_.irq(false);
    cpu_.reset();
}

// SYNC (an opcode fetch): the /IRQ latch takes the 32V flip-flop's state,
// and the latch before it (/IRQH) still holds the old pin. An opcode with
// low bits 00001 while /IRQH is inactive starts MADSEL: its data access
// (the sixth cycle of a (zp,X) instruction) goes to the pixel logic.
uint8_t Machine::on_opcode(void* ctx, uint16_t, uint8_t op, bool)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const bool irqh = m.irq_pin_;
    if (m.irq_pin_ != m.irq_state_) {
        m.irq_pin_ = m.irq_state_;
        m.cpu_.irq(m.irq_pin_);
    }
    if ((op & 0x1f) == 0x01 && !irqh) {
        m.madsel_ = true;
        m.cpu_.indexed_io = true;
        ++m.madsel_count_;
    }
    return op;
}

// A pixel read: its two bits at RAM pix >> 2 (bit X & 3 of either nibble)
// to data bits 7 (high nibble) and 6; for Y >= 224 (pixel addresses
// E000-FFFF) the third bit to bit 5, with an extra clock. Other bits read 1.
uint8_t Machine::pixel_read(uint16_t pix)
{
    const uint8_t b = vram_[pix >> 2] >> (pix & 3);
    uint8_t v = 0xff;
    if (!(b & 0x10)) v &= 0x7f;
    if (!(b & 0x01)) v &= 0xbf;
    if ((pix & 0xe000) == 0xe000) {
        if (!(vram_[bit3_addr(pix)] >> (pix & 7) & 1)) v &= 0xdf;
        cpu_.stall(1);
    }
    return v;
}

// A pixel write: data bits 7 / 6 to the pixel's bits in the high / low
// nibble, bit 5 to the third bit (Y >= 224, an extra clock). The board's
// write-enable PROM picks which of the eight 16K x 1 RAMs take it; that is
// the same bits the read path above reads.
void Machine::pixel_write(uint16_t pix, uint8_t v)
{
    uint8_t& b = vram_[pix >> 2];
    const uint8_t mask = uint8_t(0x11 << (pix & 3));
    const uint8_t bits = uint8_t(((v & 0x80) ? 0xf0 : 0) | ((v & 0x40) ? 0x0f : 0));
    b = uint8_t((b & ~mask) | (bits & mask));
    if ((pix & 0xe000) == 0xe000) {
        uint8_t& b3 = vram_[bit3_addr(pix)];
        const uint8_t m3 = uint8_t(1 << (pix & 7));
        b3 = uint8_t((v & 0x20) ? (b3 | m3) : (b3 & ~m3));
        cpu_.stall(1);
    }
}

uint8_t Machine::in0() const
{
    // Low = pressed: 5 left coin, 6 centre coin, 7 right coin, 4 start 1,
    // 3 start 2; 0-2 the cocktail player's fire buttons (none here).
    uint8_t v = 0xff;
    if (buttons_ & kCoin) v &= uint8_t(~0x20);
    if (buttons_ & kCoin2) v &= uint8_t(~0x40);
    if (buttons_ & kStart1) v &= uint8_t(~0x10);
    if (buttons_ & kStart2) v &= uint8_t(~0x08);
    return v;
}

uint8_t Machine::in1() const
{
    // Low = pressed: 0 right, 1 centre, 2 left fire, 5 slam, 6 self-test;
    // the trackball's direction lines (3, 4) read 0; 7 VBLANK (lines 0-23).
    uint8_t v = 0x67;
    if (buttons_ & kFireRight) v &= uint8_t(~0x01);
    if (buttons_ & kFireCentre) v &= uint8_t(~0x02);
    if (buttons_ & kFireLeft) v &= uint8_t(~0x04);
    if (buttons_ & kTilt) v &= uint8_t(~0x20);
    if (buttons_ & kTest) v &= uint8_t(~0x40);
    if (now() < 24 * kLineTicks) v |= 0x80;
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (m.madsel_) {
        m.madsel_ = false;
        return m.pixel_read(addr);
    }
    switch (addr & 0x7f00) {
    case 0x4800: {
        if (!m.ctrld_) return m.in0();
        // The counters as they stand now: this frame's speed spread over
        // the frame, so reads a quarter frame apart see a quarter of it.
        const int t = m.now();
        const uint32_t x = m.track_x_ + uint32_t(int64_t(m.frame_vx_) * t / kTicksPerFrame);
        const uint32_t y = m.track_y_ + uint32_t(int64_t(m.frame_vy_) * t / kTicksPerFrame);
        return uint8_t((((y >> 8) & 15) << 4) | ((x >> 8) & 15));
    }
    case 0x4900: return m.in1();
    case 0x4a00: return m.r10_;
    default:
        if ((addr & 0x7800) == 0x4000) return m.pokey_.read(uint8_t(addr & 15), m.total_ + uint32_t(m.now()));
        return 0xff;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (m.madsel_) {
        m.madsel_ = false;
        m.pixel_write(addr, v);
        return;
    }
    switch (addr & 0x7f00) {
    case 0x4800:
        m.ctrld_ = v & 1;
        m.flip_ = !(v & 0x40);
        break;
    case 0x4b00: {
        // Bits 3 / 2 / 1 low = red / green / blue on.
        const uint8_t d = uint8_t(~v);
        uint16_t c = uint16_t(((d >> 3) & 1 ? 0xf800 : 0) | ((d >> 2) & 1 ? 0x07e0 : 0) | ((d >> 1) & 1 ? 0x001f : 0));
        if (m.swap_) c = uint16_t((c >> 8) | (c << 8));
        m.pen_[addr & 7] = c;
        break;
    }
    case 0x4c00:
        m.watchdog_frames_ = 0;
        break;
    case 0x4d00:
        m.irq_state_ = false;           // the pin follows at the next opcode fetch
        break;
    default:
        if ((addr & 0x7800) == 0x4000) {
            m.audio_to(m.now());
            m.pokey_.write(uint8_t(addr & 15), v, m.total_ + uint32_t(m.now()));
        }
        break;
    }
}

// Two wait loops, found by their code, whose passes are skipped while
// nothing they look at can change - the CPU then ends the stretch (or
// leaves the loop) on the same instruction, at the same time, as running
// them would. The CPU may stand at either instruction of a loop (a run ends
// where it ends). `left` = cycles to the stretch's end; returns the cycles
// skipped (whole passes).
//  - The main loop waits for the IRQ handler to set bit 0 of a flag: LSR
//    zp; BCC back (500F in rev 2), 8 cycles a pass. While the flag is 0 and
//    the IRQ can't change before the stretch's end (the 32V flip-flop only
//    moves between stretches, and the pin follows it at each opcode
//    fetch), every pass writes 0 back and leaves C = 0, Z = 1, N = 0 (at
//    the BCC, C must be clear: the LSR before it shifted out 0). All but
//    the last pass are skipped.
//  - The IRQ handler at line 0 waits for VBLANK to end: BIT 4900; BMI back
//    (7C70), 7 cycles a pass, reading 4900 at the BIT's start (in1()). The
//    passes whose read still sees VBLANK (before line 24) are skipped; the
//    inputs don't change within a frame, so their flags are alike.
int Machine::idle_cycles(int left)
{
    if (irq_pin_ != irq_state_) return 0;
    uint16_t at = cpu_.pc;
    if ((at & 0x7fff) < 0x5003) return 0;
    const uint8_t op = cpu_.rd(at);
    if ((op == 0x46 || op == 0x90) && !irq_pin_) {
        if (op == 0x90) {
            if (cpu_.p() & m6502::CF) return 0;
            at = uint16_t(at - 2);
        }
        if (cpu_.rd(at) != 0x46 || cpu_.rd(uint16_t(at + 2)) != 0x90 || cpu_.rd(uint16_t(at + 3)) != 0xfc ||
            vram_[cpu_.rd(uint16_t(at + 1))] != 0)
            return 0;
        constexpr int kPass = 8;
        return left > 2 * kPass ? (left / kPass - 1) * kPass : 0;
    }
    if (op == 0x2c || op == 0x30) {
        int read = now();           // the next BIT's start
        if (op == 0x30) {
            if (!(cpu_.p() & m6502::NF)) return 0;
            at = uint16_t(at - 3);
            read += 3 * rate_;
        }
        if (cpu_.rd(at) != 0x2c || cpu_.rd(uint16_t(at + 1)) != 0x00 || cpu_.rd(uint16_t(at + 2)) != 0x49 ||
            cpu_.rd(uint16_t(at + 3)) != 0x30 || cpu_.rd(uint16_t(at + 4)) != 0xfb)
            return 0;
        constexpr int kPass = 7;
        constexpr int kVblankEnd = 24 * kLineTicks;
        if (read >= kVblankEnd) return 0;
        int passes = (kVblankEnd - read + kPass * rate_ - 1) / (kPass * rate_);
        if (passes > left / kPass - 1) passes = left / kPass - 1;
        return passes > 0 ? passes * kPass : 0;
    }
    return 0;
}

void Machine::audio_to(int tick)
{
    if (!audio_) return;
    int target = int(int64_t(tick) * audio_n_ / kTicksPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    int16_t* out = audio_ + audio_done_;
    const int n = target - audio_done_;
    pokey_.render(out, n);
    // One chip at up to 60 x 256, around its middle: AC-coupled (the
    // board's output capacitor), scaled so the loudest game sounds peak
    // near 21,000.
    for (int i = 0; i < n; ++i) {
        const float x = float(out[i]) * kGain;
        dc_y_ = x - dc_x_ + dc_r_ * dc_y_;
        dc_x_ = x;
        if (dc_y_ > -1e-3f && dc_y_ < 1e-3f) dc_y_ = 0;
        const float y = dc_y_ > 32767 ? 32767 : dc_y_ < -32768 ? -32768 : dc_y_;
        out[i] = int16_t(y);
    }
    audio_done_ = target;
}

int Machine::run_frame(int16_t* audio, int cap)
{
    ++frame_;
    audio_rem_ += uint64_t(audio_rate_) * kTicksPerFrame;
    int n = int(audio_rem_ / kClock);
    audio_rem_ %= kClock;
    if (n > cap) n = cap;
    audio_ = audio;
    audio_n_ = audio ? n : 0;
    audio_done_ = 0;

    // The trackball: the stick's speed plus the d-pad's steady one.
    frame_vx_ = track_vx_;
    frame_vy_ = track_vy_;
    if (buttons_ & kRight) frame_vx_ += kButtonSpeed;
    if (buttons_ & kLeft) frame_vx_ -= kButtonSpeed;
    if (buttons_ & kUp) frame_vy_ += kButtonSpeed;
    if (buttons_ & kDown) frame_vy_ -= kButtonSpeed;

    // Eight stretches of 32 lines: the 32V flip-flop sets the IRQ at lines
    // 0, 64, 128, 192 and clears it at 32, 96, 160, 224 (the CPU's write to
    // 4D00 clears it sooner); from line 224 the CPU runs at half speed.
    for (int v = 0; v < 256; v += 32) {
        irq_state_ = !(v & 32);
        rate_ = v >= kSlowLine ? 2 : 1;
        const int end = (v + 32) * kLineTicks;
        while (tick_ < end) {
            int left = (end - tick_ + rate_ - 1) / rate_;
            if (idle_skip_) {
                const int skip = idle_cycles(left);
                idle_skipped_ += uint32_t(skip);
                tick_ += skip * rate_;
                left -= skip;
            }
            tick_ += cpu_.run(left < kSlice ? left : kSlice) * rate_;
        }
    }
    audio_to(kTicksPerFrame);
    audio_ = nullptr;
    tick_ -= kTicksPerFrame;
    total_ += kTicksPerFrame;
    track_x_ += uint32_t(frame_vx_);
    track_y_ += uint32_t(frame_vy_);

    if (++watchdog_frames_ > kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
    return audio ? n : 0;
}

void Machine::draw_line(const uint8_t* ram, const uint16_t* pen, int y, uint16_t* out)
{
    // Pixel X of a byte: bit X & 3 of the low nibble = colour bit 1, of the
    // high nibble = colour bit 2; colour bit 0 (lines 224-255 only) = bit X
    // & 7 of every other byte from the line's bit3_addr().
    const int line = y + kFirstLine;
    const uint8_t* src = ram + line * 64 - 0x200;
    if (line < kSlowLine) {
        for (int i = 0; i < 64; ++i, out += 4) {
            const unsigned b = src[i];
            out[0] = pen[((b >> 2) & 4) | ((b << 1) & 2)];
            out[1] = pen[((b >> 3) & 4) | (b & 2)];
            out[2] = pen[((b >> 4) & 4) | ((b >> 1) & 2)];
            out[3] = pen[((b >> 5) & 4) | ((b >> 2) & 2)];
        }
        return;
    }
    const uint8_t* third = ram + bit3_addr(uint16_t(line << 8)) - 0x200;
    for (int i = 0; i < 64; ++i, out += 4) {
        const unsigned b = src[i];
        const unsigned t = unsigned(third[i & ~1]) >> ((i & 1) * 4);
        out[0] = pen[((b >> 2) & 4) | ((b << 1) & 2) | (t & 1)];
        out[1] = pen[((b >> 3) & 4) | (b & 2) | ((t >> 1) & 1)];
        out[2] = pen[((b >> 4) & 4) | ((b >> 1) & 2) | ((t >> 2) & 1)];
        out[3] = pen[((b >> 5) & 4) | ((b >> 2) & 2) | ((t >> 3) & 1)];
    }
}

bool Machine::snapshot()
{
    if (!shot_) {
        shot_ = static_cast<uint8_t*>(emu::alloc(kShotRam + sizeof pen_));
        if (!shot_) return false;
    }
    std::memcpy(shot_, vram_ + 0x200, kShotRam);
    std::memcpy(shot_ + kShotRam, pen_, sizeof pen_);
    return true;
}

void Machine::render_snapshot_line(int y, uint16_t* out) const
{
    uint16_t pen[8];
    std::memcpy(pen, shot_ + kShotRam, sizeof pen);
    draw_line(shot_, pen, y, out);
}

} // namespace missile
