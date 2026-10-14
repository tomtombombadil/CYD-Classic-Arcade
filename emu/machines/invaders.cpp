// Space Invaders machine - see invaders.h.
//
// Memory map (A15 isn't decoded, so 8000-FFFF mirrors 0000-7FFF):
//   0000-1FFF  program ROM (H, G, F, E - 2 KB each)
//   2000-23FF  work RAM            2400-3FFF  video RAM (1 bit a pixel)
//   4000-5FFF  empty ROM sockets (read 0)
//   6000-7FFF  mirror of 2000-3FFF
// I/O ports (A0-A2 decoded):
//   read  0 / 4  IN0: bit 3 always 1, bits 4-6 fire / left / right (unused
//                by the game; the switches 5-8 read 0)
//   read  1 / 5  IN1: bit 0 coin (low = coin), 1 start 2, 2 start 1, 3 always
//                1, 4-6 fire / left / right
//   read  2 / 6  IN2: bits 0-1 ships, 3 bonus at 1000, 7 hide coin info
//                (DIP switches), 4-6 player 2 fire / left / right
//   read  3 / 7  the shifter's result
//   write 2      shift amount (bits 0-2)    write 4  shift data
//   write 3 / 5  sound bits (invaders_sound.h)
//   write 6      watchdog reset
#include "invaders.h"

namespace invaders {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x2000},
};

// CRCs as in MAME's "invaders" set; the file names are the older ones (the
// newer sets call them 9316b-0869_m739h.h1 etc. - matching is by CRC).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu, 0x0000, 0x0800, 0x734f5ad8, "invaders.h", false},
    {kRegionCpu, 0x0800, 0x0800, 0x6bfaca4a, "invaders.g", false},
    {kRegionCpu, 0x1000, 0x0800, 0x0ccead96, "invaders.f", false},
    {kRegionCpu, 0x1800, 0x0800, 0x14e538b0, "invaders.e", false},
};

// The cabinet's overlay, in display coordinates (224 x 256, y down): a red
// strip where the UFO flies, a green one over the bases and the player's
// cannon, and green over the reserve cannons at the bottom left (the
// credit count at the bottom right stays white). Everything else white.
struct Band {
    uint16_t y0, y1;        // rows [y0, y1)
    uint16_t x0, x1;        // columns [x0, x1)
    uint8_t  pen;           // 1 red, 2 green
};
constexpr Band kOverlay[] = {
    {32, 62, 0, 224, 1},
    {184, 240, 0, 224, 2},
    {240, 256, 16, 134, 2},
};
// Pen colours: the gels let through about 1/8 of the other primaries.
constexpr uint8_t kPenRgb[3][3] = {{255, 255, 255}, {255, 32, 32}, {32, 255, 32}};

// The watchdog resets the board after 255 frames without a write to port 6.
constexpr int kWatchdogFrames = 255;

// Sample player channels, and how loud a recording plays (of 256).
enum Channel { kChUfo, kChShot, kChBaseHit, kChInvaderHit, kChFleet, kChUfoHit, kChExtraShip };
constexpr int kSampleVolume = 160;

uint16_t rgb565(const uint8_t* c, bool swap)
{
    const uint16_t px = uint16_t(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
    return swap ? uint16_t((px >> 8) | (px << 8)) : px;
}

} // namespace

const romset::Set kRomSet = {
    "invaders", nullptr, "Space Invaders", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

const char* const kSampleNames[kSampleCount] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = port_read;
    cpu_.port_out = port_write;
    cpu_.irq_ack = irq_ack;
    for (uint32_t base = 0; base < 0x10000; base += 0x8000) {
        cpu_.map_read(uint16_t(base), 0x2000, roms.cpu);
        cpu_.map_ram(uint16_t(base + 0x2000), 0x2000, ram_);
        cpu_.map_ram(uint16_t(base + 0x6000), 0x2000, ram_);
    }

    synth_.init(audio_rate);
    player_.init(audio_rate);
    for (int c = kChUfo; c <= kChExtraShip; ++c) player_.set_volume(c, kSampleVolume);

    black_ = 0;
    for (int p = 0; p < 3; ++p) pen_[p] = rgb565(kPenRgb[p], swap_rgb565_bytes);
    reset();
}

void Machine::set_samples(const samples::Wav* const* wavs)
{
    uint16_t skip = 0;
    for (int i = 0; i < kSampleCount; ++i) {
        wav_[i] = wavs ? wavs[i] : nullptr;
        if (wav_[i]) skip |= uint16_t(1 << i);
    }
    synth_.set_skip(skip);
    player_.reset();
}

void Machine::reset()
{
    cpu_.reset();
    synth_.reset();
    player_.reset();
    shifter_ = Shifter{};
    port3_ = port5_ = 0;
    watchdog_ = 0;
    carry_ = 0;
}

uint8_t Machine::in0() const
{
    uint8_t v = 0x08;                       // bit 3: unused, pulled high
    if (buttons_ & kFire)  v |= 0x10;
    if (buttons_ & kLeft)  v |= 0x20;
    if (buttons_ & kRight) v |= 0x40;
    return v;
}

uint8_t Machine::in1() const
{
    uint8_t v = 0x09;                       // bit 0: coin switch open (active low), bit 3 high
    if (buttons_ & kCoin)   v &= uint8_t(~0x01);
    if (buttons_ & kStart2) v |= 0x02;
    if (buttons_ & kStart1) v |= 0x04;
    if (buttons_ & kFire)   v |= 0x10;
    if (buttons_ & kLeft)   v |= 0x20;
    if (buttons_ & kRight)  v |= 0x40;
    return v;
}

// On the upright board port 2's controls are wired to the same panel as
// port 1's; the extra player-2 set is added on top.
uint8_t Machine::in2() const
{
    uint8_t v = uint8_t(dips_ & (kDipShipsMask | kDipBonus1000 | kDipNoCoinInfo));
    if (buttons_ & (kFire | kFire2))   v |= 0x10;
    if (buttons_ & (kLeft | kLeft2))   v |= 0x20;
    if (buttons_ & (kRight | kRight2)) v |= 0x40;
    return v;
}

uint8_t Machine::mem_read(void*, uint16_t) { return 0x00; }     // the empty sockets
void Machine::mem_write(void*, uint16_t, uint8_t) {}            // ROM

uint8_t Machine::port_read(void* ctx, uint16_t port)
{
    const Machine& m = *static_cast<const Machine*>(ctx);
    switch (port & 3) {
    case 0:  return m.in0();
    case 1:  return m.in1();
    case 2:  return m.in2();
    default: return m.shifter_.result();
    }
}

void Machine::port_write(void* ctx, uint16_t port, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (port & 7) {
    case 2: m.shifter_.count = v & 7; break;
    case 3: m.sound_write(3, v); break;
    case 4: m.shifter_.data(v); break;
    case 5: m.sound_write(5, v); break;
    case 6: m.watchdog_ = 0; break;
    default: break;
    }
}

// The interrupt flip-flop is cleared by the CPU's acknowledge.
void Machine::irq_ack(void* ctx) { static_cast<Machine*>(ctx)->cpu_.irq(false); }

void Machine::sound_write(int port, uint8_t v)
{
    uint8_t& latch = port == 3 ? port3_ : port5_;
    if (v == latch) return;
    // Everything up to now still sounds as before.
    audio_to(slice_start_ + cpu_.elapsed());
    const uint8_t rise = uint8_t(v & ~latch);
    latch = v;
    if (port == 3) {
        synth_.write_port3(v);
        using namespace invaders_sound;
        if (wav_[kUfo]) {
            if (v & 0x01) { if (!player_.playing(kChUfo)) player_.start(kChUfo, wav_[kUfo], true); }
            else          player_.stop(kChUfo);
        }
        if ((rise & 0x02) && wav_[kShot])       player_.start(kChShot, wav_[kShot]);
        if ((rise & 0x04) && wav_[kBaseHit])    player_.start(kChBaseHit, wav_[kBaseHit]);
        if ((rise & 0x08) && wav_[kInvaderHit]) player_.start(kChInvaderHit, wav_[kInvaderHit]);
        if ((rise & 0x10) && wav_[kExtraShip])  player_.start(kChExtraShip, wav_[kExtraShip]);
    } else {
        synth_.write_port5(v);
        using namespace invaders_sound;
        for (int b = 0; b < 4; ++b)
            if ((rise & (1 << b)) && wav_[kFleet1 + b]) player_.start(kChFleet, wav_[kFleet1 + b]);
        if ((rise & 0x10) && wav_[kUfoHit]) player_.start(kChUfoHit, wav_[kUfoHit]);
    }
}

void Machine::audio_to(int cycle)
{
    if (!audio_) return;
    int target = int(int64_t(cycle) * audio_n_ / kCyclesPerFrame);
    if (target > audio_n_) target = audio_n_;
    if (target <= audio_done_) return;
    int16_t* out = audio_ + audio_done_;
    const int n = target - audio_done_;
    synth_.render(out, n);
    player_.mix(out, n);
    if (!(port3_ & 0x20))                   // amplifier off
        for (int i = 0; i < n; ++i) out[i] = 0;
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

    // Line 96: RST 1, then on to the start of VBLANK.
    cpu_.irq(true, 0xcf);
    slice_start_ = carry_;
    int want = (kVblankLine - kMidLine) * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;

    // Line 224: RST 2; then through VBLANK and the top of the next frame.
    cpu_.irq(true, 0xd7);
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
    slice_start_ = (kVblankLine - kMidLine) * kCyclesPerLine + carry_;
    want = (kLinesPerFrame - kVblankLine + kMidLine) * kCyclesPerLine - carry_;
    carry_ = cpu_.run(want) - want;
    ++frame_;

    audio_to(kCyclesPerFrame);
    audio_ = nullptr;
    return audio ? n : 0;
}

void Machine::render_line(int y, uint16_t* out) const
{
    // Display line y is hardware column 255 - y: one bit of each of the 224
    // rows of 32 bytes (bit 0 = the leftmost hardware pixel of a byte).
    const int hx = 255 - y;
    const uint8_t* src = ram_ + 0x400 + (hx >> 3);
    const int bit = hx & 7;

    // Pens across the line from the overlay strips.
    uint16_t pens[kScreenW];
    for (int x = 0; x < kScreenW; ++x) pens[x] = pen_[0];
    for (const Band& b : kOverlay) {
        if (y < b.y0 || y >= b.y1) continue;
        for (int x = b.x0; x < b.x1; ++x) pens[x] = pen_[b.pen];
    }
    for (int x = 0; x < kScreenW; ++x, src += 32) out[x] = ((*src >> bit) & 1) ? pens[x] : black_;
}

} // namespace invaders
