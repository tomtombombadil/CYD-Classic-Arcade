// Q*bert machine - see qbert.h.
//
// Main CPU (8088) memory map, A0-A15 decoded (the 8088's A16-A19 aren't):
//   0000-0FFF  RAM, battery backed     1000-2FFF  RAM
//   3000-30FF  sprite RAM, write only (seen to 37FF): 64 x (y, x, ~code, -)
//   3800-3BFF  video RAM, 32 x 32 tile codes (seen to 3FFF)
//   4000-4FFF  character RAM socket - Q*bert's tiles are in ROM instead
//   5000-501F  palette RAM, write only (seen to 57FF): 16 x (GB, R)
//   5800 r DSW / w watchdog   5801 r IN1 (starts, coins, test) / w trackball reset
//   5802 r FF / w sound command   5803 r FF / w video control, coin counter, knocker
//   5804 r IN4 (the joystick)  (5800-5807 seen to 5FFF)
//   6000-FFFF  ROM (A000-FFFF in Q*bert; the 8088 starts at FFFF0 = FFF0)
// NMI = VBLANK, INTR unused. Facts from MAME's driver (read for facts only).
//
// Sound CPU (6502), A0-A14 decoded:
//   0000-0FFF  6532 RIOT: A9 = 0 its 128 bytes of RAM, A9 = 1 its registers
//   1000-1FFF  w DAC         2000-2FFF  w speech chip (phoneme, inflection)
//   3000-3FFF  w speech clock DAC        6000-7FFF  ROM (7000-7FFF here)
// IRQ = the RIOT (Q*bert uses its PA7 edge: a new command), NMI = the
// speech chip's A/R line (ready for a phoneme) or RIOT PB7 low.
#include "qbert.h"

#include <cstring>

namespace qbert {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x6000},
    {"audiocpu", 0x1000},
    {"bgtiles", 0x2000},
    {"sprites", 0x8000},
};

// CRCs as in MAME's "qbert" (US set 1).
constexpr romset::Rom kRoms[] = {
    {kRegionMain,    0x0000, 0x2000, 0xfe434526, "qb-rom2.bin", false},
    {kRegionMain,    0x2000, 0x2000, 0x55635447, "qb-rom1.bin", false},
    {kRegionMain,    0x4000, 0x2000, 0x8e318641, "qb-rom0.bin", false},
    {kRegionSound,   0x0000, 0x0800, 0x15787c07, "qb-snd1.bin", false},
    {kRegionSound,   0x0800, 0x0800, 0x58437508, "qb-snd2.bin", false},
    {kRegionTiles,   0x0000, 0x1000, 0x7a9ba824, "qb-bg0.bin",  false},
    {kRegionTiles,   0x1000, 0x1000, 0x22e5b891, "qb-bg1.bin",  false},
    {kRegionSprites, 0x0000, 0x2000, 0xdd436d3a, "qb-fg3.bin",  false},
    {kRegionSprites, 0x2000, 0x2000, 0xf69b9483, "qb-fg2.bin",  false},
    {kRegionSprites, 0x4000, 0x2000, 0x224e8356, "qb-fg1.bin",  false},
    {kRegionSprites, 0x6000, 0x2000, 0x2f695b85, "qb-fg0.bin",  false},
};

// Palette: 4 bits a gun through 2K / 1K / 470 / 240 ohm resistors - the
// conductances, scaled so all four bits give 255.
constexpr uint8_t level(int v)
{
    const double g[4] = {1.0 / 2000, 1.0 / 1000, 1.0 / 470, 1.0 / 240};
    const double all = g[0] + g[1] + g[2] + g[3];
    double s = 0;
    for (int b = 0; b < 4; ++b)
        if (v & (1 << b)) s += g[b];
    return uint8_t(255.0 * s / all + 0.5);
}
constexpr uint8_t kLevel[16] = {
    level(0), level(1), level(2),  level(3),  level(4),  level(5),  level(6),  level(7),
    level(8), level(9), level(10), level(11), level(12), level(13), level(14), level(15),
};

// The watchdog resets the board after 16 VBLANKs without a write to 5800.
constexpr int kWatchdogFrames = 16;

// The sound CPU's idle loop (7035-704D): it polls for a command the IRQ
// handler leaves at 0021. Entered at 7035 with A = 0, 0020 = 0023 = 0021
// = 0 it changes nothing and takes 29 cycles a turn, so whole turns are
// skipped while no interrupt can come.
constexpr uint16_t kSndIdlePc = 0x7035, kSndIdleEnd = 0x704d;
constexpr int      kSndIdleTurn = 29;
// The sound CPU runs in slices of at most this many cycles (so the idle
// loop is found soon after it is entered).
constexpr int      kSndSlice = 256;

// The main CPU's wait for the next frame (B36D: CMP AX,[0080] / JZ B36D):
// the NMI handler counts frames at 0080. While AX equals it, a turn
// changes nothing and takes 35 cycles; only the NMI ends it, so whole
// turns up to the end of the run are skipped.
constexpr uint16_t kMainIdleIp = 0xb36d;
constexpr int      kMainIdleTurn = 35;
constexpr int      kMainSlice = 1024;

} // namespace

const romset::Set kRomSet = {
    "qbert", nullptr, "Q*bert", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;
    swap_ = swap_rgb565_bytes;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.addr_mask = 0xffff;
    cpu_.map_ram(0x0000, 0x3000, ram_);
    for (uint32_t a = 0x3000; a < 0x3800; a += 0x100) cpu_.map_write(a, 0x100, sprite_ram_);
    cpu_.map_ram(0x3800, 0x400, vram_);
    cpu_.map_ram(0x3c00, 0x400, vram_);
    cpu_.map_read(0xa000, 0x6000, roms.main);

    snd_.ctx = this;
    snd_.mem_read = snd_read;
    snd_.mem_write = snd_write;
    for (uint32_t m = 0; m < 0x10000; m += 0x8000) {
        // RIOT RAM: pages with A9 = 0 below 1000 (A7, A8, A10, A11 not decoded).
        for (uint32_t p = 0; p < 0x1000; p += 0x100)
            if (!(p & 0x200)) snd_.map_ram(uint16_t(m + p), 0x100, snd_ram_);
        snd_.map_read(uint16_t(m + 0x7000), 0x1000, roms.sound);
    }

    speech_.init(audio_rate, kSoundClock);
    knocker_.init(audio_rate);

    // The battery-backed RAM starts as the board's MAME default: all ones.
    std::memset(ram_, 0xff, 0x1000);
    fc_ = 0;
    snd_done_ = 0;
    reset();
}

void Machine::reset()
{
    cpu_.reset();
    snd_.reset();
    snd_.irq(false);
    pa_out_ = pa_ddr_ = pb_out_ = pb_ddr_ = 0;
    edge_ie_ = edge_flag_ = timer_ie_ = timer_flag_ = false;
    edge_pos_ = false;
    timer_shift_ = 10;
    timer_end_ = snd_time_ + snd_done_ + (256 << 10);
    speech_.reset();
    knocker_.reset();
    nmi_line_ = true;           // A/R is high while the speech chip is idle
    hp_in_ = float(dac_) - 128.0f;  // the DC block starts settled (no thump)
    hp_out_ = 0;
    out_ = 0;
    watchdog_ = 0;
}

// --- main CPU bus --------------------------------------------------------------

uint8_t Machine::mem_read(void* ctx, uint32_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (addr < 0x5800 || addr >= 0x6000) return 0;  // write-only RAMs, empty sockets
    switch (addr & 7) {
    case 0: return m.dsw_;
    case 1: {
        uint8_t v = 0x40;                           // the test switch, active low
        if (m.buttons_ & kStart1) v |= 0x01;
        if (m.buttons_ & kStart2) v |= 0x02;
        if (m.buttons_ & kCoin)   v |= 0x04;
        if (m.buttons_ & kCoin2)  v |= 0x08;
        if (m.buttons_ & kTest)   v &= uint8_t(~0x40);
        if (m.buttons_ & kSelect) v |= 0x80;
        return v;
    }
    case 2: case 3: return 0xff;                    // no trackball
    case 4: {
        uint8_t v = 0;
        if (m.buttons_ & kDownRight) v |= 0x01;
        if (m.buttons_ & kUpLeft)    v |= 0x02;
        if (m.buttons_ & kUpRight)   v |= 0x04;
        if (m.buttons_ & kDownLeft)  v |= 0x08;
        return v;
    }
    default: return 0;
    }
}

void Machine::mem_write(void* ctx, uint32_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0x5000 && addr < 0x5800) {
        m.palette_[addr & 0x1f] = v;
        m.decode_pen(int(addr & 0x1f) >> 1);
        return;
    }
    if (addr < 0x5800 || addr >= 0x6000) return;    // ROM, the empty character RAM socket
    switch (addr & 7) {
    case 0: m.watchdog_ = 0; break;
    case 2: m.sound_command(v); break;
    case 3: {
        // Bit 5 drives the knocker's solenoid: a strike as it switches on.
        if ((v & 0x20) && !(m.out_ & 0x20)) {
            m.sound_to(m.now());
            m.audio_to(m.snd_done_);
            m.knocker_.strike();
            ++m.knocks_;
        }
        m.out_ = v;
        break;
    }
    default: break;                                 // trackball reset, OP40-47
    }
}

void Machine::decode_pen(int i)
{
    const uint8_t gb = palette_[i * 2], rr = palette_[i * 2 + 1];
    const int r = kLevel[rr & 15], g = kLevel[gb >> 4], b = kLevel[gb & 15];
    uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    if (swap_) px = uint16_t((px >> 8) | (px << 8));
    pens_[i] = px;
}

// --- sound board ---------------------------------------------------------------------

// The command latch: the board inverts bits 0-5 into PA0-5, and PA7 is
// high unless bits 0-3 are all set (the "no command" value between two).
void Machine::sound_command(uint8_t v)
{
    sound_to(now());
    ++sound_commands_;
    pa_in_ = uint8_t((~v & 0x3f) | 0x40 | ((v & 0x0f) != 0x0f ? 0x80 : 0));
    const bool pa7 = ((pa_out_ & pa_ddr_) | (pa_in_ & ~pa_ddr_)) & 0x80;
    if (pa7 != pa7_ && pa7 == edge_pos_) {
        edge_flag_ = true;
        riot_update_irq();
    }
    pa7_ = pa7;
}

void Machine::riot_update_irq()
{
    snd_.irq((edge_ie_ && edge_flag_) || (timer_ie_ && timer_flag_));
}

uint8_t Machine::riot_timer() const
{
    const int64_t t = snd_time_ + snd_done_ + snd_.elapsed();
    if (t < timer_end_) return uint8_t((timer_end_ - t - 1) >> timer_shift_);
    return uint8_t(0xff - ((t - timer_end_) & 0xff));   // after 0 it counts on at 1x
}

uint8_t Machine::snd_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    addr &= 0x7fff;
    if (addr >= 0x1000) return addr >= 0x6000 && addr < 0x7000 ? 0 : 0xff;   // empty ROM socket / unmapped
    // RIOT registers (RAM is mapped directly).
    const int r = addr & 0x1f;
    if (!(r & 4)) {
        switch (r & 3) {
        case 0: return uint8_t((m.pa_out_ & m.pa_ddr_) | (m.pa_in_ & ~m.pa_ddr_));
        case 1: return m.pa_ddr_;
        case 2: {
            // Switches: all off (test off); PB7 = the speech chip's A/R.
            const int64_t t = m.snd_time_ + m.snd_done_ + m.snd_.elapsed();
            const uint8_t in = uint8_t(0x7f | (m.speech_.ready(t) ? 0x80 : 0));
            return uint8_t((m.pb_out_ & m.pb_ddr_) | (in & ~m.pb_ddr_));
        }
        default: return m.pb_ddr_;
        }
    }
    if (r & 1) {    // interrupt flags; reading clears the edge flag
        const int64_t t = m.snd_time_ + m.snd_done_ + m.snd_.elapsed();
        if (t >= m.timer_end_) m.timer_flag_ = true;
        const uint8_t v = uint8_t((m.timer_flag_ ? 0x80 : 0) | (m.edge_flag_ ? 0x40 : 0));
        m.edge_flag_ = false;
        m.riot_update_irq();
        return v;
    }
    // The timer; reading clears its flag and sets its interrupt enable (A3).
    const uint8_t v = m.riot_timer();
    m.timer_flag_ = false;
    m.timer_ie_ = (r & 8) != 0;
    const int64_t t = m.snd_time_ + m.snd_done_ + m.snd_.elapsed();
    if (t >= m.timer_end_) m.timer_end_ = t + 1 + (int64_t(v) << m.timer_shift_);    // counts on as written
    m.riot_update_irq();
    return v;
}

void Machine::snd_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    addr &= 0x7fff;
    const int at = m.snd_done_ + m.snd_.elapsed();
    switch (addr >> 12) {
    case 0: {
        const int r = addr & 0x1f;
        if (!(r & 4)) {
            switch (r & 3) {
            case 0: m.pa_out_ = v; break;
            case 1: m.pa_ddr_ = v; break;
            case 2: m.pb_out_ = v; break;
            default: m.pb_ddr_ = v; break;
            }
        } else if (r & 0x10) {      // timer: prescaler 1 / 8 / 64 / 1024, A3 = interrupt enable
            static const int kShift[4] = {0, 3, 6, 10};
            m.timer_shift_ = kShift[r & 3];
            m.timer_end_ = m.snd_time_ + at + 1 + (int64_t(v) << m.timer_shift_);
            m.timer_flag_ = false;
            m.timer_ie_ = (r & 8) != 0;
            m.riot_update_irq();
            if (m.timer_ie_) m.snd_.stop();     // a new event
        } else {                    // PA7 edge: A0 = rising, A1 = interrupt enable
            m.edge_pos_ = r & 1;
            m.edge_ie_ = (r & 2) != 0;
            m.riot_update_irq();
        }
        break;
    }
    case 1:     // the DAC
        if (v == m.dac_) break;
        m.audio_to(at);
        m.dac_ = v;
        ++m.dac_writes_;
        break;
    case 2:     // the speech chip: the board inverts the data
        m.audio_to(at);
        m.speech_.write(uint8_t(~v & 0x3f), uint8_t(v >> 6), m.snd_time_ + at);
        m.nmi_line_ = false;
        m.snd_.stop();              // the run ends here: A/R's rise is a new event
        break;
    case 3:     // the speech clock
        m.audio_to(at);
        m.speech_.set_clock(v);
        break;
    default: break;
    }
}

void Machine::sound_to(int main_cycle)
{
    sound_run_to(int(int64_t(main_cycle) * kSoundCyclesPerFrame / kCyclesPerFrame));
}

void Machine::sound_run_to(int target)
{
    while (snd_done_ < target) {
        const int64_t now = snd_time_ + snd_done_;
        // The speech chip asks for the next phoneme: its A/R line rises,
        // and with it the NMI line.
        const int64_t ra = speech_.request_at();
        bool nmi = false;
        if (ra >= 0 && ra <= now) {
            speech_.acknowledge();
            if (!nmi_line_) {
                snd_.nmi();
                nmi = true;     // taken as the next run starts: no skipping now
            }
            nmi_line_ = true;
        }
        if (timer_ie_ && !timer_flag_ && now >= timer_end_) {
            timer_flag_ = true;
            riot_update_irq();
        }
        int64_t stop = snd_time_ + target;
        if (ra > now && ra < stop) stop = ra;
        if (timer_ie_ && !timer_flag_ && timer_end_ < stop) stop = timer_end_;
        int n = int(stop - now);
        if (idle_skip_ && n > kSndSlice) n = kSndSlice;

        if (idle_skip_ && !nmi && snd_.pc >= kSndIdlePc && snd_.pc < kSndIdleEnd && snd_ram_[0x21] == 0 &&
            !snd_.irq_line()) {
            if (snd_.pc != kSndIdlePc) {
                n = 1;      // a slice ended inside the loop: step to its start
            } else if (snd_.a == 0 && snd_ram_[0x20] == 0 && snd_ram_[0x23] == 0) {
                const int turns = int((stop - now) / kSndIdleTurn);
                if (turns > 0) {
                    snd_done_ += turns * kSndIdleTurn;
                    continue;
                }
            }
        }
        const int ran = snd_.run(n);
        snd_done_ += ran;
        snd_run_ += ran;
    }
}

void Machine::audio_to(int snd_cycle)
{
    if (!audio_) return;
    if (snd_cycle > kSoundCyclesPerFrame) snd_cycle = kSoundCyclesPerFrame;
    const int64_t until = int64_t(snd_cycle) * audio_n_;
    while (audio_pos_ < until) {
        const int64_t end = int64_t(audio_done_ + 1) * kSoundCyclesPerFrame;
        const int64_t to = until < end ? until : end;
        dac_acc_ += (to - audio_pos_) * dac_;
        audio_pos_ = to;
        if (to == end) {
            // The DAC level over the sample, AC coupled like the board's output.
            const float x = float(dac_acc_) / kSoundCyclesPerFrame - 128.0f;
            const float y = x - hp_in_ + 0.998f * hp_out_;
            hp_in_ = x;
            hp_out_ = y;
            int v = int(y * 110.0f) + speech_.sample() + knocker_.sample();
            // The three rarely meet; when they do, a soft knee above 16,000
            // keeps the sum under 21,000 (the loudest the players expect).
            const int a = v < 0 ? -v : v;
            if (a > 16000) {
                const int over = a - 16000;
                const int k = 16000 + over * 5000 / (over + 5000);
                v = v < 0 ? -k : k;
            }
            audio_[audio_done_++] = int16_t(v);
            dac_acc_ = 0;
        }
    }
}

// --- frame ---------------------------------------------------------------------------

void Machine::run_main_to(int cycle)
{
    while (fc_ < cycle) {
        if (idle_skip_ && cpu_.ip == kMainIdleIp && cpu_.sreg[i8088::CS] == 0 && !cpu_.nmi_pending() &&
            cpu_.r[i8088::AX] == (ram_[0x80] | ram_[0x81] << 8)) {
            const int turns = (cycle - fc_) / kMainIdleTurn;
            if (turns > 0) {
                fc_ += turns * kMainIdleTurn;
                continue;
            }
        }
        int n = cycle - fc_;
        if (idle_skip_ && n > kMainSlice) n = kMainSlice;
        if (idle_skip_ && cpu_.ip == kMainIdleIp + 4 && cpu_.sreg[i8088::CS] == 0) n = 1;   // at the JZ: one step
        const int ran = cpu_.run(n);
        fc_ += ran;
        main_run_ += ran;
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = audio_ ? n : 0;
    audio_done_ = 0;
    audio_pos_ = 0;
    dac_acc_ = 0;
    main_run_ = snd_run_ = 0;

    // The visible lines, then VBLANK: the picture is taken, the NMI comes.
    run_main_to(kVblankLine * kCyclesPerLine);
    snapshot();
    cpu_.nmi();
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        sound_to(fc_);
        reset();
    }
    run_main_to(kCyclesPerFrame);

    sound_to(kCyclesPerFrame);
    audio_to(kSoundCyclesPerFrame);
    fc_ -= kCyclesPerFrame;
    snd_done_ -= kSoundCyclesPerFrame;
    snd_time_ += kSoundCyclesPerFrame;
    ++frame_;
    audio_ = nullptr;
    return audio_n_;
}

// --- video ---------------------------------------------------------------------------

void Machine::snapshot()
{
    VideoState& vs = video_;
    std::memcpy(vs.vram, vram_, sizeof vs.vram);
    std::memcpy(vs.pens, pens_, sizeof vs.pens);
    vs.bg_on_top = out_ & 0x01;
    vs.flip_x = out_ & 0x02;
    vs.flip_y = out_ & 0x04;
    // Sprites: the hardware draws a sprite's first pixel 4 columns left of
    // its x byte and 13 lines above its y byte (MAME's measured offsets).
    int count = 0;
    for (int i = 0; i < 64; ++i) {
        const uint8_t* s = sprite_ram_ + i * 4;
        const int x = s[1] - 4, y = s[0] - 13;
        if (x <= -16 || y <= -16 || y >= kVblankLine) continue;
        VideoState::Sprite& sp = vs.sprites[count++];
        sp.x = int16_t(x);
        sp.y = int16_t(y);
        sp.code = uint8_t(~s[2]);
    }
    vs.sprite_count = uint8_t(count);
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Display line y = hardware column hx; display x = hardware line.
    const int hx = vs.flip_x ? y : 255 - y;

    // Sprites first, in RAM order (later ones on top); colour 0 is clear.
    // The hardware leaves the first 8 columns without sprites.
    uint8_t spr[kVblankLine];
    bool any = false;
    if (hx >= 8) {
        const uint8_t* planes = roms_.sprites;
        for (int i = 0; i < vs.sprite_count; ++i) {
            const VideoState::Sprite& sp = vs.sprites[i];
            const unsigned dx = unsigned(hx - sp.x);
            if (dx >= 16) continue;
            if (!any) {
                std::memset(spr, 0, sizeof spr);
                any = true;
            }
            const int bit = 7 - (dx & 7);
            const uint8_t* p = planes + sp.code * 32 + (dx >> 3);
            for (int r = 0; r < 16; ++r, p += 2) {
                const unsigned ly = unsigned(sp.y + r);
                if (ly >= unsigned(kVblankLine)) continue;
                const int c = ((p[0] >> bit) & 1) << 3 | ((p[0x2000] >> bit) & 1) << 2 |
                              ((p[0x4000] >> bit) & 1) << 1 | ((p[0x6000] >> bit) & 1);
                if (c) spr[ly] = uint8_t(c);
            }
        }
    }

    // The background: tile column hx / 8, 4-bit pixels two to a byte.
    const uint16_t* pens = vs.pens;
    const int col = hx >> 3, shift = (hx & 1) ? 0 : 4;
    const uint8_t* tiles = roms_.tiles + ((hx & 7) >> 1);
    const uint8_t* codes = vs.vram + col;
    if (!any) {
        for (int row = 0; row < 30; ++row) {
            const uint8_t* t = tiles + codes[row * 32] * 32;
            uint16_t* o = out + row * 8;
            for (int k = 0; k < 8; ++k) o[k] = pens[(t[k * 4] >> shift) & 15];
        }
    } else {
        const bool bg_top = vs.bg_on_top;
        for (int row = 0; row < 30; ++row) {
            const uint8_t* t = tiles + codes[row * 32] * 32;
            uint16_t* o = out + row * 8;
            const uint8_t* sp = spr + row * 8;
            for (int k = 0; k < 8; ++k) {
                const int b = (t[k * 4] >> shift) & 15;
                o[k] = pens[bg_top ? (b ? b : sp[k]) : (sp[k] ? sp[k] : b)];
            }
        }
    }
    if (vs.flip_y)
        for (int a = 0, b = kScreenW - 1; a < b; ++a, --b) {
            const uint16_t t = out[a];
            out[a] = out[b];
            out[b] = t;
        }
}

} // namespace qbert
