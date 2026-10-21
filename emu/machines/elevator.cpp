// Elevator Action machine - see elevator.h.
//
// Main CPU (Z80, 4 MHz; IM 1, the VBLANK IRQ at line 240 held until taken):
//   0000-7FFF  program ROM (6000-7FFF a bank: D50E bit 7 = the empty bank 1)
//   8000-87FF  RAM
//   8800-8FFF  the MCU: even = data (read: its byte, sets ZACCEPT; write:
//              the byte for it, sets ZREADY and its /INT), odd = status
//              (read: bit 0 = ZREADY clear, bit 1 = ZACCEPT clear)
//   9000-BFFF  character RAM (write only)
//   C000-C3FF  RAM; C400-CFFF the three playfields
//   D000-D05F  column scrolls; D060-D0FF RAM; D100-D1FF sprites
//   D200-D27F  palette (mirrored at D280); D300 plane priority (write)
//   D400-D403  collision registers (read; this game never reads them: 0)
//   D404       graphics ROM byte at the pointer (D509 / D50A), which counts up
//   D408-D40D  IN0, IN1, DSW A, IN2, IN3, IN4 (high nibble = AY 2 port A)
//   D40E/D40F  AY 0: address / data (read: data - its ports are DSW B / C)
//   D500-D505  H / V scroll per playfield; D506-D507 colour banks; D508
//              collision clear; D509/D50A graphics pointer; D50B sound
//              command; D50C sound semaphore 2; D50D watchdog; D50E bank /
//              sound on (bit 1) / coin lockout; D600 video mode
//   (D400-D5FF: the low nibble picks, mirrored through the page)
// Unmapped reads give 0 (as MAME's map, which the game runs on).
//
// Sound CPU (Z80, 3 MHz; IM 1 IRQ at 36.6 Hz held until taken; NMI = (a
// command waiting AND enabled by AY 3 port B bit 0 low) OR semaphore 2):
//   0000-1FFF  ROM; 4000-43FF RAM
//   4800-4FFF  A2 = 0: A1 picks AY 1 / AY 2, A2 = 1: AY 3; A0 = data
//   5000-57FF  A1-A0: 0 read the command (clears its flag) / write: clear
//              its bit 7; 1 read flags (bit 3 command waiting, bit 2
//              semaphore 2, bits 1-0 high) / write: clear semaphore 2
//   AY 1 port A = the DAC, port B = its volume (an R-ladder, inverted);
//   AY 2 port A = IN4's high nibble; AY 3 port B bit 0 = NMI enable (low).
//
// The MCU (68705P3 at 3 MHz / 4) on its ports (taitosj's security board):
//   A  the data bus: reads (B1 low: the Z80's byte) AND (B5 low with the
//      bus granted: the byte read from the Z80's memory) AND A's outputs
//   B  0 68INTRQ (not wired here), 1 rising = took the Z80's byte (clears
//      ZREADY and /INT), 2 rising = latch A for the Z80 (clears ZACCEPT),
//      3 low = BUSRQ, 4 falling = write the Z80's memory at the address
//      counter, 5 falling = read it (rising of 4 / 5 with the other high =
//      address + 1, low byte only), 6 low = address low byte = bus, 7 low
//      = address high byte = bus
//   C  0 ZREADY, 1 ZACCEPT, 2 BUSAK low, 3 (68INTAK) 0
//
// Synchronisation: everything runs on one 12 MHz tick count. The main CPU
// leads; the MCU catches up to it at every access to its latches. While
// the MCU is in an idle loop that only the Z80 or its own timer can end,
// the main CPU runs (in pieces of kChunk cycles) to the timer's interrupt;
// otherwise the two step an instruction at a time. A bus request stops the
// main CPU at once (BUSAK straight away) and it loses the requested time.
// The sound CPU catches up at the commands, the D40D status and AY 0's
// writes, and at the frame's end.
//
// Idle skipping, exact (state and time as if run): the MCU's main loop at
// 0180 (ADC $23 / INCA / STA $23 / LDA $20 / AND #$28 / AND $80 / BEQ,
// 27 cycles a pass - a counter the game uses as random numbers) is run in
// closed form pass by pass up to the timer's next interrupt; branch-to-
// self waits on the Z80's latches and /INT (BIH *, BRCLR n,PORTC,*) skip
// whole passes. The main CPU's frame wait (73F2) and the sound CPU's main
// loop (0164): once a whole pass has run, every register they set is as
// each pass leaves it, so the passes to come are only time and R.
#include "elevator.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace elevator {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu",  0x8000},
    {"audiocpu", 0x2000},
    {"mcu",      0x0800},
    {"gfx",      0x8000},
    {"proms",    0x0100},
};

constexpr const char* kVariants[] = {"elevator", "elevatora"};

// CRCs as in MAME's "elevator" (BA3, 4 boards: 2764s, variant 1) and
// "elevatora" (EA, 5 boards: 2732s, variant 2 - Tom's set, its files named
// ea-ic69.bin ... ea-ic8.bin, ba3.11, eb16.22).
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x2000, 0xda775a24, "ba3__01.2764.ic1", false, 1},
    {kRegionCpu,   0x2000, 0x2000, 0xfbfd8b3a, "ba3__02.2764.ic2", false, 1},
    {kRegionCpu,   0x4000, 0x2000, 0xa2e69833, "ba3__03-1.2764.ic3", false, 1},
    {kRegionCpu,   0x6000, 0x2000, 0x2b78c462, "ba3__04-1.2764.ic6", false, 1},
    {kRegionGfx,   0x0000, 0x2000, 0x6c4ee58f, "ba3__05.2764.ic4", false, 1},
    {kRegionGfx,   0x2000, 0x2000, 0x41ab0afc, "ba3__06.2764.ic5", false, 1},
    {kRegionGfx,   0x4000, 0x2000, 0xefe43731, "ba3__07.2764.ic9", false, 1},
    {kRegionGfx,   0x6000, 0x2000, 0x3ca20696, "ba3__08.2764.ic10", false, 1},
    {kRegionCpu,   0x0000, 0x1000, 0x24e277ef, "ea_12.2732.ic69", false, 2},
    {kRegionCpu,   0x1000, 0x1000, 0x13702e39, "ea_13.2732.ic68", false, 2},
    {kRegionCpu,   0x2000, 0x1000, 0x46f52646, "ea_14.2732.ic67", false, 2},
    {kRegionCpu,   0x3000, 0x1000, 0xe22fe57e, "ea_15.2732.ic66", false, 2},
    {kRegionCpu,   0x4000, 0x1000, 0xc10691d7, "ea_16.2732.ic65", false, 2},
    {kRegionCpu,   0x5000, 0x1000, 0x8913b293, "ea_17.2732.ic64", false, 2},
    {kRegionCpu,   0x6000, 0x1000, 0x1cabda08, "ea_18.2732.ic55", false, 2},
    {kRegionCpu,   0x7000, 0x1000, 0xf4647b4f, "ea_19.2732.ic54", false, 2},
    {kRegionGfx,   0x0000, 0x1000, 0xbbbb3fba, "ea_20.2732.ic1", false, 2},
    {kRegionGfx,   0x1000, 0x1000, 0x639cc2fd, "ea_21.2732.ic2", false, 2},
    {kRegionGfx,   0x2000, 0x1000, 0x61317eea, "ea_22.2732.ic3", false, 2},
    {kRegionGfx,   0x3000, 0x1000, 0x55446482, "ea_23.2732.ic4", false, 2},
    {kRegionGfx,   0x4000, 0x1000, 0x77895c0f, "ea_24.2732.ic5", false, 2},
    {kRegionGfx,   0x5000, 0x1000, 0x9a1b6901, "ea_25.2732.ic6", false, 2},
    {kRegionGfx,   0x6000, 0x1000, 0x839112ec, "ea_26.2732.ic7", false, 2},
    {kRegionGfx,   0x7000, 0x1000, 0xdb7ff692, "ea_27.2732.ic8", false, 2},
    {kRegionSound, 0x0000, 0x1000, 0x6d5f57cb, "ea_9.2732.ic70", false, 0},
    {kRegionSound, 0x1000, 0x1000, 0xf0a769a1, "ea_10.2732.ic71", false, 0},
    {kRegionMcu,   0x0000, 0x0800, 0x9ce75afc, "ba3__11.mc68705p3.ic4", false, 0},
    {kRegionProm,  0x0000, 0x0100, 0xb833b5ea, "eb16.ic22", false, 0},
};

// The MCU's idle loop (see above).
constexpr uint8_t kLoop180[] = {0xb9, 0x23, 0x4c, 0xb7, 0x23, 0xb6, 0x20, 0xa4, 0x28, 0xb4, 0x80, 0x27, 0xf3};
constexpr int kLoop180Cycles = 27;

// The main CPU's frame wait (73F2: LD A,($80AB) / OR A / JR NZ: 29 T-states
// and 3 opcode fetches a pass) and the sound CPU's main loop (0164: EI / LD
// IX,$4000 / BIT 0,(IX+0) / JP Z,$01D8 ... 01D8: JP $0164: 58 T-states, 7
// fetches).
constexpr uint8_t kMainWait[] = {0x3a, 0xab, 0x80, 0xb7, 0x20, 0xfa};
constexpr int kMainWaitAt = 0x73f2, kMainWaitT = 29, kMainWaitR = 3;
constexpr uint8_t kSoundLoop[] = {0xfb, 0xdd, 0x21, 0x00, 0x40, 0xdd, 0xcb, 0x00, 0x46, 0xca, 0xd8, 0x01};
constexpr uint8_t kSoundLoopBack[] = {0xc3, 0x64, 0x01};
constexpr int kSoundLoopAt = 0x0164, kSoundLoopT = 58, kSoundLoopR = 7;

// Long CPU runs are cut into pieces of this many cycles (where a wait loop
// can be seen starting).
constexpr int kChunk = 512;

// The scroll's pipeline offsets per playfield (MAME's measured values).
constexpr int kFudge1[3] = {3, 1, -1};
constexpr int kFudge2[3] = {8, 10, 12};

// Sprites show in x 3-254, lines 16-239.
constexpr int kSpriteMinX = 3, kSpriteMaxX = 254;

// The DAC's volume ladder: 680k 330k 150k 82k 39k 20k 10k 4.7k from bits 0-7.
constexpr float kLadder[8] = {1 / 680.0f, 1 / 330.0f, 1 / 150.0f, 1 / 82.0f, 1 / 39.0f, 1 / 20.0f, 1 / 10.0f, 1 / 4.7f};

// The mix (MAME's routes): AY 0's three channels x 0.15 each; AYs 1-3 have
// their outputs tied (taken here as the average of the three) x 0.5, 0.5,
// 1.0; the DAC x 0.15. AC-coupled (20 Hz, assumed), scaled for a small
// speaker with a soft knee above 16,000.
constexpr float kDcHz = 20.0f;
constexpr float kOutGain = 28000.0f;

// Spreads a byte's bits (bit 0 = the leftmost pixel) into the low bit of
// eight bytes (byte 0 = the leftmost pixel, little-endian).
struct Spread {
    uint64_t v[256];
    constexpr Spread() : v()
    {
        for (int b = 0; b < 256; ++b) {
            uint64_t w = 0;
            for (int i = 0; i < 8; ++i)
                if (b & (1 << i)) w |= uint64_t(1) << (8 * i);
            v[b] = w;
        }
    }
};
constexpr Spread kSpread{};

// 1 in each byte that isn't zero (bytes below 0x80).
inline uint64_t nonzero_bytes(uint64_t v)
{
    return ((v + 0x7f7f7f7f7f7f7f7full) >> 7) & 0x0101010101010101ull;
}


} // namespace

const romset::Set kRomSet = {
    "elevator", nullptr, "Elevator Action", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
    kVariants, 2,
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    roms_ = roms;
    audio_rate_ = audio_rate;
    swap_ = swap_rgb565_bytes;

    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.port_in = no_in;
    cpu_.port_out = no_out;
    cpu_.map_read(0x0000, 0x8000, roms.cpu);
    cpu_.map_ram(0x8000, 0x800, ram_);
    cpu_.map_write(0x9000, 0x3000, video_.chars);
    cpu_.map_ram(0xc000, 0x400, ram_c000_);
    for (int l = 0; l < 3; ++l) cpu_.map_ram(uint16_t(0xc400 + 0x400 * l), 0x400, video_.vram[l]);

    snd_.ctx = this;
    snd_.mem_read = snd_read;
    snd_.mem_write = snd_write;
    snd_.port_in = no_in;
    snd_.port_out = no_out;
    snd_.map_read(0x0000, 0x2000, roms.sound);
    snd_.map_ram(0x4000, 0x400, snd_ram_);

    mcu_.ctx = this;
    mcu_.port_in = mcu_port_in;
    mcu_.port_out = mcu_port_out;
    mcu_.init(roms.mcu);
    loop180_ = std::memcmp(roms.mcu + 0x180, kLoop180, sizeof kLoop180) == 0;
    main_wait_ = std::memcmp(roms.cpu + kMainWaitAt, kMainWait, sizeof kMainWait) == 0;
    sound_loop_ = std::memcmp(roms.sound + kSoundLoopAt, kSoundLoop, sizeof kSoundLoop) == 0 &&
                  std::memcmp(roms.sound + 0x1d8, kSoundLoopBack, sizeof kSoundLoopBack) == 0;

    dc_r_ = std::exp(-2.0f * 3.14159265f * kDcHz / float(audio_rate));
    for (int i = 0; i < 64; ++i) palette_write(2 * i, 0), palette_write(2 * i + 1, 0);
    reset();
}

void Machine::reset()
{
    cpu_.reset();
    cpu_.irq(false);
    snd_.reset();
    snd_.irq(false);
    for (ay8910::Chip& a : ay_) a.reset();
    // The MCU interface's power-on state (taitosjsec).
    mcu_addr_ = 0xffff;
    mcu_data_ = host_data_ = read_data_ = 0xff;
    zaccept_ = true;
    zready_ = false;
    pa_ = pb_ = 0xff;
    busrq_ = false;
    stall_ = 0;
    mcu_.set_int(false);
    mcu_.reset();
    bank_ = 0;
    cpu_.map_read(0x6000, 0x2000, roms_.cpu + 0x6000);
    gfx_ptr_ = 0;
    latch_ = 0xff;
    latch_full_ = false;
    sem2_ = false;
    nmi_en_ = false;
    nmi_line_ = false;
    in4_ = 0;
    dac_ = 0x80;
    dac_vol_ = 0;
    irq_held_ = false;
    snd_irq_held_ = false;
    watchdog_ = 0;
}

// --- main CPU ------------------------------------------------------------------

uint8_t Machine::in(int reg)
{
    uint8_t v = 0xff;
    switch (reg) {
    case 0x8:                                           // IN0: player 1 (active low)
        if (buttons_ & kLeft)  v &= uint8_t(~0x01);
        if (buttons_ & kRight) v &= uint8_t(~0x02);
        if (buttons_ & kDown)  v &= uint8_t(~0x04);
        if (buttons_ & kUp)    v &= uint8_t(~0x08);
        if (buttons_ & kFire)  v &= uint8_t(~0x10);
        if (buttons_ & kJump)  v &= uint8_t(~0x20);
        return v;
    case 0x9: return 0xff;                              // IN1: player 2 (cocktail)
    case 0xa: return dips_;
    case 0xb:                                           // IN2: coins, starts (active low)
        if (buttons_ & kCoin2)  v &= uint8_t(~0x10);
        if (buttons_ & kCoin)   v &= uint8_t(~0x20);
        if (buttons_ & kStart1) v &= uint8_t(~0x40);
        if (buttons_ & kStart2) v &= uint8_t(~0x80);
        return v;
    case 0xc:                                           // IN3: service (high), tilt (low)
        v &= uint8_t(~0x10);
        if (buttons_ & kService) v |= 0x10;
        if (buttons_ & kTilt)    v &= uint8_t(~0x20);
        return v;
    case 0xd:                                           // IN4: the sound CPU's status
        sound_to(now());
        return uint8_t(0x0f | (in4_ & 0xf0));
    default:
        return 0;
    }
}

uint8_t Machine::mem_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr >> 12) {
    case 0x8:
        if (addr < 0x8800) return 0;
        m.mcu_to(m.now());
        if (addr & 1) return uint8_t((m.zaccept_ ? 0 : 2) | (m.zready_ ? 0 : 1));
        m.zaccept_ = true;
        m.cpu_.stop();                                  // the MCU may wait for this
        return m.mcu_data_;
    case 0xd:
        switch (addr & 0x0f00) {
        case 0x000: case 0x100:
            if (addr < 0xd060) return m.video_.colscroll[addr & 0x7f];
            if (addr < 0xd100) return m.ram_d060_[addr - 0xd060];
            return m.video_.sprites[addr & 0xff];
        case 0x200:
            return m.video_.palette[addr & 0x7f];
        case 0x400: {
            const int r = addr & 0x0f;
            if (r < 4) return 0;                        // collisions: never read by this game
            if (r < 8) {
                const uint8_t v = m.gfx_ptr_ < 0x8000 ? m.roms_.gfx[m.gfx_ptr_] : 0;
                ++m.gfx_ptr_;
                return v;
            }
            if (r == 0xf) {
                m.ay_[0].set_port_in(0, kDsw2Default);
                m.ay_[0].set_port_in(1, kDsw3Default);
                return m.ay_[0].read_data();
            }
            return m.in(r);
        }
        default:
            return 0;
        }
    default:
        return 0;                                       // char RAM (write only), empty ROM space
    }
}

void Machine::palette_write(int off, uint8_t v)
{
    video_.palette[off] = v;
    const int i = off >> 1;
    const uint8_t lo = uint8_t(~video_.palette[2 * i]), hi = uint8_t(~video_.palette[2 * i + 1]);
    // 1k / 470 / 270 ohm weights (37.3, 79.4, 138.3 of 255), inverted bits.
    auto level = [](int b0, int b1, int b2) { return (b0 * 3733 + b1 * 7943 + b2 * 13824 + 50) / 100; };
    const int r = level((hi >> 6) & 1, (hi >> 7) & 1, lo & 1);
    const int g = level((hi >> 3) & 1, (hi >> 4) & 1, (hi >> 5) & 1);
    const int b = level(hi & 1, (hi >> 1) & 1, (hi >> 2) & 1);
    uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    if (swap_) px = uint16_t((px >> 8) | (px << 8));
    video_.pens[i] = px;
}

void Machine::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    switch (addr >> 12) {
    case 0x8:
        if (addr < 0x8800 || (addr & 1)) return;        // odd: ZINTRQ (the latch drives /INT here)
        m.mcu_to(m.now());
        m.host_data_ = v;
        ++m.host_writes_;
        m.zready_ = true;
        m.mcu_.set_int(true);
        m.cpu_.stop();
        return;
    case 0xd:
        switch (addr & 0x0f00) {
        case 0x000: case 0x100:
            if (addr < 0xd060) m.video_.colscroll[addr & 0x7f] = v;
            else if (addr < 0xd100) m.ram_d060_[addr - 0xd060] = v;
            else m.video_.sprites[addr & 0xff] = v;
            return;
        case 0x200:
            m.palette_write(addr & 0x7f, v);
            return;
        case 0x300:
            m.video_.priority = v;
            return;
        case 0x400:
            if ((addr & 0x0e) == 0x0e) {
                m.sound_to(m.now());
                m.ay_write(0, addr & 1, v);
            }
            return;
        case 0x500: {
            const int r = addr & 0x0f;
            if (r < 6) m.video_.scroll[r] = v;
            else if (r < 8) m.video_.colorbank[r - 6] = v;
            else if (r == 0x9) m.gfx_ptr_ = uint16_t((m.gfx_ptr_ & 0xff00) | v);
            else if (r == 0xa) m.gfx_ptr_ = uint16_t((m.gfx_ptr_ & 0x00ff) | v << 8);
            else if (r == 0xb) {
                m.sound_to(m.now());
                m.latch_ = v;
                m.latch_full_ = true;
                m.sound_nmi_update();
            } else if (r == 0xc) {
                m.sound_to(m.now());
                m.sem2_ = v & 1;
                m.sound_nmi_update();
            } else if (r == 0xd) {
                m.watchdog_ = 0;
            } else if (r == 0xe) {
                if ((m.bank_ ^ v) & 0x02) {
                    m.sound_to(m.now());
                    m.audio_to(m.now());
                }
                m.bank_ = v;
                m.cpu_.map_read(0x6000, 0x2000, (v & 0x80) ? nullptr : m.roms_.cpu + 0x6000);
            }
            return;                                     // D508 collision clear, D50F: nothing
        }
        case 0x600:
            m.video_.mode = v;
            return;
        default:
            return;
        }
    default:
        return;                                         // ROM
    }
}

// --- the MCU and its interface ------------------------------------------------------

uint8_t Machine::bus_value() const
{
    uint8_t v = pa_;
    if (!(pb_ & 0x02)) v &= host_data_;
    if (busrq_ && !(pb_ & 0x20)) v &= read_data_;
    return v;
}

uint8_t Machine::mcu_port_in(void* ctx, int port)
{
    const Machine& m = *static_cast<Machine*>(ctx);
    switch (port) {
    case 0:  return m.bus_value();
    case 2:  return uint8_t((m.zready_ ? 1 : 0) | (m.zaccept_ ? 2 : 0) | (m.busrq_ ? 0 : 4));
    default: return 0xff;
    }
}

void Machine::mcu_port_out(void* ctx, int port, uint8_t latch, uint8_t ddr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    const uint8_t v = uint8_t(latch | ~ddr);            // undriven pins pulled up
    if (port == 0) {
        m.pa_ = v;
        if (!(m.pb_ & 0x40)) m.mcu_addr_ = uint16_t((m.mcu_addr_ & 0xff00) | m.bus_value());
    } else if (port == 1) {
        m.mcu_port_b(v);
    }
}

void Machine::mcu_port_b(uint8_t v)
{
    const uint8_t diff = uint8_t(pb_ ^ v);
    if (!diff) return;
    const uint8_t bus = bus_value();
    const int t = mcu_t_ + mcu_.elapsed() * kMcuTicks;
    if (diff & v & 0x02) {                              // 68LRD: took the Z80's byte
        zready_ = false;
        mcu_.set_int(false);
    }
    if (diff & v & 0x04) {                              // 68LWR: a byte for the Z80
        mcu_data_ = bus;
        zaccept_ = false;
    }
    if (diff & 0x08) {                                  // BUSRQ (low)
        if (!(v & 0x08)) {
            busrq_ = true;
            busrq_at_ = t;
            ++bus_grants_;
            cpu_.stop();
        } else {
            busrq_ = false;
            stall_ += t - busrq_at_;
        }
    }
    bool inc = false;
    if (diff & 0x10) {                                  // 68WRITE
        if (!(v & 0x10)) cpu_.wr(mcu_addr_, bus);
        else if (v & 0x20) inc = true;
    }
    if (diff & 0x20) {                                  // 68READ
        if (!(v & 0x20)) read_data_ = cpu_.rd(mcu_addr_);
        else if (v & 0x10) inc = true;
    }
    if (!(v & 0x40)) mcu_addr_ = uint16_t((mcu_addr_ & 0xff00) | bus);
    else if (inc) mcu_addr_ = uint16_t((mcu_addr_ & 0xff00) | ((mcu_addr_ + 1) & 0xff));
    if (!(v & 0x80)) mcu_addr_ = uint16_t((mcu_addr_ & 0x00ff) | bus << 8);
    pb_ = v;
}

// True when the MCU sits in a loop that only the Z80 (through its latches)
// or its own timer can end; *wake = MCU cycles until the timer's interrupt
// would end it (INT_MAX: only the Z80 can).
bool Machine::mcu_idle(int* wake) const
{
    if (mcu_.irq_due()) return false;
    const uint16_t pc = mcu_.pc;
    const m68705::Cpu& c = mcu_;
    bool idle = false;
    if (loop180_ && (pc == 0x180 || pc == 0x182 || pc == 0x183 || pc == 0x185)) {
        // (its next LDA $20 decides; only the MCU's interrupts change $20)
        idle = (c.ram[0x20 - 0x10] & 0x28 & roms_.mcu[0x80]) == 0;
    } else {
        const uint8_t op = roms_.mcu[pc & 0x7ff];
        if ((op == 0x2e || op == 0x2f) && roms_.mcu[(pc + 1) & 0x7ff] == 0xfe) {
            idle = (op == 0x2e) == mcu_.int_pin();      // BIL / BIH to itself, still looping
        } else if (op < 0x10 && roms_.mcu[(pc + 1) & 0x7ff] == 0x02 && roms_.mcu[(pc + 2) & 0x7ff] == 0xfd) {
            const bool bit = (mcu_port_in(const_cast<Machine*>(this), 2) | 0xf0) >> ((op >> 1) & 7) & 1;
            idle = bit == !(op & 1);                    // BRSET / BRCLR n,PORTC to itself, still looping
            if (c.ddr[2]) idle = false;                 // (port C as an input only)
        }
    }
    if (!idle) return false;
    *wake = INT_MAX;
    if (!(mcu_.cc & m68705::CC_I)) {
        const int u = mcu_.cycles_to_timer_irq();
        if (u >= 0) *wake = u;
    }
    return true;
}

// Skips whole passes of the idle loop the MCU is in (mcu_idle() true), at
// most `budget` cycles' worth and none that would reach the timer's
// interrupt. Returns the cycles skipped.
int Machine::mcu_skip(int budget)
{
    int wake;
    if (!mcu_idle(&wake)) return 0;
    const uint16_t pc = mcu_.pc;
    if (loop180_ && pc >= 0x180 && pc < 0x18d && pc != 0x180) return -1;  // run to the pass's start first
    int len;
    if (pc == 0x180 && loop180_) len = kLoop180Cycles;
    else len = roms_.mcu[pc] < 0x10 ? 10 : 4;
    int k = budget / len;
    if (wake != INT_MAX) {
        const int kw = (wake - 1) / len;
        if (kw < k) k = kw;
    }
    if (k <= 0) return 0;
    if (len == kLoop180Cycles) {
        uint8_t& m23 = mcu_.ram[0x23 - 0x10];
        unsigned c = mcu_.cc & m68705::CC_C, h = 0, a = mcu_.a;
        for (int i = 0; i < k; ++i) {
            const unsigned r = a + m23 + c;
            c = r >> 8;
            h = (a ^ m23 ^ r) & 0x10;
            m23 = uint8_t(r + 1);
            a = 0;                                      // LDA $20 / AND #$28 / AND $80: still 0
        }
        mcu_.a = 0;
        mcu_.cc = uint8_t((mcu_.cc & ~(m68705::CC_C | m68705::CC_H | m68705::CC_N)) | m68705::CC_Z |
                          (c ? m68705::CC_C : 0) | (h ? m68705::CC_H : 0));
    } else if (len == 10) {
        const uint8_t op = roms_.mcu[pc];
        const bool bit = (mcu_port_in(this, 2) | 0xf0) >> ((op >> 1) & 7) & 1;
        mcu_.cc = uint8_t((mcu_.cc & ~m68705::CC_C) | (bit ? m68705::CC_C : 0));
    }
    mcu_.timer(k * len);
    return k * len;
}

void Machine::mcu_to(int tick)
{
    while (mcu_t_ < tick) {
        const int budget = (tick - mcu_t_ + kMcuTicks - 1) / kMcuTicks;
        int chunk = budget < 64 ? budget : 64;
        if (idle_skip_) {
            const int s = mcu_skip(budget);
            if (s > 0) {
                mcu_t_ += s * kMcuTicks;
                mcu_skipped_ += uint64_t(s);
                continue;
            }
            if (s < 0) chunk = 1;                       // an instruction at a time to the loop's start
        }
        const int n = mcu_.run(chunk);
        mcu_t_ += n * kMcuTicks;
    }
}

// --- sound board ------------------------------------------------------------------

uint8_t Machine::snd_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0x4800) {
        const int chip = (addr & 4) ? 3 : (addr & 2) ? 2 : 1;
        return (addr & 1) ? m.ay_[chip].read_data() : 0;
    }
    if ((addr & 0xf800) == 0x5000) {
        if ((addr & 3) == 0) {
            m.latch_full_ = false;
            m.sound_nmi_update();
            return m.latch_;
        }
        if ((addr & 3) == 1) return uint8_t((m.latch_full_ ? 8 : 0) | (m.sem2_ ? 4 : 0) | 3);
    }
    return 0;
}

void Machine::snd_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if ((addr & 0xf800) == 0x4800) {
        const int chip = (addr & 4) ? 3 : (addr & 2) ? 2 : 1;
        m.ay_write(chip, addr & 1, v);
    } else if ((addr & 0xf800) == 0x5000) {
        if ((addr & 3) == 0) m.latch_ &= 0x7f;
        else if ((addr & 3) == 1) {
            m.sem2_ = false;
            m.sound_nmi_update();
        }
    }
}

void Machine::sound_nmi_update()
{
    const bool line = (latch_full_ && nmi_en_) || sem2_;
    if (line && !nmi_line_) {
        snd_.nmi();
        snd_nmi_pending_ = true;                        // taken at the start of the next run
    }
    nmi_line_ = line;
}

void Machine::ay_write(int chip, bool data, uint8_t v)
{
    ay8910::Chip& a = ay_[chip];
    if (!data) {
        a.write_address(v);
        return;
    }
    audio_to(chip == 0 ? now() : snd_now());
    a.write_data(v);
    switch (chip) {
    case 1: {
        dac_ = a.port_out(0);
        const uint8_t vol = uint8_t(~a.port_out(1));    // through a 7416 inverter
        float g = 0;
        for (int b = 0; b < 8; ++b)
            if (vol & (1 << b)) g += kLadder[b];
        float total = 0;
        for (float l : kLadder) total += l;
        dac_vol_ = g / total;
        break;
    }
    case 2:
        in4_ = a.port_out(0);
        break;
    case 3:
        nmi_en_ = !(a.port_out(1) & 1);
        sound_nmi_update();
        break;
    default:
        break;
    }
}

void Machine::sound_to(int tick)
{
    while (snd_t_ < tick) {
        if (snd_t_ >= snd_irq_at_) {
            snd_irq_held_ = true;
            snd_.irq(true, 0xff);
            snd_irq_at_ += kSoundIrqTicks;
        }
        if (snd_irq_held_) {
            // Held until taken (IM 1): an instruction at a time.
            const bool ready = snd_.iff1 != 0;
            snd_nmi_pending_ = false;
            snd_primed_ = false;
            snd_t_ += snd_.run(1) * kSoundTicks;
            if (ready && !snd_.iff1 && snd_.pc.w == 0x0038) {
                snd_irq_held_ = false;
                snd_.irq(false);
            }
            continue;
        }
        const int end = tick < snd_irq_at_ ? tick : snd_irq_at_;
        int cycles = (end - snd_t_ + kSoundTicks - 1) / kSoundTicks;
        const bool looping = idle_skip_ && sound_loop_ && !snd_nmi_pending_ && !(snd_ram_[0] & 1);
        const uint16_t spc = snd_.pc.w;
        bool prime = false;
        if (looping && spc == kSoundLoopAt && cycles >= kSoundLoopT) {
            if (snd_primed_) {
                // At a pass's start after a whole pass: every register the
                // loop sets (F, IX, IFF) is as each pass leaves it, so the
                // passes to come are only time and R.
                const int k = cycles / kSoundLoopT;
                snd_.r = uint8_t(snd_.r + k * kSoundLoopR);
                snd_t_ += k * kSoundLoopT * kSoundTicks;
                cycles -= k * kSoundLoopT;
                snd_skipped_ += uint64_t(k) * kSoundLoopT;
                if (!cycles) continue;
            } else {
                cycles = kSoundLoopT;                   // one whole pass, run
                prime = true;
            }
        } else if (looping && (spc == 0x165 || spc == 0x169 || spc == 0x16d || spc == 0x1d8)) {
            cycles = 1;                                 // to the pass's start
        } else if (cycles > kChunk) {
            cycles = kChunk;                            // pieces: the loop is seen when it starts
        }
        snd_nmi_pending_ = false;
        snd_t_ += snd_.run(cycles) * kSoundTicks;
        snd_primed_ = prime && snd_.pc.w == kSoundLoopAt;
    }
}

// --- audio -----------------------------------------------------------------------

int16_t Machine::mix_sample(uint32_t clocks)
{
    const float scale = clocks ? 1.0f / (float(clocks) * float(ay8910::kMaxLevel)) : 0;
    float s = 0.15f * float(acc_[0] + acc_[1] + acc_[2]) * scale;
    s += (0.5f / 3) * float(acc_[3] + acc_[4] + acc_[5]) * scale;
    s += (0.5f / 3) * float(acc_[6] + acc_[7] + acc_[8]) * scale;
    s += (1.0f / 3) * float(acc_[9] + acc_[10] + acc_[11]) * scale;
    s += clocks ? 0.15f * dac_acc_ / float(clocks) : 0;
    if (!(bank_ & 0x02)) s = 0;                         // sound off
    const float out = dc_r_ * (dc_y_ + s - dc_x_);
    dc_x_ = s;
    dc_y_ = out;
    float v = out * kOutGain;
    const float a = std::fabs(v);
    if (a > 16000.0f) {
        const float over = a - 16000.0f;
        v = (v < 0 ? -1.0f : 1.0f) * (16000.0f + over / (1.0f + over / 16000.0f));
    }
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return int16_t(v);
}

void Machine::audio_to(int tick)
{
    int clk = tick / kAyTicks;
    constexpr int kFrameClocks = kTicksPerFrame / kAyTicks;
    if (clk > kFrameClocks) clk = kFrameClocks;
    while (audio_clock_ < clk) {
        const int end = audio_done_ < audio_n_ ? int(int64_t(audio_done_ + 1) * kFrameClocks / audio_n_)
                                               : kFrameClocks;
        const int to = clk < end ? clk : end;
        const uint32_t n = uint32_t(to - audio_clock_);
        for (int c = 0; c < 4; ++c) ay_[c].run(n, acc_ + 3 * c);
        dac_acc_ += (float(dac_) * (2.0f / 255.0f) - 1.0f) * dac_vol_ * float(n);
        audio_clock_ = to;
        if (to == end && audio_done_ < audio_n_) {
            const int16_t s = mix_sample(uint32_t(end - sample_start_));
            if (audio_) audio_[audio_done_] = s;
            ++audio_done_;
            sample_start_ = end;
            for (uint32_t& a : acc_) a = 0;
            dac_acc_ = 0;
        }
    }
}

// --- frame ---------------------------------------------------------------------------

int Machine::run_frame(int16_t* audio, int cap)
{
    audio_rem_ += uint64_t(audio_rate_) * kTicksPerFrame;
    int n = int(audio_rem_ / 12000000u);
    audio_rem_ %= 12000000u;
    if (n > cap) n = cap;
    audio_ = audio && n > 0 ? audio : nullptr;
    audio_n_ = n > 0 ? n : 0;
    audio_done_ = 0;
    audio_clock_ = 0;
    sample_start_ = 0;
    for (uint32_t& a : acc_) a = 0;
    dac_acc_ = 0;

    // VBLANK (line 240) starts the frame. The watchdog (a counter of
    // VBLANKs that D50D clears) resets the whole board.
    if (++watchdog_ >= kWatchdogFrames) {
        ++watchdog_resets_;
        reset();
    }
    irq_held_ = true;
    cpu_.irq(true, 0xff);

    while (main_t_ < kTicksPerFrame) {
        if (stall_ > 0) {                               // time lost to the MCU's bus requests
            main_t_ += stall_;
            stall_ = 0;
            mcu_to(main_t_);
            continue;
        }
        if (busrq_) {                                   // the MCU has the bus: the Z80 waits
            mcu_to(mcu_t_ + 1);
            continue;
        }
        int horizon = kTicksPerFrame;
        int wake;
        int cycles;
        bool lock = true;
        if (irq_held_) {
            cycles = 1;                                 // held until taken
        } else if (!mcu_idle(&wake)) {
            cycles = 1;                                 // lockstep with the MCU
        } else {
            // Until the MCU's timer can end its idle loop (only the Z80's
            // accesses can otherwise, and they bring it up to date).
            if (wake != INT_MAX && mcu_t_ + int64_t(wake) * kMcuTicks < horizon)
                horizon = mcu_t_ + wake * kMcuTicks;
            cycles = (horizon - main_t_ + kMainTicks - 1) / kMainTicks;
            if (cycles < 1) cycles = 1;
            lock = false;
        }
        const bool waiting = idle_skip_ && main_wait_ && !irq_held_ && ram_[0xab] != 0;
        const uint16_t pc = cpu_.pc.w;
        bool prime = false;
        if (waiting && pc == kMainWaitAt && cycles >= kMainWaitT) {
            if (main_primed_) {
                // The frame wait after a whole pass: A and F are as every
                // pass leaves them, so the passes to come are time and R.
                const int k = cycles / kMainWaitT;
                cpu_.r = uint8_t(cpu_.r + k * kMainWaitR);
                main_t_ += k * kMainWaitT * kMainTicks;
                cycles -= k * kMainWaitT;
                main_skipped_ += uint64_t(k) * kMainWaitT;
            } else {
                cycles = kMainWaitT;                    // one whole pass, run
                prime = true;
            }
        } else if (waiting && (pc == kMainWaitAt + 3 || pc == kMainWaitAt + 4)) {
            cycles = 1;                                 // to the pass's start
        } else if (cycles > kChunk) {
            cycles = kChunk;                            // pieces: the wait is seen when it starts
        }
        if (cycles > 0) {
            slice_start_ = main_t_;
            const bool ready = irq_held_ && cpu_.iff1;
            main_t_ += cpu_.run(cycles) * kMainTicks;
            if (ready && !cpu_.iff1 && cpu_.pc.w == 0x0038) {
                irq_held_ = false;
                cpu_.irq(false);
            }
            main_primed_ = prime && cpu_.pc.w == kMainWaitAt;
        }
        if (lock || main_t_ >= horizon) mcu_to(main_t_);
    }

    mcu_to(kTicksPerFrame);
    sound_to(kTicksPerFrame);
    audio_to(kTicksPerFrame);
    ++frame_;

    main_t_ -= kTicksPerFrame;
    mcu_t_ -= kTicksPerFrame;
    snd_t_ -= kTicksPerFrame;
    snd_irq_at_ -= kTicksPerFrame;
    busrq_at_ -= kTicksPerFrame;
    const int done = audio_ ? audio_n_ : 0;
    audio_ = nullptr;
    return done;
}

// --- video ---------------------------------------------------------------------------

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    const int line = y + kFirstLine;
    // Each plane's pens for this line (0 = blank), 8 bytes at a time.
    alignas(8) uint8_t plane[4][264];                   // 0 = sprites, 1-3 = playfields 1-3
    const int col[3] = {(vs.colorbank[0] & 7) * 8, ((vs.colorbank[0] >> 4) & 7) * 8, (vs.colorbank[1] & 7) * 8};
    bool on[4];
    for (int l = 0; l < 3; ++l) {
        on[l + 1] = vs.mode & (0x10 << l);
        if (!on[l + 1]) continue;
        const int s = vs.scroll[2 * l];
        const int xs = -(s & 0xf8) + ((s + kFudge1[l]) & 7) + kFudge2[l];
        const uint8_t bank = l == 0 ? vs.colorbank[0] & 0x08 : l == 1 ? vs.colorbank[0] & 0x80 : vs.colorbank[1] & 0x08;
        const uint8_t* chars = vs.chars + (bank ? 0x1800 : 0);
        const uint8_t* vram = vs.vram[l];
        const uint8_t* cs = vs.colscroll + 32 * l;
        const int vscroll = vs.scroll[2 * l + 1];
        const uint64_t pen = uint64_t(col[l]);
        uint8_t* buf = plane[l + 1];
        const int x0 = xs & 0xff;
        for (int c = 0; c < 32; ++c) {
            const int sy = (line + cs[c] + vscroll) & 0xff;
            const uint8_t* g = chars + vram[(sy >> 3) * 32 + c] * 8 + (sy & 7);
            uint64_t w = kSpread.v[g[0]] | kSpread.v[g[0x800]] << 1 | kSpread.v[g[0x1000]] << 2;
            if (w) w |= nonzero_bytes(w) * pen;
            std::memcpy(buf + ((x0 + c * 8) & 0xff), &w, 8);
        }
        for (int i = 0; i < (x0 & 7); ++i) buf[i] = buf[256 + i];
    }

    uint8_t* obj = plane[0];
    std::memset(obj, 0, 256);
    on[0] = false;
    if (vs.mode & 0x80) {
        const uint8_t* sr = vs.sprites + ((vs.mode & 0x04) ? 0x80 : 0);
        const int colbase = 2 * ((vs.colorbank[1] >> 4) & 3);
        // Drawn 1E, 1D ... 0, then 1F on top (1F comes first in the chain).
        for (int k = 0x1f; k >= 0; --k) {
            const int which = (k - 1) & 0x1f;
            if (which >= 0x10 && which <= 0x17) continue;
            const uint8_t* s = sr + which * 4;
            const uint8_t sy = uint8_t(240 - s[1]);
            if (sy >= 240) continue;
            int r = line - sy;
            if (r < 0 || r > 15) continue;
            if (s[2] & 0x02) r = 15 - r;
            const uint8_t* g = vs.chars + ((s[3] & 0x40) ? 0x1800 : 0) + (s[3] & 0x3f) * 32 + (r & 7) + (r & 8) * 2;
            uint64_t half[2] = {kSpread.v[g[0]] | kSpread.v[g[0x800]] << 1 | kSpread.v[g[0x1000]] << 2,
                                kSpread.v[g[8]] | kSpread.v[g[0x808]] << 1 | kSpread.v[g[0x1008]] << 2};
            if (!(half[0] | half[1])) continue;
            on[0] = true;
            uint8_t px[16];
            std::memcpy(px, half, 16);
            const uint8_t pen = uint8_t((colbase + ((s[2] >> 2) & 1)) * 8);
            const bool fx = s[2] & 0x01;
            const int sx = uint8_t(s[0] - 1);
            for (int i = 0; i < 16; ++i) {
                const uint8_t p = px[fx ? 15 - i : i];
                if (!p) continue;
                int x = sx + i;
                if (x > 255) x -= 256;                  // also drawn at sx - 256
                if (x < kSpriteMinX || x > kSpriteMaxX) continue;
                obj[x] = uint8_t(pen | p);
            }
        }
    }

    // The priority PROM: for each mask of blank planes (bit 0 sprites, 1-3
    // playfields 1-3) the plane shown; all blank = the background (pen 0
    // of playfield 3's colour).
    uint8_t pick[16];
    const uint8_t* prom = roms_.prom + ((vs.priority & 0x0f) << 4);
    const int shift = (vs.priority & 0x10) ? 2 : 0;
    for (int m = 0; m < 16; ++m) pick[m] = uint8_t((prom[m] >> shift) & 3);
    const uint8_t bg = uint8_t(col[2]);
    // Usually the PROM's choice is a plain order of the planes (the shown
    // plane = the topmost that isn't blank): then the planes are laid on
    // each other 8 pixels at a time. Otherwise pixel by pixel.
    int order[4];                                       // top first
    bool plain = true;
    for (int k = 0, used = 0; k < 4; ++k) {
        order[k] = pick[used];
        if (used & (1 << order[k])) plain = false;
        used |= 1 << order[k];
    }
    for (int m = 0; m < 15 && plain; ++m)
        for (int k = 0; k < 4; ++k)
            if (!(m & (1 << order[k]))) {
                plain = pick[m] == order[k];
                break;
            }
    alignas(8) uint8_t pens[256];
    if (plain) {
        uint64_t* d = reinterpret_cast<uint64_t*>(pens);
        const uint64_t bgw = 0x0101010101010101ull * bg;
        for (int i = 0; i < 32; ++i) d[i] = bgw;
        for (int k = 3; k >= 0; --k) {
            const int p = order[k];
            if (!on[p]) continue;
            const uint8_t* src = plane[p];
            for (int i = 0; i < 32; ++i) {
                uint64_t v;
                std::memcpy(&v, src + 8 * i, 8);
                if (!v) continue;
                const uint64_t m = nonzero_bytes(v) * 0xff;
                d[i] = (d[i] & ~m) | v;
            }
        }
    } else {
        for (int p = 0; p < 4; ++p)
            if (!on[p]) std::memset(plane[p], 0, 256);
        for (int x = 0; x < 256; ++x) {
            const int m = (plane[0][x] ? 0 : 1) | (plane[1][x] ? 0 : 2) | (plane[2][x] ? 0 : 4) | (plane[3][x] ? 0 : 8);
            const uint8_t v = plane[pick[m]][x];
            pens[x] = v ? v : bg;
        }
    }
    uint16_t pal[64];                                   // (a local copy: no aliasing with `out`)
    std::memcpy(pal, vs.pens, sizeof pal);
    for (int x = 0; x < 256; x += 4) {
        out[x] = pal[pens[x] & 63];
        out[x + 1] = pal[pens[x + 1] & 63];
        out[x + 2] = pal[pens[x + 2] & 63];
        out[x + 3] = pal[pens[x + 3] & 63];
    }
}

} // namespace elevator
