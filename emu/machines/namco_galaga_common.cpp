// Namco Galaga-family CPU board - see namco_galaga_common.h.
//
// Memory map, the same on all three CPUs (A10 of the 1 KB RAMs isn't
// decoded on the board, but nothing uses the mirrors, so they are left out):
//   0000-3FFF  the CPU's own program ROM (4 / 8 / 16 KB; the rest open)
//   6800-6FFF  write: A5-A4 = 0/1 WSG registers (A4-A0), 2 latch (bit =
//              A2-A0: 0 main IRQ enable / ack, 1 sub IRQ enable / ack,
//              2 sound NMI enable (active low), 3 sub CPUs + custom chips
//              out of reset, 4-7 per game), 3 watchdog reset
//              read: per game (Galaga's DIP switches)
//   7000-77FF  06XX: A8 = 0 data, 1 control
//   8000-87FF  video RAM               8800-8BFF  RAM 1 (sprite codes at 8B80)
//   9000-93FF  RAM 2 (sprite x / y)    9800-9BFF  RAM 3 (sprite flags)
//   A000-...   per game (video latch, Dig Dug's EAROM)
#include "namco_galaga_common.h"

namespace namco_galaga {

namespace {

// Slice length in CPU cycles: the CPUs see each other's RAM writes at
// most this late (MAME's driver uses 512).
constexpr int kSlice = 256;

// When the sound CPU's NMIs, VBLANK and the frame's end fall.
constexpr int kEventCycles[] = {64 * kCyclesPerLine, 192 * kCyclesPerLine, kVblankLine * kCyclesPerLine,
                                kCyclesPerFrame};
enum Event { kNmi64, kNmi192, kVblank, kFrameEnd, kEventCount };

// The watchdog resets the board after 8 frames without a write to 6830.
constexpr int kWatchdogFrames = 8;

// Palette PROM bits -> 8-bit levels through the resistor DAC (conductance
// shares; as on Pac-Man).
constexpr uint8_t kRGWeights[3] = {33, 71, 151};
constexpr uint8_t kBWeights[2] = {81, 174};

int bit_at(const uint8_t* base, int bit) { return (base[bit >> 3] >> (7 - (bit & 7))) & 1; }

// A 2bpp pixel: the two planes of 4 pixels share a byte, plane 0 (the high
// bit of the pixel value) in the upper nibble.
int pixel_at(const uint8_t* base, int bit) { return (bit_at(base, bit) << 1) | bit_at(base, bit + 4); }

} // namespace

// --- the board -----------------------------------------------------------------

void Board::board_init(const BoardRoms& roms, uint32_t audio_rate, const Speedup* speedups, int speedup_count)
{
    roms_ = roms;
    audio_rate_ = audio_rate;
    speedups_ = speedups;
    speedup_count_ = speedup_count;
    for (int n = 0; n < kCpuCount; ++n) {
        z80::Cpu& c = cpu_[n];
        port_[n] = {this, n};
        c.ctx = &port_[n];
        c.mem_read = mem_read;
        c.mem_write = mem_write;
        c.map_read(0, roms.cpu_size[n], roms.cpu[n]);
        c.map_ram(0x8000, 0x800, vram_);
        c.map_ram(0x8800, 0x400, ram_[0]);
        c.map_ram(0x9000, 0x400, ram_[1]);
        c.map_ram(0x9800, 0x400, ram_[2]);
    }
    wsg_.init(roms.wave, 3, kWsgClock, audio_rate);
    board_reset();
}

void Board::board_reset()
{
    for (z80::Cpu& c : cpu_) c.reset();
    for (int n = 0; n < kCpuCount; ++n) {
        pos_[n] = pos_[kMain];
        sleep_[n] = nullptr;
        nmi_sent_[n] = false;
    }
    latch_ = 0;                         // sub CPUs held in reset
    bus06_.reset();
    chip51_.reset();
    chips_reset();
    latch_changed(latch_);
    wsg_.reset();
    watchdog_ = 0;
}

void Board::set_inputs(uint16_t b)
{
    buttons_ = b;
    // The 51XX's view: active low, see namco_io.h.
    uint8_t sticks = 0xff, buttons = 0xff;
    if (b & kUp)      sticks &= uint8_t(~0x01);
    if (b & kRight)   sticks &= uint8_t(~0x02);
    if (b & kDown)    sticks &= uint8_t(~0x04);
    if (b & kLeft)    sticks &= uint8_t(~0x08);
    if (b & kFire)    buttons &= uint8_t(~0x01);
    if (b & kStart1)  buttons &= uint8_t(~0x04);
    if (b & kStart2)  buttons &= uint8_t(~0x08);
    if (b & kCoin)    buttons &= uint8_t(~0x10);
    if (b & kCoin2)   buttons &= uint8_t(~0x20);
    if (b & kService) buttons &= uint8_t(~0x40);
    if (b & kTest)    buttons &= uint8_t(~0x80);
    chip51_.set_inputs(sticks, buttons);
}

uint8_t Board::mem_read(void* ctx, uint16_t addr)
{
    const Port& p = *static_cast<const Port*>(ctx);
    return p.board->read(p.n, addr);
}

void Board::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    const Port& p = *static_cast<const Port*>(ctx);
    p.board->write(p.n, addr, v);
}

uint8_t Board::read(int n, uint16_t a)
{
    if ((a & 0xf800) == 0x7000) {
        if (a & 0x100) return bus06_.control();
        if (!bus06_.reading()) return 0;
        const uint8_t sel = bus06_.selected();
        uint8_t v = 0xff;
        if (sel & 1) v &= chip51_.read();
        for (int chip = 1; chip < 4; ++chip)
            if (sel & (1 << chip)) v &= chip_read(chip);
        return v;
    }
    return io_read(n, a);
}

void Board::write(int n, uint16_t a, uint8_t v)
{
    if ((a & 0xf800) == 0x6800) {
        switch (a & 0x30) {
        case 0x00:
        case 0x10:
            audio_to(now(n));
            wsg_.pacman_write(uint8_t(a & 0x1f), v);
            break;
        case 0x20: write_latch(a & 7, v & 1, n); break;
        default:   watchdog_ = 0; break;
        }
        return;
    }
    if ((a & 0xf800) == 0x7000) {
        if (a & 0x100) {
            bus06_.set_control(v, frame_base_ + uint64_t(now(n)));
            // End the slice here so the first NMI isn't late.
            cpu_[n].stop();
            return;
        }
        if (bus06_.reading()) return;
        const uint8_t sel = bus06_.selected();
        if (sel & 1) chip51_.write(v);
        for (int chip = 1; chip < 4; ++chip)
            if (sel & (1 << chip)) chip_write(chip, v);
        return;
    }
    io_write(n, a, v);
}

void Board::write_latch(int bit, bool v, int n)
{
    const uint8_t old = latch_;
    latch_ = v ? uint8_t(latch_ | (1 << bit)) : uint8_t(latch_ & ~(1 << bit));
    if (latch_ == old) return;
    switch (bit) {
    case 0:
        if (!v) cpu_[kMain].irq(false);         // clears a pending IRQ
        break;
    case 1:
        if (!v) cpu_[kSub].irq(false);
        break;
    case 2:
        break;                                  // read at the NMI times
    case 3:
        if (!v) {                               // into reset
            cpu_[kSub].reset();
            cpu_[kSound].reset();
            chip51_.reset();
            chips_reset();
        } else {                                // out of reset: start now
            pos_[kSub] = pos_[kSound] = now(n);
        }
        sleep_[kSub] = sleep_[kSound] = nullptr;
        break;
    default:
        latch_changed(latch_);
        break;
    }
}

// The speedup whose loop CPU n is spinning in at the start of a slice, or
// null. Never while an interrupt is waiting to be taken.
const Speedup* Board::idle(int n)
{
    const z80::Cpu& c = cpu_[n];
    if (nmi_sent_[n] || (c.irq_line() && c.iff1)) return nullptr;
    const uint16_t pc = cpu_[n].pc.w;
    for (int i = 0; i < speedup_count_; ++i) {
        const Speedup& s = speedups_[i];
        if (s.cpu != n || pc < s.pc_lo || pc > s.pc_hi) continue;
        if (!s.addr) {
            sleep_[n] = &s;
            return &s;
        }
        const uint8_t v = s.addr < 0x8800 ? vram_[s.addr & 0x7ff] : ram_[((s.addr >> 11) & 3) - 1][s.addr & 0x3ff];
        if (((v & s.mask) == s.want) == s.equal) return &s;
    }
    return nullptr;
}

void Board::run_cpus(int end)
{
    for (int n = 0; n < kCpuCount; ++n) {
        if (n == kSub) end = pos_[kMain];       // the others catch up with the main CPU
        if (n != kMain && subs_held()) {
            pos_[n] = end;
            continue;
        }
        const int want = end - pos_[n];
        if (want <= 0) continue;
        const Speedup* s = sleep_[n];
        if (!s && speedups_on_) s = idle(n);
        if (s) {
            stats_[n].skipped += uint32_t(want);
            pos_[n] = end;
            cpu_[n].r = uint8_t(cpu_[n].r + want / s->cycles_per_r);
            continue;
        }
        const int done = cpu_[n].run(want);
        nmi_sent_[n] = false;
        pos_[n] += done;
        stats_[n].run += uint32_t(done);
    }
}

void Board::do_vblank()
{
    vblank();
    if (latch_ & 0x01) {
        cpu_[kMain].irq(true);
        sleep_[kMain] = nullptr;
    }
    if ((latch_ & 0x02) && !subs_held()) {
        cpu_[kSub].irq(true);
        sleep_[kSub] = nullptr;
    }
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        board_reset();
    }
}

void Board::nmi(int n)
{
    cpu_[n].nmi();
    nmi_sent_[n] = true;
    sleep_[n] = nullptr;
}

void Board::audio_to(int cycle)
{
    if (!audio_) return;
    if (cycle > kCyclesPerFrame) cycle = kCyclesPerFrame;
    const int want = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (want <= audio_done_) return;
    wsg_.render(audio_ + audio_done_, want - audio_done_);
    mix(audio_ + audio_done_, want - audio_done_);
    audio_done_ = want;
}

int Board::run_frame(int16_t* audio, int cap)
{
    // Samples for this frame, keeping the remainder so the long-run rate is exact.
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio;
    audio_n_ = audio ? n : 0;
    audio_done_ = 0;
    for (CpuStats& s : stats_) s = {};

    int ev = 0;
    while (ev < kEventCount) {
        int end = pos_[kMain] + kSlice;
        if (end > kEventCycles[ev]) end = kEventCycles[ev];
        const uint64_t due = bus06_.next_nmi();
        if (due != namco_io::Bus06::kNever && due < frame_base_ + uint64_t(end)) {
            const int at = int(int64_t(due - frame_base_));
            end = at > pos_[kMain] ? at : pos_[kMain];
        }
        run_cpus(end);
        while (bus06_.next_nmi() <= frame_base_ + uint64_t(pos_[kMain])) {
            nmi(kMain);
            bus06_.nmi_taken();
        }
        for (; ev < kEventCount && pos_[kMain] >= kEventCycles[ev]; ++ev) {
            switch (ev) {
            case kNmi64:
            case kNmi192:
                if (!(latch_ & 0x04) && !subs_held()) nmi(kSound);
                break;
            case kVblank: do_vblank(); break;
            default:      frame_end(); break;
            }
        }
    }
    audio_to(kCyclesPerFrame);
    for (int& p : pos_) p -= kCyclesPerFrame;
    frame_base_ += kCyclesPerFrame;
    ++frame_;
    return audio_n_;
}

// --- graphics helpers -----------------------------------------------------------------

uint16_t rgb565(int r, int g, int b, bool swap)
{
    const uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return swap ? uint16_t((px >> 8) | (px << 8)) : px;
}

uint16_t prom_rgb565(uint8_t v, bool swap)
{
    const int r = kRGWeights[0] * (v & 1) + kRGWeights[1] * ((v >> 1) & 1) + kRGWeights[2] * ((v >> 2) & 1);
    const int g = kRGWeights[0] * ((v >> 3) & 1) + kRGWeights[1] * ((v >> 4) & 1) + kRGWeights[2] * ((v >> 5) & 1);
    const int b = kBWeights[0] * ((v >> 6) & 1) + kBWeights[1] * ((v >> 7) & 1);
    return rgb565(r, g, b, swap);
}

// The monitor is turned 90 degrees clockwise: a display pixel (x, y) is the
// hardware's (y, 7 - x) inside a character, (y, 15 - x) inside a sprite.
void decode_chars_2bpp(const uint8_t* rom, int count, uint16_t* rows)
{
    // 16 bytes each; hardware pixel x 0-3 in bytes 8-15, 4-7 in bytes 0-7
    // (byte = row y).
    for (int code = 0; code < count; ++code) {
        const uint8_t* src = rom + code * 16;
        for (int dy = 0; dy < 8; ++dy) {
            uint16_t bits = 0;
            for (int dx = 0; dx < 8; ++dx) {
                const int hx = dy, hy = 7 - dx;
                const int xoff = hx < 4 ? 64 + hx : hx - 4;
                bits |= uint16_t(pixel_at(src, xoff + hy * 8) << (2 * dx));
            }
            rows[code * 8 + dy] = bits;
        }
    }
}

void decode_sprites(const uint8_t* rom, int count, uint32_t* rows)
{
    // 64 bytes each; hardware x in groups of 4 pixels 8 bytes apart, rows
    // 0-7 then 8-15 32 bytes on.
    for (int code = 0; code < count; ++code) {
        const uint8_t* src = rom + code * 64;
        for (int hx = 0; hx < 16; ++hx) {
            uint32_t bits = 0;
            for (int i = 0; i < 16; ++i) {
                const int hy = 15 - i;
                const int xoff = (hx >> 2) * 64 + (hx & 3);
                const int yoff = hy < 8 ? hy * 8 : 256 + (hy - 8) * 8;
                bits |= uint32_t(pixel_at(src, xoff + yoff)) << (2 * i);
            }
            rows[code * 16 + hx] = bits;
        }
    }
}

void draw_sprite_line(const Sprite& s, int line, const uint32_t* rows, int codes, const uint16_t* pens,
                      uint8_t opaque, uint16_t* out)
{
    const int wide = (s.flags & kWide) ? 1 : 0, tall = (s.flags & kTall) ? 1 : 0;
    const int dx = line - s.x;
    if (dx < 0 || dx >= 16 << wide) return;
    const bool flipx = s.flags & kFlipX, flipy = s.flags & kFlipY;
    const int tx = dx >> 4, px = dx & 15;
    const int row = flipx ? 15 - px : px;
    for (int ty = 0; ty <= tall; ++ty) {
        // Tile (tx, ty) of a 2 x 2 sprite is code + 0 / 1 / 2 / 3, mirrored
        // with the flips.
        const int tile = ((ty ^ (tall & int(flipy))) << 1) | (tx ^ (wide & int(flipx)));
        const uint32_t bits = rows[((s.code + tile) & (codes - 1)) * 16 + row];
        if (!bits) continue;
        // Hardware y grows to the left on the display.
        const int left = kScreenW - 16 - (s.y + 16 * ty);
        for (int i = 0; i < 16; ++i) {
            const int x = left + i;
            if (x < 0 || x >= kScreenW) continue;
            const int p = int(bits >> (2 * (flipy ? 15 - i : i))) & 3;
            if (opaque & (1 << p)) out[x] = pens[p];
        }
    }
}

} // namespace namco_galaga
