// Burger Time machine - see btime.h.
//
// Main CPU memory map:
//   0000-07FF  RAM                   0C00-0C0F  palette RAM
//   1000-13FF  video RAM (character codes; sprites in the first row)
//   1400-17FF  colour RAM (bits 0-1: character bank)
//   1800-1FFF  the same two with x and y swapped
//   4000  read P1 (active low: right left up down, bit 4 the button)
//   4001  read P2 (cocktail)
//   4002  read SYSTEM: starts (active low), coins (bits 6-7, active high)
//         write: video control, bit 0 = flip screen
//   4003  read DSW1 (bit 7 = VBLANK)    write: the sound CPU's command latch
//   4004  read DSW2                     write: background control
//   C000-FFFF  program ROM (B000-BFFF is empty on this set)
// Sound CPU: 0000-03FF RAM (seen 8 times up to 1FFF), 2000 / 4000 AY 1
// data / address, 6000 / 8000 AY 2 data / address (each decoded over
// 8 KB), A000 read the latch, C000 write the NMI enable (bit 0), E000-FFFF
// the 4 KB ROM twice. Facts from MAME's driver (read for facts only).
#include "btime.h"

#include <cmath>

namespace btime {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x4000},
    {"audiocpu", 0x1000},
    {"gfx1",     0x6000},
    {"gfx2",     0x1800},
    {"bg_map",   0x0800},
};

constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x1000, 0x368a25b5, "aa04.9b",  false},
    {kRegionCpu,   0x1000, 0x1000, 0xb4ba400d, "aa06.13b", false},
    {kRegionCpu,   0x2000, 0x1000, 0x8005bffa, "aa05.10b", false},
    {kRegionCpu,   0x3000, 0x1000, 0x086440ad, "aa07.15b", false},
    {kRegionSound, 0x0000, 0x1000, 0xf55e5211, "ab14.12h", false},
    {kRegionGfx,   0x0000, 0x1000, 0xc4617243, "aa12.7k",  false},
    {kRegionGfx,   0x1000, 0x1000, 0xac01042f, "ab13.9k",  false},
    {kRegionGfx,   0x2000, 0x1000, 0x854a872a, "ab10.10k", false},
    {kRegionGfx,   0x3000, 0x1000, 0xd4848014, "ab11.12k", false},
    {kRegionGfx,   0x4000, 0x1000, 0x8650c788, "aa8.13k",  false},
    {kRegionGfx,   0x5000, 0x1000, 0x8dec15e6, "ab9.15k",  false},
    {kRegionTiles, 0x0000, 0x0800, 0xc7a14485, "ab00.1b",  false},
    {kRegionTiles, 0x0800, 0x0800, 0x25b49078, "ab01.3b",  false},
    {kRegionTiles, 0x1000, 0x0800, 0xb8ef56c3, "ab02.4b",  false},
    {kRegionMap,   0x0000, 0x0800, 0xd26bc1f3, "ab03.6b",  false},
};

// Palette RAM bits (inverted) through the resistors to each gun: red and
// green 47k / 33k / 15k, blue 33k / 15k - levels by conductance.
constexpr uint8_t kRGWeights[3] = {46, 65, 144};
constexpr uint8_t kBWeights[2] = {80, 175};

constexpr int kSliceLines = 8;              // the 8V line changes every 8 lines
constexpr int kSliceCycles = kSliceLines * kCyclesPerLine;
constexpr int kFirstLine = 8;               // run_frame() starts at the first visible line

// The mixer (MAME's notes of the board, R49-R52, read for facts): channel
// 2A through a multiple-feedback band-pass, 5k in, 10k to ground, 47k
// feedback, 2 x 68 nF: f0 = 187 Hz, Q 1.88, gain 4.7 (inverting). The other
// five channels are summed at 1/5 each; both paths meet in an op-amp mixer,
// a 10k / 10 uF DC block (1.6 Hz) and the amplifier's 100 uF coupling into
// the speaker, a high-pass at ~530 Hz with a 3 ohm load (MAME's guess, kept:
// without it channel 2A's ~190 Hz thumps are 2-3x louder against the music
// than with it). Only the coupling high-pass is run here (it blocks DC too).
constexpr float kBandF0 = 187.0f, kBandQ = 1.88f, kBandGain = 4.7f;
constexpr float kSpeakerTau = 3.0f * 100e-6f;      // 3 ohm x 100 uF
// Output scale: the loudest sound (2A's thump when the cook is caught)
// peaks near 19,000 in the PC tool's play runs; the music in play only
// near 2,500 - that is the board's balance as far as it is known.
constexpr float kOutGain = 24000.0f;

inline uint32_t word24(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16)); }

// One 8 x 8 character, 3 planes `plane` bytes apart (plane 2 = the high
// bit), one byte a row, bit 7 = left, into display orientation: the
// monitor is turned 90 degrees counter-clockwise, so a display line
// through a character is one bit position (display line j = bit j) and its
// 8 pixels are the 8 rows. Out: per display line, 8 pixels of 3 bits
// (pixel k at bits 3k) in 3 bytes.
void decode_char(const uint8_t* src, int plane, uint8_t* out)
{
    for (int j = 0; j < 8; ++j) {
        uint32_t w = 0;
        for (int k = 0; k < 8; ++k) {
            const uint32_t v = (((src[2 * plane + k] >> j) & 1) << 2) | (((src[plane + k] >> j) & 1) << 1) |
                               ((src[k] >> j) & 1);
            w |= v << (3 * k);
        }
        out[3 * j] = uint8_t(w);
        out[3 * j + 1] = uint8_t(w >> 8);
        out[3 * j + 2] = uint8_t(w >> 16);
    }
}

} // namespace

const romset::Set kRomSet = {
    "btime", nullptr, "Burger Time", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;
    swap_ = swap_rgb565_bytes;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.opcode_hook = opcode;
    cpu_.map_ram(0x0000, 0x800, ram_);
    cpu_.map_ram(0x0c00, 0x100, pal_);
    cpu_.map_ram(0x1000, 0x400, vram_);
    cpu_.map_ram(0x1400, 0x400, cram_);
    cpu_.map_read(0xc000, 0x4000, roms.cpu);
    irq_vector_ = uint16_t(roms.cpu[0x3ffe] | (roms.cpu[0x3fff] << 8));

    snd_.ctx = this;
    snd_.mem_read = snd_read;
    snd_.mem_write = snd_write;
    snd_.opcode_hook = snd_opcode;
    for (int k = 0; k < 8; ++k) snd_.map_ram(uint16_t(k * 0x400), 0x400, snd_ram_);
    snd_.map_read(0xe000, 0x1000, roms.sound);
    snd_.map_read(0xf000, 0x1000, roms.sound);

    // The mixer's filters at the output rate (RBJ band-pass, 0 dB peak).
    const float w0 = 2.0f * 3.14159265f * kBandF0 / float(audio_rate);
    const float alpha = std::sin(w0) / (2.0f * kBandQ);
    const float a0 = 1.0f + alpha;
    bp_b0_ = alpha / a0;
    bp_a1_ = -2.0f * std::cos(w0) / a0;
    bp_a2_ = (1.0f - alpha) / a0;
    dc_r_ = std::exp(-1.0f / (kSpeakerTau * float(audio_rate)));
    for (uint32_t k = 0; k < kInvCount; ++k) inv_[k] = 1.0f / (float(ay8910::kMaxLevel) * float(kInvFirst + k));

    for (uint8_t& p : pal_) p = 0xff;       // black until the program sets it (RAM is random at power-on)
    for (uint8_t& t : slot_of_) t = 0;
    for (uint16_t& o : owner_) o = kNoGlyph;
    hand_ = 0;
    fc_ = 0;
    snd_done_ = 0;
    reset();
}

// The graphics in display orientation, decoded when first drawn: a frame
// shows ~70-110 different characters and sprite quarters and up to 14
// background tiles (56 quarters), so a cache of 255 holds them all and
// decodes well under one a frame (0.35 in play) - 7 KB instead of 30 KB
// for all 1024 + 256.
// Replacement in turn (the oldest goes); a glyph still in use is simply
// decoded again.
const uint8_t* Machine::glyph_miss(int id) const
{
    const int h = hand_;
    hand_ = uint8_t(h + 1 == kSlots ? 0 : h + 1);
    if (owner_[h] != kNoGlyph) slot_of_[owner_[h]] = 0;
    owner_[h] = uint16_t(id);
    slot_of_[id] = uint8_t(h + 1);
    uint8_t* out = cache_ + h * 24;
    if (id < kTileGlyphs) decode_char(roms_.gfx + id * 8, 0x2000, out);
    else decode_char(roms_.tiles + (id - kTileGlyphs) * 8, 0x800, out);
    ++decodes_;
    return out;
}

void Machine::reset()
{
    cpu_.reset();
    cpu_.irq(false);
    snd_.reset();
    snd_.irq(false);
    ay_[0].reset();
    ay_[1].reset();
    latch_ = 0;
    flip_ = false;
    bg_ctrl_ = 0;
    coin_held_ = false;
    fresh_reset_ = true;
    nmi_enable_ = false;
    vck_ = false;
    snd_idle_ = snd_nmi_due_ = false;
    stall_ = 0;
    bp_x1_ = bp_x2_ = bp_y1_ = bp_y2_ = 0;
    dc_x_ = dc_y_ = 0;
    update_pens();
}

void Machine::set_inputs(uint16_t b)
{
    // A coin raises IRQ, held until the CPU takes it.
    if (b & ~buttons_ & (kCoin | kCoin2)) {
        coin_held_ = true;
        cpu_.irq(true);
        ++coin_irqs_;
    }
    buttons_ = b;
}

void Machine::update_pens()
{
    for (int i = 0; i < 16; ++i) {
        const uint8_t d = uint8_t(~pal_[i]);
        int r = 0, g = 0, b = 0;
        for (int k = 0; k < 3; ++k) {
            if (d & (1 << k)) r += kRGWeights[k];
            if (d & (8 << k)) g += kRGWeights[k];
        }
        for (int k = 0; k < 2; ++k)
            if (d & (0x40 << k)) b += kBWeights[k];
        uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        if (swap_) px = uint16_t((px >> 8) | (px << 8));
        pen_[i] = px;
    }
    // Character pixel c over background pixel b: c unless it is 0.
    for (int c = 0; c < 8; ++c)
        for (int b = 0; b < 8; ++b) over_[c * 8 + b] = c ? pen_[c] : pen_[8 + b];
}

// --- main CPU bus --------------------------------------------------------------

// 1800-1FFF: offset x * 32 + y reaches x + y * 32.
inline int swapped(uint16_t addr)
{
    const int o = addr & 0x3ff;
    return ((o & 31) << 5) | (o >> 5);
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0x1800) return (addr & 0x400) ? m.cram_[swapped(addr)] : m.vram_[swapped(addr)];
    switch (addr) {
    case 0x4000: {
        uint8_t v = 0xff;
        if (m.buttons_ & kRight)  v &= ~0x01;
        if (m.buttons_ & kLeft)   v &= ~0x02;
        if (m.buttons_ & kUp)     v &= ~0x04;
        if (m.buttons_ & kDown)   v &= ~0x08;
        if (m.buttons_ & kPepper) v &= ~0x10;
        return v;
    }
    case 0x4001: return 0xff;               // player 2's panel (cocktail only)
    case 0x4002: {
        uint8_t v = 0x3f;
        if (m.buttons_ & kStart1) v &= ~0x01;
        if (m.buttons_ & kStart2) v &= ~0x02;
        if (m.buttons_ & kCoin)   v |= 0x40;
        if (m.buttons_ & kCoin2)  v |= 0x80;
        return v;
    }
    case 0x4003: return m.read_dsw1();
    case 0x4004: return m.dsw2_;
    default:     return 0x00;               // unmapped, empty ROM socket
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0x1800) {
        ((addr & 0x400) ? m.cram_ : m.vram_)[swapped(addr)] = v;
        return;
    }
    switch (addr) {
    case 0x4002: m.flip_ = v & 1; break;
    case 0x4003:
        m.sound_to(m.now());
        m.latch_ = v;
        m.snd_.irq(true);
        break;
    case 0x4004: m.bg_ctrl_ = v; break;
    default: break;                         // ROM, unmapped
    }
}

// DSW1 with VBLANK in bit 7. The program waits for VBLANK to start or end
// in two-instruction loops (LDA 4003 / BPL or BMI back to it): such a wait
// is skipped to the read that sees the change - the CPU is held for the
// loops it would have run (same cycles, same result), unless an IRQ could
// come in meanwhile.
uint8_t Machine::read_dsw1()
{
    const int t = now();
    const bool vblank = t >= kVisibleLines * kCyclesPerLine;
    const uint8_t v = uint8_t(kDsw1 | (vblank ? 0x80 : 0));
    const uint16_t pc = cpu_.pc;                // after LDA's operand
    if (pc < 0xc003 || pc > 0xfffd) return v;
    const uint8_t* rom = roms_.cpu - 0xc000;
    const uint8_t br = rom[pc];
    if (rom[pc - 3] != 0xad || rom[pc - 2] != 0x03 || rom[pc - 1] != 0x40 || rom[pc + 1] != 0xfb ||
        (br != 0x10 && br != 0x30))
        return v;
    if ((br == 0x10) == vblank) return v;       // BPL waits for VBLANK, BMI for its end
    if (cpu_.irq_line() && !(cpu_.p() & m6502::IF)) return v;
    const int edge = vblank ? kCyclesPerFrame : kVisibleLines * kCyclesPerLine;
    const int loop = 4 + 3 + (((pc + 2) ^ (pc - 3)) >> 8 ? 1 : 0);     // LDA abs + branch taken
    const int reads = (edge - t + loop - 1) / loop;     // the read that sees the change
    if (reads > 1) {
        stall_ += (reads - 1) * loop;
        cpu_.stop();
    }
    return v;
}

uint8_t Machine::opcode(void* ctx, uint16_t addr, uint8_t op, bool wrote)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (m.coin_held_ && addr == m.irq_vector_) {    // the IRQ was taken
        m.coin_held_ = false;
        m.cpu_.irq(false);
    }
    if (m.fresh_reset_) {
        m.fresh_reset_ = false;
        wrote = false;
    }
    return deco7_decrypt(addr, op, wrote);
}

// --- sound CPU -------------------------------------------------------------------

// The sound program idles in a jump to itself between interrupts: the CPU
// stops there, and sound_to() skips the rest of the stretch (an interrupt
// due would have been taken before this fetch).
uint8_t Machine::snd_opcode(void* ctx, uint16_t addr, uint8_t op, bool)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (op == 0x4c && addr >= 0xe000 && m.snd_.rd(uint16_t(addr + 1)) == uint8_t(addr) &&
        m.snd_.rd(uint16_t(addr + 2)) == uint8_t(addr >> 8)) {
        m.snd_idle_ = true;
        m.snd_.stop();
    }
    return op;
}

uint8_t Machine::snd_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xe000) == 0xa000) {
        m.snd_.irq(false);
        return m.latch_;
    }
    return 0x00;
}

void Machine::snd_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const int chip = (addr & 0xe000) >= 0x6000 ? 1 : 0;
    switch (addr & 0xe000) {
    case 0x2000: case 0x6000:
        m.audio_to((m.snd_done_ + m.snd_.elapsed()) * kSoundDivider);
        m.ay_[chip].write_data(v);
        break;
    case 0x4000: case 0x8000:
        m.ay_[chip].write_address(v);
        break;
    case 0xc000: {
        const bool was = m.nmi_enable_ && m.vck_;
        m.nmi_enable_ = v & 1;
        if (!was && m.nmi_enable_ && m.vck_) {
            m.snd_.nmi();
            m.snd_nmi_due_ = true;
            ++m.nmis_;
        }
        break;
    }
    default: break;
    }
}

void Machine::set_vck(bool v)
{
    const bool was = nmi_enable_ && vck_;
    vck_ = v;
    if (!was && nmi_enable_ && vck_) {
        snd_.nmi();
        snd_nmi_due_ = true;
        ++nmis_;
    }
}

void Machine::sound_to(int main_cycle)
{
    const int target = main_cycle / kSoundDivider;
    while (snd_done_ < target) {
        // Idle in its loop with no interrupt to take: nothing happens until
        // the next one, which comes from outside sound_to().
        if (snd_idle_ && !snd_nmi_due_ && !(snd_.irq_line() && !(snd_.p() & m6502::IF))) {
            snd_done_ = target;
            break;
        }
        snd_idle_ = false;
        snd_done_ += snd_.run(target - snd_done_);
        snd_nmi_due_ = false;                   // taken in the run's first step
    }
}

// --- audio -----------------------------------------------------------------------

int16_t Machine::mix_sample(const uint32_t* acc, uint32_t clocks)
{
    // A sample is ~62.5 clocks at 24 kHz: 1 / (level x clocks) from a table.
    const float scale = clocks - kInvFirst < kInvCount ? inv_[clocks - kInvFirst]
                                                       : 1.0f / (float(ay8910::kMaxLevel) * float(clocks ? clocks : 1));
    const float direct = 0.2f * scale * float(acc[0] + acc[1] + acc[2] + acc[4] + acc[5]);
    float y = 0;
    if (acc[3] || bp_y1_ != 0 || bp_y2_ != 0 || bp_x1_ != 0) {     // 2A or its filter still ringing
        const float x = scale * float(acc[3]);
        y = bp_b0_ * (x - bp_x2_) - bp_a1_ * bp_y1_ - bp_a2_ * bp_y2_;
        if (y > -1e-6f && y < 1e-6f && x == 0) y = 0;            // rung out
        bp_x2_ = bp_x1_;
        bp_x1_ = x;
        bp_y2_ = bp_y1_;
        bp_y1_ = y;
    }
    const float in = direct - kBandGain * y;
    const float out = dc_r_ * (dc_y_ + in - dc_x_);    // one-pole high-pass
    dc_x_ = in;
    dc_y_ = out;
    float s = out * kOutGain;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return int16_t(s);
}

void Machine::audio_to(int main_cycle)
{
    if (main_cycle > kCyclesPerFrame) main_cycle = kCyclesPerFrame;
    while (audio_clock_ < main_cycle) {
        const int end = audio_done_ < audio_n_
                            ? int(int64_t(audio_done_ + 1) * kCyclesPerFrame / audio_n_)
                            : kCyclesPerFrame;
        const int to = main_cycle < end ? main_cycle : end;
        ay_[0].run(uint32_t(to - audio_clock_), acc_);
        ay_[1].run(uint32_t(to - audio_clock_), acc_ + 3);
        audio_clock_ = to;
        if (to == end && audio_done_ < audio_n_) {
            const int16_t s = mix_sample(acc_, uint32_t(end - sample_start_));
            if (audio_) audio_[audio_done_] = s;
            ++audio_done_;
            sample_start_ = end;
            for (uint32_t& a : acc_) a = 0;
        }
    }
}

// --- frame ---------------------------------------------------------------------------

int Machine::run_frame(int16_t* audio, int cap)
{
    // Samples for this frame, keeping the remainder so the long-run rate is exact.
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

    int executed = 0;
    for (int s = 0; s < kLinesPerFrame / kSliceLines; ++s) {
        const int line = (kFirstLine + s * kSliceLines) % kLinesPerFrame;
        set_vck((line >> 3) & 1);
        const int end = (s + 1) * kSliceCycles;
        while (fc_ < end) {
            const int ran = cpu_.run(end - fc_);
            executed += ran;
            fc_ += ran + stall_;
            stall_ = 0;
        }
        sound_to(end);
    }
    audio_to(kCyclesPerFrame);
    main_cycles_ = uint32_t(executed);

    fc_ -= kCyclesPerFrame;
    snd_done_ -= kSoundCyclesPerFrame;
    ++frame_;
    update_pens();
    const int done = audio_ ? audio_n_ : 0;
    audio_ = nullptr;
    return done;
}

// --- video -----------------------------------------------------------------------------

// Display line y is hardware column 247 - y, display x is hardware line
// x + 8 (with flip screen the whole picture turns 180 degrees).
void Machine::render_line(int y, uint16_t* out) const
{
    const int yy = flip_ ? kScreenH - 1 - y : y;

    // Characters: video RAM row 1 + y / 8 (offset / 32), columns 1-30 (row
    // and column 0 aren't shown; the sprites live in column 0).
    const int row = 1 + (yy >> 3), j = yy & 7;
    const uint8_t* v = vram_ + row * 32 + 1;
    const uint8_t* cr = cram_ + row * 32 + 1;
    if (!(bg_ctrl_ & 0x10)) {
        for (int g = 0; g < 30; ++g) {
            uint32_t w = word24(glyph(v[g] | ((cr[g] & 3) << 8)) + 3 * j);
            uint16_t* o = out + g * 8;
            for (int k = 0; k < 8; ++k, w >>= 3) o[k] = pen_[w & 7];
        }
    } else {
        // Over the background (16 x 16 tiles, the map page by bits 0-2 of
        // the control; hardware column c of tiles starts at x = 239 - 16c),
        // where character pixel 0 is clear. Display x 8g..8g+7 is half
        // (g + 1) & 1 of tile row (g + 1) / 2 (row 0's first half is off
        // the left edge).
        const int page = (((flip_ ? 0 : 1) + (bg_ctrl_ & 3)) & 3) | (bg_ctrl_ & 4);
        const int c = (yy + 7) >> 4;
        const int sx = 8 - yy + 16 * c;
        const int half = sx < 8 ? 2 : 0;
        const int bj = 7 - (sx & 7);
        const uint8_t* map = roms_.map + page * 256 + c * 16;
        const uint16_t* bg_pen = pen_ + 8;
        for (int g = 0; g < 30; ++g) {
            const int r = (g + 1) >> 1;
            uint32_t b = word24(glyph(kTileGlyphs + map[r] * 4 + half + ((g + 1) & 1)) + 3 * bj);
            uint32_t w = word24(glyph(v[g] | ((cr[g] & 3) << 8)) + 3 * j);
            uint16_t* o = out + g * 8;
            if (!w) {                       // a blank character (most of them)
                for (int k = 0; k < 8; ++k, b >>= 3) o[k] = bg_pen[b & 7];
            } else {
                for (int k = 0; k < 8; ++k, w >>= 3, b >>= 3) o[k] = over_[((w & 7) << 3) | (b & 7)];
            }
        }
    }

    // Sprites: 8, at video RAM 0x80 apart: flags (bit 0 on, 1 flip y, 2
    // flip x), code, y, x every 0x20. Later ones on top; pixel 0 clear.
    for (int i = 0; i < 8; ++i) {
        const uint8_t* s = vram_ + i * 0x80;
        const uint8_t f = s[0];
        if (!(f & 1)) continue;
        const int sx = 7 - yy + s[0x60];
        if (sx < 0 || sx > 15) continue;
        const int gx = (f & 4) ? 15 - sx : sx;
        const int id = s[0x20] * 4 + (gx < 8 ? 2 : 0), line = 3 * (7 - (gx & 7));
        const uint32_t top = word24(glyph(id) + line);
        const uint32_t bot = word24(glyph(id + 1) + line);
        if (!top && !bot) continue;
        const int x0 = 231 - s[0x40];
        for (int sy = 0; sy < 16; ++sy) {
            const int x = (x0 + sy) & 0xff;
            if (x >= kScreenW) continue;
            const int gy = (f & 2) ? 15 - sy : sy;
            const int p = int((gy < 8 ? top >> (3 * gy) : bot >> (3 * (gy - 8))) & 7);
            if (p) out[x] = pen_[p];
        }
    }

    if (flip_)
        for (int a = 0, b = kScreenW - 1; a < b; ++a, --b) {
            const uint16_t t = out[a];
            out[a] = out[b];
            out[b] = t;
        }
}

} // namespace btime
