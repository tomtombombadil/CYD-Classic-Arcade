// Galaxian machine - see galaxian.h.
//
// Memory map (A15 isn't decoded: 8000-FFFF reads FF):
//   0000-27FF  program ROM (U V W Y 7L: 10 KB; 2800-3FFF empty)
//   4000-43FF  RAM (mirrored at 4400)
//   5000-53FF  tile codes (mirrored at 5400)
//   5800-58FF  object RAM (mirrored to 5FFF): 00-3F scroll / colour of each
//              tile column, 40-5F 8 sprites (Y, code + flips, colour, X),
//              60-7F 8 bullets (-, Y, -, X)
//   6000 read  IN0                 6800 read  IN1        7000 read  IN2 (DIPs)
//   7800 read  watchdog reset
//   6000-6007  write (mirrored every 8 to 67FF): 0-1 start lamps, 2 coin
//              lockout, 3 coin counter, 4-7 the background DAC bits
//   6800-6807  write: the sound board's latch: 0-2 FS1-FS3, 3 HIT, 4 not
//              connected, 5 FIRE, 6 VOL1, 7 VOL2
//   7000-7007  write: 1 NMI enable, 4 stars on, 6 / 7 flip X / Y
//   7800       write: the tone's pitch
// Inputs are active high.
#include "galaxian.h"

#include <cstring>

namespace galaxian {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x2800},
    {"gfx1",    0x1000},
    {"proms",   0x0020},
};

// CRCs as in MAME's "galaxian" (Namco set 1; the program chips carry
// Midway's names).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x0800, 0x745e2d61, "galmidw.u", false},
    {kRegionCpu,   0x0800, 0x0800, 0x9c999a40, "galmidw.v", false},
    {kRegionCpu,   0x1000, 0x0800, 0xb5894925, "galmidw.w", false},
    {kRegionCpu,   0x1800, 0x0800, 0x6b3ca10b, "galmidw.y", false},
    {kRegionCpu,   0x2000, 0x0800, 0x1b933207, "7l", false},
    {kRegionGfx,   0x0000, 0x0800, 0x39fb43a4, "1h.bin", false},
    {kRegionGfx,   0x0800, 0x0800, 0x7e3f56a2, "1k.bin", false},
    {kRegionProms, 0x0000, 0x0020, 0xc3ac9467, "6l.bpr", false},
};

} // namespace

const romset::Set kRomSet = {
    "galaxian", nullptr, "Galaxian (Namco set 1)", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
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
    cpu_.map_read(0x0000, 0x2800, roms.cpu);         // 2800-3FFF: no chips (read FF)
    cpu_.map_ram(0x4000, 0x400, ram_);
    cpu_.map_ram(0x4400, 0x400, ram_);
    cpu_.map_read(0x5000, 0x400, vram_);
    cpu_.map_read(0x5400, 0x400, vram_);
    cpu_.map_write(0x5000, 0x400, vram_);
    cpu_.map_write(0x5400, 0x400, vram_);

    video_.init(roms.proms, roms.gfx, {true, true, false}, swap_rgb565_bytes);
    sound_.init(audio_rate);
    reset();
}

void Machine::reset()
{
    cpu_.reset();
    sound_.reset();
    nmi_enable_ = false;
    stars_on_ = false;
    pitch_ = 0xff;
    sound_ctrl_ = 0;
    lfo_ = 0;
    watchdog_ = 0;
    carry_ = 0;
}

uint8_t Machine::in0() const
{
    uint8_t v = 0;                          // bit 5 clear = upright cabinet
    if (buttons_ & kCoin)    v |= 0x01;
    if (buttons_ & kCoin2)   v |= 0x02;
    if (buttons_ & kLeft)    v |= 0x04;
    if (buttons_ & kRight)   v |= 0x08;
    if (buttons_ & kFire)    v |= 0x10;
    if (buttons_ & kTest)    v |= 0x40;
    if (buttons_ & kService) v |= 0x80;
    return v;
}

uint8_t Machine::in1() const
{
    uint8_t v = 0;                          // bits 2-4 player 2 (cocktail), 6-7 coinage 1 / 1
    if (buttons_ & kStart1) v |= 0x01;
    if (buttons_ & kStart2) v |= 0x02;
    return v;
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr & 0xf800) {
    case 0x5800: return m.obj_[addr & 0xff];
    case 0x6000: return m.in0();
    case 0x6800: return m.in1();
    case 0x7000: return m.dsw_;
    case 0x7800: m.watchdog_ = 0; return 0xff;
    default:     return 0xff;
    }
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const int bit = addr & 7;
    const bool on = v & 1;
    switch (addr & 0xf800) {
    case 0x5800:
        m.obj_[addr & 0xff] = v;
        break;
    case 0x6000:
        if (bit >= 4) {
            m.audio_to(m.now());
            const uint8_t mask = uint8_t(1 << (bit - 4));
            m.lfo_ = on ? uint8_t(m.lfo_ | mask) : uint8_t(m.lfo_ & ~mask);
            m.sound_.lfo(bit - 4, on);
        }
        break;
    case 0x6800:
        if (on != bool(m.sound_ctrl_ & (1 << bit))) {
            m.audio_to(m.now());
            m.sound_ctrl_ = on ? uint8_t(m.sound_ctrl_ | (1 << bit)) : uint8_t(m.sound_ctrl_ & ~(1 << bit));
            m.sound_.control(bit, on);
        }
        break;
    case 0x7000:
        if (bit == 1) {
            m.nmi_enable_ = on;
        } else if (bit == 4) {
            // Stars on: the generator restarts from where the beam is.
            if (on && !m.stars_on_) {
                const int c = m.now() < 0 ? 0 : m.now();
                const uint32_t at = uint32_t(c / kCyclesPerLine) * 512 + uint32_t(c % kCyclesPerLine) * 4;
                m.star_origin_ = (galaxian_video::kStarPeriod - at % galaxian_video::kStarPeriod) %
                                 galaxian_video::kStarPeriod;
            }
            m.stars_on_ = on;
        }
        break;                              // 6 / 7: flip screen (cocktail), not drawn
    case 0x7800:
        if (v != m.pitch_) {
            m.audio_to(m.now());
            m.pitch_ = v;
            m.sound_.pitch(v);
        }
        break;
    default:
        break;
    }
}

uint8_t Machine::port_read(void*, uint16_t) { return 0xff; }
void    Machine::port_write(void*, uint16_t, uint8_t) {}

void Machine::vblank()
{
    video_.capture(state_, vram_, obj_, 0, stars_on_, star_origin_);
    // The star field moves one LFSR step (half a pixel) a frame.
    star_origin_ = (star_origin_ + galaxian_video::kStarPeriod - 1) % galaxian_video::kStarPeriod;

    if (nmi_enable_) cpu_.nmi();
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
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

    slice_start_ = carry_;
    int want = kVblankLine * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    slice_start_ = kVblankLine * kCyclesPerLine + carry_;
    vblank();
    want = (kLinesPerFrame - kVblankLine) * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    ++frame_;

    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    return audio ? n : 0;
}

} // namespace galaxian
