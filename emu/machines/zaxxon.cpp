// Zaxxon machine - see zaxxon.h.
//
// Memory map (the main board, set 1):
//   0000-4FFF  program ROM (3, 2, 1); 5000-5FFF reads 0 (no ROM there)
//   6000-6FFF  work RAM
//   8000-83FF  character codes, 32 x 32 (mirrored to 9FFF)
//   A000-A0FF  sprite RAM, 32 x 4 bytes used (mirrored to BFFF)
//   C000-DFFF  read: C000 SW00 (stick, fire), C001 SW01 (cocktail stick),
//              C002 SW1 DIPs, C003 SW2 coinage (A0-A1, A8-A10 = 0);
//              C100 SW100 (starts, coin latches) (A8 = 1)
//              write: C000-C007 74LS259 U55, bit = A0-A2: 0-1 coin
//              enables, 2 service enable, 3-4 coin counters, 6 flip screen
//   E000-FFFF  E03C-E03F the 8255 (ports A-C = sound, then its control);
//              E0F0-E0F3 / E0F8-E0FB the 74LS259 U56 (bit = A0-A1 + 4 x A3,
//              data bit 0): 0 IRQ enable, 1 character colour bank, 6
//              background colour bank, 7 background on; E0F8 / E0F9 also
//              the background position's low 8 / high 3 bits
//   (A8-A12 aren't decoded above E000: E13C, FF3C... are the same).
//
// Interrupts: VBLANK raises the IRQ (IM 1) if U56 bit 0 is set; it stays
// raised until the game writes bit 0 = 0 (its handler does). The service
// switch pulses NMI (test mode). No watchdog.
//
// Coins: a coin pulse sets its latch (read in SW100 bits 5-7) while its
// enable bit is set; writing the enable bit 0 clears it.
#include "zaxxon.h"

#include "../rom/crc32.h"

#include <cstring>

namespace zaxxon {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",     0x5000},
    {"gfx_tx",      0x1000},
    {"gfx_bg",      0x6000},
    {"gfx_spr",     0x6000},
    {"tilemap_dat", 0x8000},
    {"proms",       0x0200},
};

const char* const kVariants[] = {"zaxxon", "zaxxon2"};

// CRCs as in MAME's "zaxxon" (set 1, rev D - its ROMs carry a red "D";
// variant 1) and "zaxxon2" (set 2, variant 2: program ROMs 3 and 1 and
// the character colour PROM differ). Older MAME versions name set 1's
// files zaxxon.1 ... zaxxon.15, zaxxon.u72, zaxxon.u98; matching is by CRC.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,     0x0000, 0x2000, 0x6e2b4a30, "zaxxon_rom3d.u27", false, 1},
    {kRegionCpu,     0x4000, 0x1000, 0x1c123ef9, "zaxxon_rom1d.u29", false, 1},
    {kRegionProms,   0x0100, 0x0100, 0xdeaa21f7, "zaxxon.u72",       false, 1},
    {kRegionCpu,     0x0000, 0x2000, 0xb18e428a, "zaxxon_rom3a.u27", false, 2},
    {kRegionCpu,     0x4000, 0x1000, 0x1977d933, "zaxxon_rom1a.u29", false, 2},
    {kRegionProms,   0x0100, 0x0100, 0xa9e1fb43, "mro17.u41",        false, 2},
    {kRegionCpu,     0x2000, 0x2000, 0x1c9ea398, "zaxxon_rom2d.u28", false},
    {kRegionChars,   0x0000, 0x0800, 0x07bf8c52, "zaxxon_rom14.u68", false},
    {kRegionChars,   0x0800, 0x0800, 0xc215edcb, "zaxxon_rom15.u69", false},
    {kRegionBgGfx,   0x0000, 0x2000, 0x6e07bb68, "zaxxon_rom6.u113", false},
    {kRegionBgGfx,   0x2000, 0x2000, 0x0a5bce6a, "zaxxon_rom5.u112", false},
    {kRegionBgGfx,   0x4000, 0x2000, 0xa5bf1465, "zaxxon_rom4.u111", false},
    {kRegionSprites, 0x0000, 0x2000, 0xeaf0dd4b, "zaxxon_rom11.u77", false},
    {kRegionSprites, 0x2000, 0x2000, 0x1c5369c7, "zaxxon_rom12.u78", false},
    {kRegionSprites, 0x4000, 0x2000, 0xab4e8a9a, "zaxxon_rom13.u79", false},
    {kRegionMap,     0x0000, 0x2000, 0x28d65063, "zaxxon_rom8.u91",  false},
    {kRegionMap,     0x2000, 0x2000, 0x6284c200, "zaxxon_rom7.u90",  false},
    {kRegionMap,     0x4000, 0x2000, 0xa95e61fd, "zaxxon_rom10.u93", false},
    {kRegionMap,     0x6000, 0x2000, 0x7e42691f, "zaxxon_rom9.u92",  false},
    {kRegionProms,   0x0000, 0x0100, 0x6cc6695b, "mro16.u76",        false},
};

// Each graphics ROM set holds its bit planes one after the other: the
// pixel value's high bit in the last plane.
constexpr int kBgPlane = 0x2000, kSpritePlane = 0x2000, kCharPlane = 0x800;
constexpr int kSpriteBytes = 128;       // 32 x 32 pixels, one plane
constexpr int kSpriteCodes = 64;

// Palette PROM bits -> 8-bit levels: red and green 1K / 470 / 220 ohm,
// blue 470 / 220 ohm, each with a 470 ohm pull-down; scaled together so
// full red / green is 255 (blue tops out at ~244).
void palette_weights(double rg[3], double b[2])
{
    const double grg[3] = {1 / 1000.0, 1 / 470.0, 1 / 220.0}, gb[2] = {1 / 470.0, 1 / 220.0};
    const double pd = 1 / 470.0;
    const double sum_rg = grg[0] + grg[1] + grg[2] + pd, sum_b = gb[0] + gb[1] + pd;
    const double scale = 255.0 / ((grg[0] + grg[1] + grg[2]) / sum_rg);
    for (int i = 0; i < 3; ++i) rg[i] = grg[i] / sum_rg * scale;
    for (int i = 0; i < 2; ++i) b[i] = gb[i] / sum_b * scale;
}

// The game's main loop (01F4-020F) waits for the next frame by running its
// task queue; with nothing queued, one pass is
//   01F4 LD D,60 ; LD HL,6031 ; LD E,(HL) ; LD A,(DE) ; ADD A,A ;
//        JR NC,0228 (not taken: the queue entry's bit 7 = empty)
//   01FE CALL 0253: LD A,(600D) ; LD B,A ; AND 0F ; RET NZ
//   0201 CALL 028E: LD DE,606C ; LD A,(DE) ; AND A ; RET NZ, or with 606C
//        = 0: LD HL,6034 ; LD A,(6006) ; AND A ; JR Z,02A0 ; LD HL,6037 ;
//        02A0 LD A,(HL) ; LD HL,6013 ; CP (HL) ; RET C
//   0204 LD HL,600C ; INC (HL) ; LD HL,6010 ; LD A,(600D) ; CP (HL) ;
//        JR Z,01F4 (until the IRQ handler changes 600D)
// 206, 274 or 279 T-states and 22, 30 or 31 opcode fetches; nothing
// changes but the count at 600C (and the stack slot both CALLs write, the
// same each pass). Set 1's program has this loop at 01F4-02A5 (CRC-32 of
// those bytes below); another program skips nothing.
constexpr uint16_t kLoopTop = 0x01f4, kLoopEnd = 0x02a6;
constexpr uint32_t kLoopCrc = 0xfd360418;
constexpr int kSlice = 512;

} // namespace

const romset::Set kRomSet = {
    "zaxxon", nullptr, "Zaxxon", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = port_read;
    cpu_.port_out = port_write;
    cpu_.map_read(0x0000, 0x5000, roms.cpu);
    cpu_.map_ram(0x6000, 0x1000, ram_);
    for (uint32_t a = 0x8000; a < 0xa000; a += 0x400) cpu_.map_ram(uint16_t(a), 0x400, fg_ram_);

    sound_.init(audio_rate);

    double rg[3], b[2];
    palette_weights(rg, b);
    for (int e = 0; e < 256; ++e) {
        const uint8_t v = roms.proms[e];
        const int r = int(rg[0] * (v & 1) + rg[1] * ((v >> 1) & 1) + rg[2] * ((v >> 2) & 1) + 0.5);
        const int g = int(rg[0] * ((v >> 3) & 1) + rg[1] * ((v >> 4) & 1) + rg[2] * ((v >> 5) & 1) + 0.5);
        const int bl = int(b[0] * ((v >> 6) & 1) + b[1] * ((v >> 7) & 1) + 0.5);
        uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3));
        if (swap_rgb565_bytes) px = uint16_t((px >> 8) | (px << 8));
        pen_[e] = px;
    }
    // Character colours by position: the PROM at (column + 32 x (row / 4)).
    for (int i = 0; i < 256; ++i) char_colour_[i] = uint8_t((roms.proms[0x100 + i] & 0x0f) * 8);

    loop_known_ = crc32::of(roms.cpu + kLoopTop, kLoopEnd - kLoopTop) == kLoopCrc;
    decode_chars();
    std::memset(bg_tag_, 0, sizeof bg_tag_);
    // Which sprite columns draw anything (most sprites are narrower than 32).
    for (int code = 0; code < kSpriteCodes; ++code) {
        uint32_t used = 0;
        for (int col = 0; col < 32; ++col) {
            const uint8_t* g = roms.sprites + code * kSpriteBytes + (col >> 3) * 8;
            for (int gy = 0; gy < 32; ++gy) {
                const int off = (gy & 7) + (gy >> 3) * 32;
                if ((g[off] | g[kSpritePlane + off] | g[2 * kSpritePlane + off]) & (0x80 >> (col & 7))) used |= 1u << col;
            }
        }
        sprite_cols_used_[code] = used;
    }
    reset();
}

// Characters: byte (code * 8 + row) in each plane, bit 7 - x = pixel x;
// stored by board column x, row y in bits 2y+1..2y.
void Machine::decode_chars()
{
    for (int code = 0; code < 256; ++code)
        for (int x = 0; x < 8; ++x) {
            uint16_t bits = 0;
            for (int y = 0; y < 8; ++y) {
                const int lo = (roms_.chars[code * 8 + y] >> (7 - x)) & 1;
                const int hi = (roms_.chars[kCharPlane + code * 8 + y] >> (7 - x)) & 1;
                bits |= uint16_t(((hi << 1) | lo) << (2 * y));
            }
            char_cols_[code * 8 + x] = bits;
        }
}

void Machine::reset()
{
    cpu_.reset();
    cpu_.irq(false);
    sound_.reset();
    latch1_ = latch2_ = 0;
    bg_pos_ = 0;
    ppi_[0] = ppi_[1] = ppi_[2] = 0xff;
    coin_status_ = 0;
    pos_ = 0;
}

void Machine::set_inputs(uint16_t buttons)
{
    const uint16_t pressed = uint16_t(buttons & ~buttons_);
    buttons_ = buttons;
    // A coin pulse sets its latch only while the game enables that slot.
    if ((pressed & kCoin) && (latch1_ & 0x01)) coin_status_ |= 0x01;
    if ((pressed & kCoin2) && (latch1_ & 0x02)) coin_status_ |= 0x02;
    if ((pressed & kService) && (latch1_ & 0x04)) coin_status_ |= 0x04;
    if (pressed & kTest) cpu_.nmi();
}

uint8_t Machine::sw100() const
{
    uint8_t v = uint8_t(coin_status_ << 5);         // active high
    if (buttons_ & kStart1) v |= 0x04;
    if (buttons_ & kStart2) v |= 0x08;
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (addr < 0x6000) return 0x00;                 // 5000-5FFF: no ROM
    if ((addr & 0xe000) == 0xa000) return m.sprite_ram_[addr & 0xff];
    if ((addr & 0xe000) == 0xc000) {
        if ((addr & 0x0700) == 0x0100) return m.sw100();
        if ((addr & 0x0700) != 0) return 0xff;
        switch (addr & 3) {
        case 0: {                                   // SW00, active high
            uint8_t v = 0;
            if (m.buttons_ & kRight) v |= 0x01;
            if (m.buttons_ & kLeft)  v |= 0x02;
            if (m.buttons_ & kDown)  v |= 0x04;
            if (m.buttons_ & kUp)    v |= 0x08;
            if (m.buttons_ & kFire)  v |= 0x10;
            return v;
        }
        case 1:  return 0x00;                       // SW01: the cocktail stick
        case 2:  return m.sw1_;
        default: return kCoinageDefault;
        }
    }
    if ((addr & 0xe0fc) == 0xe03c) return (addr & 3) < 3 ? m.ppi_[addr & 3] : 0xff;
    return 0xff;
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xe000) == 0xa000) { m.sprite_ram_[addr & 0xff] = v; return; }
    if ((addr & 0xe000) == 0xc000) {
        if ((addr & 0x0700) != 0) return;
        const int bit = addr & 7;
        const uint8_t mask = uint8_t(1 << bit);
        m.latch1_ = (v & 1) ? uint8_t(m.latch1_ | mask) : uint8_t(m.latch1_ & ~mask);
        if (bit < 3 && !(v & 1)) m.coin_status_ &= uint8_t(~mask);     // enable off clears the latch
        return;
    }
    if ((addr & 0xe000) != 0xe000) return;          // ROM, the empty blocks
    if ((addr & 0xfc) == 0x3c) { m.ppi_write(addr & 3, v); return; }
    if ((addr & 0xf4) == 0xf0) {
        const bool a3 = addr & 8;
        const int bit = (a3 ? 4 : 0) | (addr & 3);
        const uint8_t mask = uint8_t(1 << bit);
        m.latch2_ = (v & 1) ? uint8_t(m.latch2_ | mask) : uint8_t(m.latch2_ & ~mask);
        if (bit == 0 && !(v & 1)) m.cpu_.irq(false);    // IRQ enable off clears the IRQ
        if (a3 && !(addr & 2)) {
            if (addr & 1) m.bg_pos_ = uint16_t((m.bg_pos_ & 0x0ff) | ((v & 7) << 8));
            else          m.bg_pos_ = uint16_t((m.bg_pos_ & 0x700) | v);
        }
    }
}

// The 8255 in mode 0, all ports outputs; bit set / reset reaches port C.
// Writing a mode clears the outputs, but the game writes FF to all three
// a few microseconds later (01E2, then 19CB): that blip isn't passed to the
// sound models, which would start every one shot at power-on (assumed:
// the real board's one shots may well fire then, unheard behind the amp
// coming up).
void Machine::ppi_write(int reg, uint8_t v)
{
    audio_to(pos_ + cpu_.elapsed());                // up to now it sounded as before
    if (reg < 3) {
        ppi_[reg] = v;
    } else if (v & 0x80) {
        ppi_[0] = ppi_[1] = ppi_[2] = 0;
        return;
    } else {
        const uint8_t mask = uint8_t(1 << ((v >> 1) & 7));
        ppi_[2] = (v & 1) ? uint8_t(ppi_[2] | mask) : uint8_t(ppi_[2] & ~mask);
        reg = 2;
    }
    switch (reg) {
    case 0:  sound_.write_a(ppi_[0]); break;
    case 1:  sound_.write_b(ppi_[1]); break;
    default: sound_.write_c(ppi_[2]); break;
    }
}

uint8_t Machine::port_read(void*, uint16_t) { return 0xff; }
void    Machine::port_write(void*, uint16_t, uint8_t) {}

void Machine::vblank()
{
    VideoState& vs = video_;
    std::memcpy(vs.fg, fg_ram_, sizeof vs.fg);
    vs.bg_pos = bg_pos_;
    vs.bg_on = (latch2_ & 0x80) != 0;
    vs.bg_bank = (latch2_ & 0x40) ? 0x80 : 0;
    vs.fg_bank = (latch2_ & 0x02) ? 0x80 : 0;
    // Sprites, 0x7C down to 0 (sprite 0 on top); those wholly off the
    // visible lines are left out.
    int n = 0;
    for (int offs = 0x7c; offs >= 0; offs -= 4) {
        const uint8_t* s = sprite_ram_ + offs;
        const int top = sprite_top(s[0]);
        if (top == kVblankLine) continue;           // lines 240-255 and 0-15: all hidden
        VideoState::Sprite& d = vs.sprite[n++];
        d.x = uint8_t(sprite_left(s[3]));
        d.y = uint8_t(top);
        d.code = uint8_t(s[1] & 0x3f);
        d.flags = uint8_t(s[1] >> 6);
        d.pens = uint8_t((s[2] & 0x1f) * 8);
    }
    vs.sprite_count = uint8_t(n);

    if (latch2_ & 0x01) cpu_.irq(true, 0xff);
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    sound_.render(audio_ + audio_done_, target - audio_done_);
    audio_done_ = target;
}

// Whether a pass of the main loop from its top would be an idle pass (see
// kLoopTop), which repeats unchanged until the IRQ: its T-states and
// opcode fetches, or false.
bool Machine::idle_pass(int& cycles, int& fetches) const
{
    if (cpu_.pc.w != kLoopTop || cpu_.irq_line()) return false;
    const uint8_t frame = ram_[0x00d];
    if (!(ram_[ram_[0x031]] & 0x80) || !(frame & 0x0f) || frame != ram_[0x010]) return false;
    if (ram_[0x06c]) {
        cycles = 206;
        fetches = 22;
        return true;
    }
    const bool second = ram_[0x006] != 0;
    if (ram_[second ? 0x037 : 0x034] >= ram_[0x013]) return false;
    cycles = second ? 279 : 274;
    fetches = second ? 31 : 30;
    return true;
}

// Runs the CPU in slices. In the main loop's idle pass, whole passes but
// the last are skipped (their time, their opcode fetches in R and their
// count at 600C added as if run), so the CPU runs the rest itself and
// ends the slice exactly where it would have.
void Machine::run_to(int cycle)
{
    while (pos_ < cycle) {
        const int left = cycle - pos_;
        pos_ += cpu_.run(left < kSlice ? left : kSlice);
        if (!idle_skip_ || !loop_known_ || pos_ >= cycle) continue;
        // In the loop: step to its top (a pass's instructions at most).
        const uint16_t pc = cpu_.pc.w;
        const bool in_loop = (pc >= kLoopTop && pc <= 0x020f) || (pc >= 0x0253 && pc <= 0x025a)
                          || (pc >= 0x028e && pc <= 0x02a5);
        if (!in_loop) continue;
        for (int i = 0; i < 31 && pos_ < cycle && cpu_.pc.w != kLoopTop; ++i) pos_ += cpu_.run(1);
        int pass_cycles, pass_fetches;
        if (!idle_pass(pass_cycles, pass_fetches)) continue;
        const int passes = (cycle - pos_) / pass_cycles - 1;
        if (passes <= 0) continue;
        pos_ += passes * pass_cycles;
        cpu_.r = uint8_t(cpu_.r + passes * pass_fetches);
        ram_[0x00c] = uint8_t(ram_[0x00c] + passes);
        idle_cycles_ += uint64_t(passes) * pass_cycles;
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // Samples for this frame, keeping the remainder so the long-run rate is exact.
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio;
    audio_n_ = audio ? n : 0;
    audio_done_ = 0;

    pos_ -= kCyclesPerFrame;    // the last frame's overrun (0 or a few cycles past)
    if (pos_ < 0) pos_ = 0;
    run_to(kVblankLine * kCyclesPerLine);
    vblank();
    run_to(kCyclesPerFrame);
    ++frame_;

    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    return audio ? n : 0;
}

// A background tile decoded into the cache: 8 words, one a column, row y
// in bits 4y+2..4y.
void Machine::decode_bg_tile(int code, uint32_t* cols) const
{
    ++bg_misses_;
    const uint8_t* p = roms_.bg + code * 8;
    for (int x = 0; x < 8; ++x) cols[x] = 0;
    for (int y = 0; y < 8; ++y) {
        const unsigned b0 = p[y], b1 = p[kBgPlane + y], b2 = p[2 * kBgPlane + y];
        for (int x = 0; x < 8; ++x) {
            const int s = 7 - x;
            cols[x] |= uint32_t(((b0 >> s) & 1) | (((b1 >> s) & 1) << 1) | (((b2 >> s) & 1) << 2)) << (4 * y);
        }
    }
}

// A character column (8 rows x 2 bits) with no see-through pixel.
static inline bool opaque(uint32_t bits) { return ((bits | (bits >> 1)) & 0x5555) == 0x5555; }

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Display line y is board column y; display x is board line 239 - x.
    const int bx = y;
    const int tx = bx >> 3;
    const uint16_t* char_cols = char_cols_ + (bx & 7);

    // The characters are drawn last; where they cover whole tiles at either
    // end of the line (the score and fuel lines are all black or lettered
    // characters, the altitude meter's), nothing under them is drawn.
    int top = 2, bottom = 29;                       // the tile rows not covered
    while (top <= bottom && opaque(char_cols[vs.fg[top * 32 + tx] * 8])) ++top;
    while (bottom >= top && opaque(char_cols[vs.fg[bottom * 32 + tx] * 8])) --bottom;
    // Pixel pairs (board lines 239 / 238, 237 / 236, ...) k0 to k1.
    const int k0 = 116 - 4 * bottom, k1 = 120 - 4 * top;

    // Background: a pair shares a picture column and is rows r + 1 / r of
    // one tile; each pair steps one column right and two rows up the picture.
    if (k0 >= k1) {
    } else if (!vs.bg_on) {
        for (int x = 2 * k0; x < 2 * k1; ++x) out[x] = 0;
    } else {
        const uint8_t* map = roms_.map;
        int sx = (bg_column(bx, 238) + k0) & 255, sy = (bg_row(vs.bg_pos, 238) - 2 * k0) & 4095;
        uint16_t* o = out + 2 * k0;
        for (int k = k0; k < k1;) {
            // A run of pairs in one tile: to its right edge or its top row.
            const int cx = sx & 7, ry = sy & 7;
            int n = 8 - cx;
            if ((ry >> 1) + 1 < n) n = (ry >> 1) + 1;
            if (k1 - k < n) n = k1 - k;
            const int idx = (sy >> 3) * 32 + (sx >> 3);
            const unsigned attr = map[0x4000 + idx];
            const int code = map[idx] | ((attr & 3) << 8);
            const int slot = (code ^ (code >> 8) * 0x47) & (kBgCacheSlots - 1);
            uint32_t* tile = bg_cache_ + slot * 8;
            if (bg_tag_[slot] != code + 1) {
                bg_tag_[slot] = uint16_t(code + 1);
                decode_bg_tile(code, tile);
            }
            const uint32_t* cols = tile + cx;
            const uint16_t* pens = pen_ + vs.bg_bank + (attr >> 4) * 8;
            for (int i = 0, r = 4 * ry; i < n; ++i, r -= 8) {
                const uint32_t v = cols[i] >> r;
                o[2 * i] = pens[(v >> 4) & 7];
                o[2 * i + 1] = pens[v & 7];
            }
            o += 2 * n;
            k += n;
            sx = (sx + n) & 255;
            sy = (sy - 2 * n) & 4095;
        }
    }

    // Sprites, 32 x 32 (the first in the list drawn first), on the lines
    // the characters leave open.
    const int first = 240 - 2 * k1, lines = 2 * (k1 - k0);
    for (int i = 0; i < vs.sprite_count; ++i) {
        const VideoState::Sprite& sp = vs.sprite[i];
        int col = (bx - sp.x) & 255;
        if (col >= 32) continue;
        if (sp.flags & 1) col = 31 - col;
        if (!(sprite_cols_used_[sp.code] >> col & 1)) continue;
        const uint8_t* g = roms_.sprites + sp.code * kSpriteBytes + (col >> 3) * 8;
        const int s = 7 - (col & 7);
        const uint16_t* pens = pen_ + sp.pens;
        const bool flip_y = sp.flags & 2;
        for (int py = 0; py < 32; ++py) {
            const int ly = (sp.y + py) & 255;
            if (unsigned(ly - first) >= unsigned(lines)) continue;
            const int gy = flip_y ? 31 - py : py;
            const uint8_t* b = g + (gy & 7) + (gy >> 3) * 32;
            const int v = ((b[0] >> s) & 1) | (((b[kSpritePlane] >> s) & 1) << 1)
                        | (((b[2 * kSpritePlane] >> s) & 1) << 2);
            if (v) out[239 - ly] = pens[v];
        }
    }

    // Characters on top, board rows 2-29: board line ty * 8 + r is display
    // x 239 - ty * 8 - r.
    for (int ty = 2; ty < 30; ++ty) {
        uint32_t bits = char_cols[vs.fg[ty * 32 + tx] * 8];
        if (!bits) continue;
        const uint16_t* pens = pen_ + vs.fg_bank + char_colour_[tx + 32 * (ty >> 2)];
        uint16_t* o = out + 239 - ty * 8;
        if (opaque(bits)) {
            o[0] = pens[bits & 3];          o[-1] = pens[(bits >> 2) & 3];
            o[-2] = pens[(bits >> 4) & 3];  o[-3] = pens[(bits >> 6) & 3];
            o[-4] = pens[(bits >> 8) & 3];  o[-5] = pens[(bits >> 10) & 3];
            o[-6] = pens[(bits >> 12) & 3]; o[-7] = pens[bits >> 14];
            continue;
        }
        for (int r = 0; r < 8; ++r, bits >>= 2)
            if (bits & 3) o[-r] = pens[bits & 3];
    }
}

} // namespace zaxxon
