// Taito Qix - see qix.h.
//
// Data CPU:
//   8000-83FF  shared RAM          8400-87FF  its own RAM
//   8800       6850 ACIA (diagnostics only, not fitted here: reads 0)
//   8C00       read / write: raise the video CPU's FIRQ (even addresses
//              to 8FFE); 8C01 (odd): clear its own FIRQ
//   9000-93FF  the sound PIA: A = command out, B = volume, CA1 = the sound
//              board's CA2, CA2 = the sound board's interrupt, CB1 = VSYNC,
//              CB2 = flip; its IRQs go to the data CPU's IRQ
//   9400-97FF  PIA U11: A = player 1 (up, right, down, left, button 2,
//              start 2, start 1, button 1), B = coins / test switches
//   9800-9BFF  PIA U20: spare inputs
//   9C00-9FFF  PIA U30: A = player 2 (cocktail), B = coin counter / lock
//   C000-FFFF  ROM (U12-U19)
// Video CPU:
//   0000-7FFF  video RAM, the half picked by bit 7 of 9402
//   8000-83FF  shared RAM          8400-87FF  CMOS RAM
//   8800       palette bank (bits 0-1), self-test LEDs (bits 2-7)
//   8C00       raise the data CPU's FIRQ; 8C01 clear its own
//   9000-93FF  palette RAM (RRGGBBII)
//   9400       the byte at the latched address; 9402 / 9403 the latch
//   9800       the scanline: the shown line latched as it starts (the
//              6845's row address bits make it the line number), 255 in
//              the blank
//   9C00 / 9C01  the 6845's address / register
//   C800-FFFF  ROM (U4-U10)
// Inputs are active low. Facts from MAME's driver (read for facts only).
//
// Running the CPUs: both 6809s run on one clock, in step - the one behind
// runs, at most kSlice cycles past the other, so a write or a FIRQ one
// makes is seen by the other within kSlice cycles (a FIRQ raised ahead of
// the other's time waits for it to get there). The game is sensitive to
// this: an 8-cycle slice gives another attract-mode Qix than 32 (MAME
// interleaves them at every instruction). Idle loops: before a slice a CPU is
// followed for up to kIdleProbe instructions; a round trip that leaves
// every register as it was, writes nothing and touches no I/O (reading
// the scanline register is allowed) is an idle loop - so is the data
// CPU's counting wait, "INC <c ; LDA / CMPA m ; BNE", whose only write is
// the count (a random seed in shared RAM at 8025, which the video CPU's
// code never reads). The CPU then sleeps, its time standing still, while
// the other runs on alone. It wakes when the other writes the shared RAM
// (its writes are then caught by the bus callback) or raises its FIRQ,
// when its IRQ rises, at the end of the run (a VSYNC edge, the frame's
// end) and, if it reads the scanline, when the scanline changes. Waking,
// it counts the loop's whole rounds up to that moment at once (adding
// them to a counting wait's count) and runs on from there: it sees a
// write at most one round (~14 cycles) early - less than the lockstep's
// own slack.
#include "qix.h"

#include <climits>
#include <cstring>
#include <new>

namespace qix {

namespace {

constexpr int kSlice = 32;
constexpr int kSoloSlice = 128;
constexpr int kIdleProbe = 12;
constexpr int kFirstShownIdx = 17;          // frames start at line 256: line 0 is the 17th
constexpr int kVsyncLines = 16;             // the 6845's VSYNC width

// The first boot's restart (see run_frame()): this many frames after the
// CMOS is first seen valid, so its writing is long done.
constexpr uint32_t kRestartFrames = 30;

} // namespace

// --- ROM set ---------------------------------------------------------------------------

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x4000},
    {"videocpu", 0x3800},
    {"audiocpu", 0x0800},
};

// MAME's "qix" (Rev 2). Older collections name the files u4 ... u27.
constexpr romset::Rom kRoms[] = {
    {kRegionData,  0x0000, 0x0800, 0xaad35508, "qq12_rev2.u12", false},
    {kRegionData,  0x0800, 0x0800, 0x46c13504, "qq13_rev2.u13", false},
    {kRegionData,  0x1000, 0x0800, 0x5115e896, "qq14_rev2.u14", false},
    {kRegionData,  0x1800, 0x0800, 0xccd52a1b, "qq15_rev2.u15", false},
    {kRegionData,  0x2000, 0x0800, 0xcd1c36ee, "qq16_rev2.u16", false},
    {kRegionData,  0x2800, 0x0800, 0x1acb682d, "qq17_rev2.u17", false},
    {kRegionData,  0x3000, 0x0800, 0xde77728b, "qq18_rev2.u18", false},
    {kRegionData,  0x3800, 0x0800, 0xc0994776, "qq19_rev2.u19", false},
    {kRegionVideo, 0x0000, 0x0800, 0x5b906a09, "qq4_rev2.u4",   false},
    {kRegionVideo, 0x0800, 0x0800, 0x254a3587, "qq5_rev2.u5",   false},
    {kRegionVideo, 0x1000, 0x0800, 0xace30389, "qq6_rev2.u6",   false},
    {kRegionVideo, 0x1800, 0x0800, 0x8ebcfa7c, "qq7_rev2.u7",   false},
    {kRegionVideo, 0x2000, 0x0800, 0xb8a3c8f9, "qq8_rev2.u8",   false},
    {kRegionVideo, 0x2800, 0x0800, 0x26cbcd55, "qq9_rev2.u9",   false},
    {kRegionVideo, 0x3000, 0x0800, 0x568be942, "qq10_rev2.u10", false},
    {kRegionSound, 0x0000, 0x0800, 0xf3782bd0, "qq27.u27",      false},
};

const romset::Set kRomSet = {
    "qix", nullptr, "Qix", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

// --- setup -------------------------------------------------------------------------------

Machine::Machine()
{
    vram_[0] = new (std::nothrow) uint8_t[0x8000];
    vram_[1] = new (std::nothrow) uint8_t[0x8000];
    for (uint8_t* half : vram_)
        if (half) std::memset(half, 0, 0x8000);
}

Machine::~Machine()
{
    delete[] vram_[0];
    delete[] vram_[1];
}

void Machine::init(const uint8_t* const* regions, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    audio_rate_ = audio_rate;
    swap_ = swap_rgb565_bytes;
    rom_data_ = regions[kRegionData];
    rom_video_ = regions[kRegionVideo];

    m6809::Cpu& d = side_[kData].cpu;
    d.ctx = this;
    d.mem_read = data_read;
    d.mem_write = data_write;
    d.map_ram(0x8000, 0x400, shared_);
    d.map_ram(0x8400, 0x400, data_ram_);
    d.map_read(0xc000, 0x4000, rom_data_);

    m6809::Cpu& v = side_[kVideo].cpu;
    v.ctx = this;
    v.mem_read = video_read;
    v.mem_write = video_write;
    v.map_ram(0x8000, 0x400, shared_);
    v.map_ram(0x8400, 0x400, cmos_);
    v.map_ram(0x9000, 0x400, palette_);
    v.map_read(0xc800, 0x3800, rom_video_);
    window_ = 0;
    v.map_ram(0x0000, 0x8000, vram_[0]);

    snd_pia_.ctx = this;
    snd_pia_.out_a = snd_pia_port_a;
    snd_pia_.out_b = snd_pia_port_b;
    snd_pia_.irq_changed = snd_pia_irq;
    sound_.owner = this;
    sound_.port_a_changed = sound_port_a;
    sound_.ca2_changed = sound_ca2;
    sound_.init(regions[kRegionSound], audio_rate);

    // RRGGBBII: our own guess at the video board's levels - the intensity
    // bits scale the colour (half to full) and add a little grey, so the
    // full colours stay saturated and intensity 0 is dim but not black.
    for (int i = 0; i < 256; ++i) {
        const double in = (i & 3) / 3.0;
        int c8[3];
        for (int k = 0; k < 3; ++k) {
            const double v = ((i >> (6 - 2 * k)) & 3) / 3.0;
            const double level = v * (0.5 + 0.5 * in) * 0.85 + 0.15 * in;
            c8[k] = int(level * 255 + 0.5);
        }
        uint16_t px = uint16_t(((c8[0] >> 3) << 11) | ((c8[1] >> 2) << 5) | (c8[2] >> 3));
        if (swap_) px = uint16_t((px >> 8) | (px << 8));
        rgb565_[i] = px;
    }
    side_[kData].t = side_[kVideo].t = 0;
    reset();
}

void Machine::reset()
{
    for (Pia6821& p : pia_) p.reset();
    snd_pia_.reset();
    snd_pia_.in_a = 0xff;
    snd_ca2_ = true;
    sound_.reset();
    for (Side& s : side_) {
        s.cpu.irq(false);
        s.cpu.firq(false);
        s.cpu.reset();
        s.asleep = false;
        s.firq_at = -1;
        s.probe_wait = s.probe_backoff = 0;
    }
    map_shared_writes();
    map_video_window(0);
    latch_[0] = latch_[1] = 0;
    palette_bank_ = 0;
}

// --- the bus -----------------------------------------------------------------------------

int Machine::beam_line(int t)
{
    return (256 + t / kCyclesPerLine) % kLinesPerFrame;
}

uint8_t Machine::scanline_at(int t)
{
    const int line = beam_line(t);
    return line < 256 ? uint8_t(line) : 0xff;
}

void Machine::map_video_window(uint8_t latch_hi)
{
    const uint8_t half = latch_hi >> 7;
    if (half == window_) return;
    window_ = half;
    side_[kVideo].cpu.map_ram(0x0000, 0x8000, vram_[half]);
}

// A sleeping CPU must see the other's shared RAM writes: while it sleeps,
// the other's writes there go through the bus callback.
void Machine::map_shared_writes()
{
    for (int s = 0; s < 2; ++s)
        side_[s].cpu.map_write(0x8000, 0x400, side_[1 - s].asleep ? nullptr : shared_);
}

void Machine::shared_write(int s, uint16_t addr, uint8_t v)
{
    uint8_t& b = shared_[addr & 0x3ff];
    if (b == v) return;                     // nothing the sleeper could see
    b = v;
    const Side& o = side_[1 - s];
    if (o.watch >= 0 && addr != o.watch && addr != o.watch_count) return;     // not what it reads
    wake(1 - s, now(s));
    side_[s].cpu.stop();
}

void Machine::raise_firq(int from)
{
    const int to = 1 - from, at = now(from);
    Side& o = side_[to];
    wake(to, at);
    if (o.t >= at) {
        o.cpu.firq(true);
    } else if (o.firq_at < 0 || at < o.firq_at) {
        o.firq_at = at;                     // when it gets there
    }
    side_[from].cpu.stop();                 // let the other catch up
}

void Machine::wake(int s, int at)
{
    Side& a = side_[s];
    if (!a.asleep) return;
    if (at > a.t && a.round > 0) {
        const int rounds = (at - a.t) / a.round;
        a.t += rounds * a.round;
        if (a.counter) *a.counter = uint8_t(*a.counter + rounds);     // a counting wait's count
    }
    a.asleep = false;
    a.probe_wait = a.probe_backoff = 0;
    map_shared_writes();
}

uint8_t Machine::data_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    ++m.side_[kData].io;
    switch (addr & 0xfc00) {
    case 0x8c00:
        if (addr & 1) m.side_[kData].cpu.firq(false);
        else          m.raise_firq(kData);
        return 0;
    case 0x9000:
        m.sync_sound();
        return m.snd_pia_.read(addr & 3);
    case 0x9400: return m.pia_[0].read(addr & 3);
    case 0x9800: return m.pia_[1].read(addr & 3);
    case 0x9c00: return m.pia_[2].read(addr & 3);
    default:     return 0;              // ACIA, unmapped
    }
}

void Machine::data_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    ++m.side_[kData].io;
    switch (addr & 0xfc00) {
    case 0x8000:
        m.shared_write(kData, addr, v);
        return;
    case 0x8c00:
        if (addr & 1) m.side_[kData].cpu.firq(false);
        else          m.raise_firq(kData);
        return;
    case 0x9000:
        m.sync_sound();
        m.snd_pia_.write(addr & 3, v);
        if ((addr & 3) == 1) m.snd_pia_ca2_check();
        return;
    case 0x9400: m.pia_[0].write(addr & 3, v); return;
    case 0x9800: m.pia_[1].write(addr & 3, v); return;
    case 0x9c00: m.pia_[2].write(addr & 3, v); return;
    default:     return;
    }
}

uint8_t Machine::video_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    Side& s = m.side_[kVideo];
    ++s.io;
    switch (addr & 0xfc00) {
    case 0x8c00:
        if (addr & 1) s.cpu.firq(false);
        else          m.raise_firq(kVideo);
        return 0;
    case 0x9400:
        if ((addr & 3) == 0) return m.vram_[m.latch_[0] >> 7][((m.latch_[0] & 0x7f) << 8) | m.latch_[1]];
        return 0;
    case 0x9800:
        ++s.scan_reads;
        return scanline_at(m.now(kVideo));
    case 0x9c00:
        // Only the light pen and cursor registers (14-17) read back.
        if ((addr & 1) && m.crtc_sel_ >= 14 && m.crtc_sel_ <= 17) return m.crtc_[m.crtc_sel_];
        return 0;
    default:
        return 0;
    }
}

void Machine::video_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    Side& s = m.side_[kVideo];
    ++s.io;
    switch (addr & 0xfc00) {
    case 0x8000:
        m.shared_write(kVideo, addr, v);
        return;
    case 0x8800:
        m.palette_bank_ = v & 3;            // LEDs in bits 2-7
        return;
    case 0x8c00:
        if (addr & 1) s.cpu.firq(false);
        else          m.raise_firq(kVideo);
        return;
    case 0x9400:
        switch (addr & 3) {
        case 0:
            m.vram_[m.latch_[0] >> 7][((m.latch_[0] & 0x7f) << 8) | m.latch_[1]] = v;
            return;
        case 2:
            m.latch_[0] = v;
            m.map_video_window(v);
            return;
        case 3:
            m.latch_[1] = v;
            return;
        default:
            return;                         // the write mask of later boards
        }
    case 0x9c00:
        if (addr & 1) m.crtc_[m.crtc_sel_] = v;
        else          m.crtc_sel_ = v & 31;
        return;
    default:
        return;
    }
}

// --- the sound PIA -----------------------------------------------------------------------

// Port A pins as the sound board sees them: output bits driven, the rest
// pulled up.
void Machine::snd_pia_port_a(void* ctx, uint8_t)
{
    Machine& m = *static_cast<Machine*>(ctx);
    m.sound_.set_command(uint8_t((m.snd_pia_.port_a() & m.snd_pia_.ddr_a()) | ~m.snd_pia_.ddr_a()));
}

void Machine::snd_pia_port_b(void* ctx, uint8_t v)
{
    static_cast<Machine*>(ctx)->sound_.set_volume(v);
}

// CA2 is the sound board's interrupt: set / reset output (CRA 11x), else
// not driven (high).
void Machine::snd_pia_ca2_check()
{
    const uint8_t c = snd_pia_.ctl_a();
    const bool level = (c & 0x30) == 0x30 ? (c & 0x08) != 0 : true;
    if (level == snd_ca2_) return;
    snd_ca2_ = level;
    if (level) ++sound_commands_;
    sound_.set_ca1(level);
}

void Machine::snd_pia_irq(void* ctx)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const bool level = m.snd_pia_.irq();
    m.side_[kData].cpu.irq(level);
    if (level) m.wake(kData, m.now(kData));
}

void Machine::sound_port_a(void* ctx, uint8_t pins)
{
    static_cast<Machine*>(ctx)->snd_pia_.in_a = pins;
}

void Machine::sound_ca2(void* ctx, bool level)
{
    static_cast<Machine*>(ctx)->snd_pia_.ca1(level);
}

// --- inputs ------------------------------------------------------------------------------

void Machine::set_input_ports()
{
    const uint16_t b = buttons_;
    uint8_t p1 = 0, coin = 0;
    if (b & kUp)     p1 |= 0x01;
    if (b & kRight)  p1 |= 0x02;
    if (b & kDown)   p1 |= 0x04;
    if (b & kLeft)   p1 |= 0x08;
    if (b & kSlow)   p1 |= 0x10;
    if (b & kStart2) p1 |= 0x20;
    if (b & kStart1) p1 |= 0x40;
    if (b & kFast)   p1 |= 0x80;
    if (b & kAdvance) coin |= 0x01;
    if (b & kCoin)    coin |= 0x10;
    if (b & kCoin2)   coin |= 0x20;
    if (b & kTilt)    coin |= 0x80;
    pia_[0].in_a = uint8_t(~p1);
    pia_[0].in_b = uint8_t(~coin);
    pia_[1].in_a = 0xff;
    pia_[1].in_b = 0xff;
    pia_[2].in_a = 0xff;
    pia_[2].in_b = 0x00;
}

// --- running -----------------------------------------------------------------------------

// When a sleeping CPU must look again: the end of the run, or the next
// scanline change if its loop reads the scanline.
int Machine::due(int s, int target) const
{
    return side_[s].wake_at < target ? side_[s].wake_at : target;
}

// A counting wait, "INC <c ; LDA or CMPA <m or mmmm ; BNE back" (the data
// CPU counts a random seed while it waits for its interrupt): the loop
// around `pc` if the code there is one, its counter's byte, else null;
// `at` = the count's address, `reads` = the one other byte it reads.
static uint8_t* counting_loop(const m6809::Cpu& c, uint16_t pc, uint16_t& at, uint16_t& reads)
{
    auto peek = [&](uint16_t a) -> int {
        const uint8_t* p = c.read_page[a >> m6809::Cpu::kPageShift];
        return p ? p[a & (m6809::Cpu::kPageSize - 1)] : -1;
    };
    static const int kStarts[] = {0, 2, 4, 5};
    for (int back : kStarts) {
        const uint16_t s0 = uint16_t(pc - back);
        if (peek(s0) != 0x0c) continue;                         // INC <c
        const int op = peek(uint16_t(s0 + 2));
        const bool ext = op == 0xb6 || op == 0xb1;              // LDA / CMPA mmmm
        if (!ext && op != 0x96 && op != 0x91) continue;         // LDA / CMPA <m
        const int len = ext ? 7 : 6;
        if (back > 2 && back != len - 2) continue;              // pc on an instruction
        if (peek(uint16_t(s0 + len - 2)) != 0x26 || peek(uint16_t(s0 + len - 1)) != 256 - len) continue;
        const uint16_t counter = uint16_t((c.dp << 8) | peek(uint16_t(s0 + 1)));
        const uint16_t read = ext ? uint16_t((peek(uint16_t(s0 + 3)) << 8) | peek(uint16_t(s0 + 4)))
                                  : uint16_t((c.dp << 8) | peek(uint16_t(s0 + 3)));
        if (counter == read || !c.read_page[counter >> m6809::Cpu::kPageShift]) return nullptr;
        if (!c.read_page[read >> m6809::Cpu::kPageShift]) return nullptr;     // must be memory
        reads = read;
        at = counter;
        return const_cast<uint8_t*>(c.read_page[counter >> m6809::Cpu::kPageShift])
               + (counter & (m6809::Cpu::kPageSize - 1));
    }
    return nullptr;
}

// Follows the CPU an instruction at a time (not past `limit`) looking for
// an idle loop (see the top of this file). True if it now sleeps.
bool Machine::probe_idle(int s, int limit)
{
    Side& a = side_[s];
    m6809::Cpu& c = a.cpu;
    const uint16_t pc0 = c.pc;
    int first = -1;
    uint32_t writes = 0, io = 0, scan = 0;
    uint16_t d = 0, x = 0, y = 0, u = 0, sp = 0;
    uint8_t dp = 0, cc = 0;
    uint8_t* counter = nullptr;
    uint8_t count = 0;
    uint16_t reads = 0, count_at = 0;
    for (int n = 0; n < kIdleProbe && a.t < limit; ++n) {
        running_ = s;
        const int ran = c.run(1);
        running_ = -1;
        a.t += ran;
        a.ran += uint64_t(ran);
        if (c.pc != pc0) continue;
        if (first < 0) {
            first = a.t;
            writes = c.writes; io = a.io; scan = a.scan_reads;
            d = c.d; x = c.x; y = c.y; u = c.u; sp = c.s; dp = c.dp; cc = c.cc;
            counter = counting_loop(c, pc0, count_at, reads);
            if (counter) count = *counter;
            continue;
        }
        if (a.io - io == a.scan_reads - scan && c.d == d && c.x == x && c.y == y && c.u == u && c.s == sp
            && c.dp == dp && c.cc == cc && !c.in_cwai() && !c.in_sync() && !c.halted() && a.t > first
            && (c.writes == writes || (counter && c.writes == writes + 1 && *counter == uint8_t(count + 1)))) {
            a.asleep = true;
            a.round = a.t - first;
            a.counter = c.writes == writes ? nullptr : counter;
            // A counting wait reads only its count and one byte: only
            // writes there can change what it does.
            a.watch = a.counter ? reads : -1;
            a.watch_count = a.counter ? count_at : -1;
            a.wake_at = INT_MAX;
            if (a.scan_reads != scan) {         // waits for the scanline: until it changes
                const int i = a.t / kCyclesPerLine;
                const int line = beam_line(a.t);
                if (line < 255) a.wake_at = (i + 1) * kCyclesPerLine;
                else a.wake_at = (i < kFirstShownIdx ? kFirstShownIdx : kFirstShownIdx + kLinesPerFrame) * kCyclesPerLine;
            }
            ++idle_skips_;
            map_shared_writes();
            return true;
        }
        break;
    }
    return false;
}

void Machine::run_cpus_to(int target)
{
    for (;;) {
        // The next thing each CPU does: an awake one runs from its time, a
        // sleeping one wakes when due. A sleeper goes first on a tie.
        int act[2];
        for (int s = 0; s < 2; ++s) act[s] = side_[s].asleep ? due(s, target) : side_[s].t;
        int s = act[1] < act[0] || (act[1] == act[0] && side_[1].asleep) ? 1 : 0;
        if (side_[s].asleep) {
            wake(s, act[s]);
            continue;
        }
        if (act[s] >= target) {
            if (side_[1 - s].asleep) { wake(1 - s, target); continue; }
            break;
        }
        Side& a = side_[s];
        const Side& b = side_[1 - s];
        if (a.firq_at >= 0 && a.t >= a.firq_at) {
            a.cpu.firq(true);
            a.firq_at = -1;
        }
        // Alone (the other sleeps) it runs longer slices, still probed now
        // and then, so it can fall asleep too.
        int limit = b.asleep ? a.t + kSoloSlice : b.t + kSlice;
        if (b.asleep && due(1 - s, target) < limit) limit = due(1 - s, target);
        if (limit > target) limit = target;
        if (a.firq_at >= 0 && a.firq_at < limit) limit = a.firq_at;
        // A busy CPU isn't probed before every slice: each probe that finds
        // no idle loop doubles the wait before the next (up to 16 slices).
        if (a.probe_wait) {
            --a.probe_wait;
        } else if (idle_skipping_ && probe_idle(s, limit)) {
            a.probe_backoff = 0;
            continue;
        } else {
            a.probe_backoff = a.probe_backoff ? (a.probe_backoff < 16 ? a.probe_backoff * 2 : 16) : 1;
            a.probe_wait = a.probe_backoff;
        }
        if (a.t >= limit) continue;
        running_ = s;
        const int ran = a.cpu.run(limit - a.t);
        running_ = -1;
        a.t += ran;
        a.ran += uint64_t(ran);
    }
}

// Valid as the game checks it at power-on: 55AA at 86A9 and the sum of
// 8701-87FF in 8700.
bool Machine::cmos_valid() const
{
    if (cmos_[0x2a9] != 0x55 || cmos_[0x2aa] != 0xaa) return false;
    uint8_t sum = 0;
    for (int i = 0x301; i < 0x400; ++i) sum = uint8_t(sum + cmos_[i]);
    return sum == cmos_[0x300];
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // The first boot found the CMOS RAM empty, wrote the factory settings
    // and now waits on the setup pages for the operator's Advance switch
    // (five presses lead through them unchanged). Instead the board is
    // restarted once the settings are written: with a valid CMOS the game
    // starts at once, as it does on every later power-on.
    if (!cmos_restart_) {
        if (!cmos_valid_at_) {
            if (cmos_valid()) cmos_valid_at_ = frame_;
        } else if (frame_ - cmos_valid_at_ >= kRestartFrames) {
            cmos_restart_ = true;
            cmos_valid_at_ = frame_;
            // The operator's ATTRACT SOUND (87E9) as asked, the checksum in
            // 8700 made again as the game makes it (the sum of 8701-87FF);
            // the game copies the settings to its RAM as it starts.
            if (attract_sound_ >= 0) {
                cmos_[0x3e9] = uint8_t(attract_sound_);
                uint8_t sum = 0;
                for (int i = 0x301; i < 0x400; ++i) sum = uint8_t(sum + cmos_[i]);
                cmos_[0x300] = sum;
            }
            reset();
        }
    }
    set_input_ports();

    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    snd_rem_ += uint64_t(kCyclesPerFrame) * qix_sound::kClock;
    snd_cycles_ = int(snd_rem_ / kCpuClock);
    snd_rem_ %= kCpuClock;
    sound_.begin_frame(audio && n > 0 ? audio : nullptr, n, snd_cycles_);

    // The frame starts with VSYNC (CB1 high, lines 256-271); the data CPU's
    // interrupt is its falling edge.
    snd_pia_.cb1(true);
    run_cpus_to(kVsyncLines * kCyclesPerLine);
    snd_pia_.cb1(false);
    run_cpus_to(kCyclesPerFrame);
    for (Side& s : side_) {
        s.t -= kCyclesPerFrame;
        if (s.firq_at >= 0) s.firq_at = s.firq_at > kCyclesPerFrame ? s.firq_at - kCyclesPerFrame : 0;
    }
    sound_.end_frame();

    const uint8_t* bank = palette_ + palette_bank_ * 256;
    for (int i = 0; i < 256; ++i) pen_[i] = rgb565_[bank[i]];
    ++frame_;
    return audio ? n : 0;
}

// The tube is turned: the player's row y is the CRTC's column 255 - y, and
// the player's column x is the CRTC's line x + kFirstLine (lines 0-7 and
// 248-255 are left out: the game draws nothing there but the edges of
// its sparkle effects).
void Machine::render_line(int y, uint16_t* out) const
{
    const int col = 255 - y;
    const uint8_t* src = vram_[0] + kFirstLine * 256 + col;
    for (int i = kFirstLine; i < 128; ++i, src += 256) *out++ = pen_[*src];
    src = vram_[1] + col;
    for (int i = 128; i < kFirstLine + kScreenW; ++i, src += 256) *out++ = pen_[*src];
}

} // namespace qix
