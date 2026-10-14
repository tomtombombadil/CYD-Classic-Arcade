// Donkey Kong machine - see dkong.h.
//
// Main CPU memory map:
//   0000-3FFF  program ROM (5E 5C 5B 5A on set 1; 5F 5G 5H 5K on set 2)
//   6000-6BFF  RAM                    7000-73FF  sprite RAM (two banks of 0x200)
//   7400-77FF  video RAM (tile codes, 32 x 32)
//   7800-780F  8257 DMA registers
//   7C00       read IN0 (player 1)    write: the tune latch for the 8035
//   7C80       read IN1 (player 2, cocktail)
//   7D00       read IN2 (starts, coin, the 8035's status; resets the watchdog)
//   7D00-7D07  write: latch 6H, bit 0 of the data to bit n: 0 walk, 1 jump,
//              2 stomp (analog), 3 spring / coin, 4 falling, 5 barrel /
//              prize (read by the 8035 as P2 bit 5, T1, T0)
//   7D80       read DSW0              write: the 8035's INT (data != 0)
//   7D82 flip screen   7D83 sprite bank   7D84 NMI enable   7D85 DMA request
//   7D86-7D87  palette bank (bits 0, 1)
// All inputs active high. Facts from MAME's driver (read for facts only).
//
// 8035: program 0000-0FFF = ROM 3I (2 KB, seen twice); MOVX reads ROM 3J
// (256-byte page = P2 bits 0-2) or, with P2 bit 6 set, the tune latch
// inverted (low 4 bits). P1 = the DAC. P2 bit 4 = status to the main CPU
// (IN2 bit 6, inverted), bit 7 = DAC full level / fade.
#include "dkong.h"

#include <cstring>

namespace dkong {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x4000},
    {"soundcpu", 0x1000},
    {"gfx1",     0x1000},
    {"gfx2",     0x2000},
    {"proms",    0x0300},
};

const char* const kVariants[] = {"dkong", "dkongo"};

// CRCs as in MAME's "dkong" (variant 1) and "dkongo" (variant 2); 5C and
// everything else are the same in both.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,     0x0000, 0x1000, 0xba70b88b, "c_5et_g.bin", false, 1},
    {kRegionCpu,     0x2000, 0x1000, 0x1c97d324, "c_5bt_g.bin", false, 1},
    {kRegionCpu,     0x3000, 0x1000, 0xb9005ac0, "c_5at_g.bin", false, 1},
    {kRegionCpu,     0x0000, 0x1000, 0x424f2b11, "c_5f_b.bin",  false, 2},
    {kRegionCpu,     0x2000, 0x1000, 0x1d28895d, "c_5h_b.bin",  false, 2},
    {kRegionCpu,     0x3000, 0x1000, 0x553b89bb, "tkg3c.5k",    false, 2},
    {kRegionCpu,     0x1000, 0x1000, 0x5ec461ec, "c_5ct_g.bin", false},
    {kRegionSound,   0x0000, 0x0800, 0x45a4ed06, "s_3i_b.bin",  false},
    {kRegionSound,   0x0800, 0x0800, 0x4743fe92, "s_3j_b.bin",  false},
    {kRegionTiles,   0x0000, 0x0800, 0x12c8c95d, "v_5h_b.bin",  false},
    {kRegionTiles,   0x0800, 0x0800, 0x15e9c5e9, "v_3pt.bin",   false},
    {kRegionSprites, 0x0000, 0x0800, 0x59f8054d, "l_4m_b.bin",  false},
    {kRegionSprites, 0x0800, 0x0800, 0x672e4714, "l_4n_b.bin",  false},
    {kRegionSprites, 0x1000, 0x0800, 0xfeaa59ee, "l_4r_b.bin",  false},
    {kRegionSprites, 0x1800, 0x0800, 0x20f2ef7e, "l_4s_b.bin",  false},
    {kRegionProms,   0x0000, 0x0100, 0xe273ede5, "c-2k.bpr",    false},
    {kRegionProms,   0x0100, 0x0100, 0xd6412358, "c-2j.bpr",    false},
    {kRegionProms,   0x0200, 0x0100, 0xb869b8f5, "v-5e.bpr",    false},
};

// Palette PROM bits (stored inverted) -> 8-bit levels through the resistor
// DAC: red and green 1K / 470 / 220 ohm, blue 470 / 220 ohm.
constexpr uint8_t kRGWeights[3] = {33, 71, 151};
constexpr uint8_t kBWeights[2] = {81, 174};

// The watchdog resets the board when IN2 isn't read for this many frames.
constexpr int kWatchdogFrames = 256;

// The 8257 holds the bus 4 clocks per transfer; each byte is two (one
// channel reads it into a latch, the other writes it out).
constexpr int kDmaCyclesPerByte = 8;

} // namespace

const romset::Set kRomSet = {
    "dkong", nullptr, "Donkey Kong", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
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
    cpu_.map_read(0x0000, 0x4000, roms.cpu);
    cpu_.map_ram(0x6000, 0xc00, ram_);
    cpu_.map_ram(0x7000, 0x400, sprite_ram_);
    cpu_.map_ram(0x7400, 0x400, vram_);

    snd_.ctx = this;
    snd_.rom = roms.sound;
    snd_.rom_mask = 0x7ff;
    snd_.ext_read = snd_ext_read;
    snd_.port_in = snd_port_in;
    snd_.port_out = snd_port_out;
    snd_.test_in = snd_test;

    board_.init(audio_rate);

    // 256 pens: colour (0-63) x 4 + pixel value. Pixel value 0 is black on
    // the real board (the PROMs' outputs are switched off).
    const uint8_t* lo = roms.proms;
    const uint8_t* hi = roms.proms + 0x100;
    for (int i = 0; i < 256; ++i) {
        const int r3 = ~(hi[i] >> 1) & 7;
        const int g3 = ~(((hi[i] << 2) & 4) | ((lo[i] >> 2) & 3)) & 7;
        const int b2 = ~lo[i] & 3;
        int r = 0, g = 0, b = 0;
        for (int k = 0; k < 3; ++k) {
            if (r3 & (1 << k)) r += kRGWeights[k];
            if (g3 & (1 << k)) g += kRGWeights[k];
        }
        for (int k = 0; k < 2; ++k)
            if (b2 & (1 << k)) b += kBWeights[k];
        if ((i & 3) == 0) r = g = b = 0;
        uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        if (swap_rgb565_bytes) px = uint16_t((px >> 8) | (px << 8));
        pen_[i] = px;
    }

    decode_gfx();
    fc_ = 0;                                // a watchdog reset keeps the frame's timing
    snd_done_ = 0;
    reset();
}

// The monitor is turned 90 degrees counter-clockwise: display line y is
// hardware column 255 - y, display x is hardware line - 16. Graphics are
// stored by hardware column (= display line), rows (= display x) packed.
void Machine::decode_gfx()
{
    // Tiles: 8 bytes each (one a row, bit 7 = left), plane 1 (the high bit
    // of the pixel value) 0x800 on.
    const uint8_t* t = roms_.tiles;
    for (int code = 0; code < 256; ++code)
        for (int px = 0; px < 8; ++px) {
            uint16_t bits = 0;
            for (int py = 0; py < 8; ++py) {
                const int b = 7 - px;
                const int v = (((t[0x800 + code * 8 + py] >> b) & 1) << 1) | ((t[code * 8 + py] >> b) & 1);
                bits |= uint16_t(v << (2 * py));
            }
            tile_cols_[code * 8 + px] = bits;
        }
    // Sprites: 16 bytes each (one a row) for the left 8 pixels, the right 8
    // 0x800 on; plane 1 0x1000 on.
    const uint8_t* s = roms_.sprites;
    for (int code = 0; code < 128; ++code)
        for (int px = 0; px < 16; ++px) {
            uint32_t bits = 0;
            for (int py = 0; py < 16; ++py) {
                const int off = (px < 8 ? 0 : 0x800) + code * 16 + py;
                const int b = 7 - (px & 7);
                const uint32_t v = uint32_t((((s[0x1000 + off] >> b) & 1) << 1) | ((s[off] >> b) & 1));
                bits |= v << (2 * py);
            }
            sprite_cols_[code * 16 + px] = bits;
        }
}

void Machine::reset()
{
    cpu_.reset();
    snd_.reset();
    snd_.irq(false);
    board_.reset();
    tune_ = 0;
    sound_bits_ = 0;
    nmi_mask_ = flip_ = drq_ = false;
    sprite_bank_ = palette_bank_ = 0;
    for (int c = 0; c < 4; ++c) dma_addr_[c] = dma_count_reg_[c] = 0;
    dma_mode_ = 0;
    dma_flip_ = false;
    stall_ = 0;
    watchdog_ = 0;
    dac_ = 0;
}

// --- main CPU bus --------------------------------------------------------------

uint8_t Machine::in2()
{
    sound_to(now());                        // bit 6 comes from the 8035
    watchdog_ = 0;
    uint8_t v = 0;
    if (buttons_ & kStart1) v |= 0x04;
    if (buttons_ & kStart2) v |= 0x08;
    if (!(snd_.p2 & 0x10))  v |= 0x40;
    if (buttons_ & kCoin)   v |= 0x80;
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0x7800 && addr < 0x7810) {
        const int r = addr & 0x0f;
        if (r == 8) return 0;               // status: no channel finished pending
        if (r > 8) return 0xff;
        const uint16_t v = (r & 1) ? m.dma_count_reg_[r >> 1] : m.dma_addr_[r >> 1];
        const uint8_t b = m.dma_flip_ ? uint8_t(v >> 8) : uint8_t(v);
        m.dma_flip_ = !m.dma_flip_;
        return b;
    }
    switch (addr & 0xff80) {
    case 0x7c00: {
        uint8_t v = 0;
        if (m.buttons_ & kRight) v |= 0x01;
        if (m.buttons_ & kLeft)  v |= 0x02;
        if (m.buttons_ & kUp)    v |= 0x04;
        if (m.buttons_ & kDown)  v |= 0x08;
        if (m.buttons_ & kJump)  v |= 0x10;
        return v;
    }
    case 0x7c80: return 0x00;               // player 2's controls (cocktail only)
    case 0x7d00: return m.in2();
    case 0x7d80: return m.dsw0_;
    default:     return 0x00;               // empty sockets, unmapped
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr >= 0x7800 && addr < 0x7810) {
        const int r = addr & 0x0f;
        if (r == 8) {                       // mode set: channel enables, options
            m.dma_mode_ = v;
            m.dma_flip_ = false;
        } else if (r < 8) {
            uint16_t& reg = (r & 1) ? m.dma_count_reg_[r >> 1] : m.dma_addr_[r >> 1];
            reg = m.dma_flip_ ? uint16_t((reg & 0x00ff) | (v << 8)) : uint16_t((reg & 0xff00) | v);
            m.dma_flip_ = !m.dma_flip_;
        }
        return;
    }
    switch (addr) {
    case 0x7c00:
        m.sound_to(m.now());
        m.tune_ = v;
        break;
    case 0x7d00: case 0x7d01: case 0x7d02: case 0x7d03:
    case 0x7d04: case 0x7d05: case 0x7d06: case 0x7d07: {
        const uint8_t bit = uint8_t(1 << (addr & 7));
        const uint8_t bits = (v & 1) ? uint8_t(m.sound_bits_ | bit) : uint8_t(m.sound_bits_ & ~bit);
        if (bits == m.sound_bits_) break;
        m.sound_to(m.now());
        m.audio_to(m.snd_done_);
        m.sound_bits_ = bits;
        m.board_.write_latch(bits);
        break;
    }
    case 0x7d80:
        m.sound_to(m.now());
        m.snd_.irq(v != 0);
        break;
    case 0x7d82: m.flip_ = v & 1; break;
    case 0x7d83: m.sprite_bank_ = v & 1; break;
    case 0x7d84: m.nmi_mask_ = v & 1; break;
    case 0x7d85: {
        const bool drq = v & 1;
        if (drq && !m.drq_) m.dma();
        m.drq_ = drq;
        break;
    }
    case 0x7d86: case 0x7d87: {
        const uint8_t bit = uint8_t(1 << (addr & 1));
        m.palette_bank_ = (v & 1) ? uint8_t(m.palette_bank_ | bit) : uint8_t(m.palette_bank_ & ~bit);
        break;
    }
    default: break;                         // ROM, 7C80 / 7D81 (Radar Scope's grid), unmapped
    }
}

uint8_t Machine::port_read(void*, uint16_t) { return 0xff; }
void Machine::port_write(void*, uint16_t, uint8_t) {}

// The DMA request: channel 0 reads the sprite list into the board's latch,
// channel 1 writes it to sprite RAM, byte by byte (rotating priority) -
// so a copy of channel 0's count + 1 bytes. It happens at once; the Z80 is
// held off the bus meanwhile.
void Machine::dma()
{
    if ((dma_mode_ & 0x03) != 0x03) return;
    const int n = (dma_count_reg_[0] & 0x3fff) + 1;
    uint16_t src = dma_addr_[0], dst = dma_addr_[1];
    for (int i = 0; i < n; ++i) cpu_.wr(dst++, cpu_.rd(src++));
    dma_addr_[0] = src;
    dma_addr_[1] = dst;
    dma_count_reg_[0] |= 0x3fff;            // counted down to terminal count
    dma_count_reg_[1] = uint16_t((dma_count_reg_[1] & 0xc000) | ((dma_count_reg_[1] - n) & 0x3fff));
    if (dma_mode_ & 0x40) dma_mode_ &= uint8_t(~0x03);     // TC stop
    ++dma_count_;
    stall_ += n * kDmaCyclesPerByte;
    cpu_.stop();
}

// --- sound CPU -------------------------------------------------------------------

uint8_t Machine::snd_ext_read(void* ctx, uint8_t addr)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    if (m.snd_.p2 & 0x40) return uint8_t(~m.tune_ & 0x0f);
    return m.roms_.sound[0x800 + (m.snd_.p2 & 7) * 256 + addr];
}

uint8_t Machine::snd_port_in(void* ctx, uint8_t port)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    switch (port) {
    case i8035::kBus: return snd_ext_read(ctx, 0);
    case i8035::kP2:  return uint8_t((m.snd_.p2 & ~0x20) | ((m.sound_bits_ & 0x08) ? 0 : 0x20));
    default:          return 0xff;
    }
}

void Machine::snd_port_out(void* ctx, uint8_t port, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (port == i8035::kP1) {
        if (v == m.dac_) return;
        m.audio_to(m.snd_done_ + m.snd_.elapsed());
        m.dac_ = v;
        ++m.dac_writes_;
    } else if (port == i8035::kP2) {
        m.audio_to(m.snd_done_ + m.snd_.elapsed());
        m.board_.set_dac_full(v & 0x80);
    }
}

// T0 = latch bit 5, T1 = latch bit 4, both inverted.
uint8_t Machine::snd_test(void* ctx, uint8_t pin)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    return (m.sound_bits_ & (pin ? 0x10 : 0x20)) ? 0 : 1;
}

void Machine::sound_to(int main_cycle)
{
    const int target = main_cycle * kSoundCyclesPerLine / kCyclesPerLine;
    while (snd_done_ < target) snd_done_ += snd_.run(target - snd_done_);
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
            audio_[audio_done_++] = board_.sample(float(dac_acc_) / kSoundCyclesPerFrame);
            dac_acc_ = 0;
        }
    }
}

// --- frame ---------------------------------------------------------------------------

void Machine::run_main_to(int cycle)
{
    while (fc_ < cycle) {
        fc_ += cpu_.run(cycle - fc_) + stall_;
        stall_ = 0;
    }
}

int Machine::run_frame(int16_t* audio, int cap)
{
    // Samples for this frame, keeping the remainder so the long-run rate is exact.
    audio_rem_ += uint64_t(audio_rate_) * kCyclesPerFrame;
    int n = int(audio_rem_ / kCpuClock);
    audio_rem_ %= kCpuClock;
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = audio_ ? n : 0;
    audio_done_ = 0;
    audio_pos_ = 0;
    dac_acc_ = 0;

    // From the first visible line to VBLANK, then the NMI and on to the
    // next frame's first visible line.
    run_main_to((kVblankLine - kFirstLine) * kCyclesPerLine);
    if (nmi_mask_) cpu_.nmi();
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
    run_main_to(kCyclesPerFrame);
    snapshot();

    sound_to(kCyclesPerFrame);
    audio_to(kSoundCyclesPerFrame);
    fc_ -= kCyclesPerFrame;
    snd_done_ -= kSoundCyclesPerFrame;
    ++frame_;
    audio_ = nullptr;
    return audio_n_;
}

// The sprite list as the hardware sees it: sprite i's line r shows when it
// is among the first 16 sprites on that line. A sprite's first line is
// 247 - its y byte (the line buffer is filled a line ahead), its left edge
// its x byte - 8; both wrap at 256.
void Machine::snapshot()
{
    VideoState& vs = video_;
    std::memcpy(vs.vram, vram_, sizeof vs.vram);
    vs.palette_bank = palette_bank_;
    vs.flip = flip_;
    uint8_t per_line[256] = {};
    const uint8_t* s = sprite_ram_ + (sprite_bank_ ? 0x200 : 0);
    int count = 0;
    for (int i = 0; i < 128; ++i, s += 4) {
        const uint8_t top = uint8_t(247 - s[0]);
        uint16_t lines = 0;
        for (int r = 0; r < 16; ++r) {
            const int hy = (top + r) & 0xff;
            if (per_line[hy] >= 16) continue;
            ++per_line[hy];
            if (hy >= kFirstLine && hy < kVblankLine) lines |= uint16_t(1 << r);
        }
        if (!lines) continue;
        VideoState::Sprite& sp = vs.sprites[count++];
        sp.x = uint8_t(s[3] - 8);
        sp.y = top;
        sp.code = s[1] & 0x7f;
        sp.attr = uint8_t((s[2] & 0x8f) | ((s[1] & 0x80) >> 1));
        sp.lines = lines;
    }
    vs.sprite_count = uint8_t(count);
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Flip screen turns the whole picture (tiles and sprites) 180 degrees.
    // The upright cabinet's game sets it: its monitor is mounted that way.
    // Below, display line y is hardware column hx; display x 8k..8k+7 is
    // tile row k + 2. A tile's colour comes from PROM 5E by column and
    // group of four rows.
    const int hx = vs.flip ? y : 255 - y;
    const int col = hx >> 3, px = hx & 7;
    const uint8_t* colours = roms_.proms + 0x200 + col;
    const int bank = vs.palette_bank * 16;
    for (int row = 2; row < 30; ++row) {
        const uint16_t bits = tile_cols_[vs.vram[row * 32 + col] * 8 + px];
        const uint16_t* pens = pen_ + ((colours[(row >> 2) * 32] & 0x0f) + bank) * 4;
        uint16_t* o = out + (row - 2) * 8;
        for (int k = 0; k < 8; ++k) o[k] = pens[(bits >> (2 * k)) & 3];
    }

    // Sprites, in RAM order (later ones on top); pixel value 0 is clear.
    for (int i = 0; i < vs.sprite_count; ++i) {
        const VideoState::Sprite& sp = vs.sprites[i];
        const int dx = (hx - sp.x) & 0xff;
        if (dx >= 16) continue;
        const uint32_t bits = sprite_cols_[sp.code * 16 + ((sp.attr & 0x80) ? 15 - dx : dx)];
        if (!bits) continue;
        const uint16_t* pens = pen_ + ((sp.attr & 0x0f) + bank) * 4;
        const bool flip_y = sp.attr & 0x40;
        for (int r = 0; r < 16; ++r) {
            if (!(sp.lines & (1 << r))) continue;
            const int p = int(bits >> (2 * (flip_y ? 15 - r : r))) & 3;
            if (p) out[((sp.y + r) & 0xff) - kFirstLine] = pens[p];
        }
    }
    if (vs.flip)
        for (int a = 0, b = kScreenW - 1; a < b; ++a, --b) {
            const uint16_t t = out[a];
            out[a] = out[b];
            out[b] = t;
        }
}

} // namespace dkong
