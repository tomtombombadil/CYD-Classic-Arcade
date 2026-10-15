// Ms. Pac-Man machine - see mspacman.h.
#include "mspacman.h"

#include <cstring>

namespace mspacman {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x4000},
    {"aux",     kAuxSize},
    {"gfx1",    0x2000},
    {"proms",   0x0120},
    {"namco",   0x0200},
};

// CRCs as in MAME's "mspacman" set. The Pac-Man program ROMs, PROMs and
// sound PROMs are the "pacman" / "puckman" ones; the graphics (5E, 5F) are
// Ms. Pac-Man's own.
constexpr romset::Rom kRoms[] = {
    {kRegionCpu,   0x0000, 0x1000, 0xc1e6ab10, "pacman.6e", false},
    {kRegionCpu,   0x1000, 0x1000, 0x1a6fb2d4, "pacman.6f", false},
    {kRegionCpu,   0x2000, 0x1000, 0xbcdd1beb, "pacman.6h", false},
    {kRegionCpu,   0x3000, 0x1000, 0x817d94e3, "pacman.6j", false},
    {kRegionAux,   0x0000, 0x0800, 0xf45fbbcd, "u5", false},
    {kRegionAux,   0x0800, 0x1000, 0xa90e7000, "u6", false},
    {kRegionAux,   0x1800, 0x1000, 0xc82cd714, "u7", false},
    {kRegionGfx,   0x0000, 0x1000, 0x5c281d01, "5e", false},
    {kRegionGfx,   0x1000, 0x1000, 0x615af909, "5f", false},
    {kRegionProms, 0x0000, 0x0020, 0x2fc650bd, "82s123.7f", false},
    {kRegionProms, 0x0020, 0x0100, 0x3eb3a8e4, "82s126.4a", false},
    {kRegionSound, 0x0000, 0x0100, 0xa9cc86bf, "82s126.1m", false},
    {kRegionSound, 0x0100, 0x0100, 0x77245b66, "82s126.3m", true},   // timing, unused
};

// The aux board's wiring: for each ROM address line (index), the Z80
// address line driving it; for each Z80 data line (index), the ROM data
// line it reads. (The bit orders of MAME's bitswaps, as facts.)
constexpr uint8_t kU5Lines[11]  = {0, 1, 2, 4, 3, 6, 10, 9, 5, 7, 8};
constexpr uint8_t kU67Lines[12] = {0, 1, 2, 4, 5, 6, 8, 10, 9, 7, 3, 11};
constexpr uint8_t kDataLines[8] = {1, 2, 3, 6, 7, 5, 4, 0};

template <int N>
uint16_t rewire(uint16_t v, const uint8_t (&lines)[N])
{
    uint16_t out = 0;
    for (int b = 0; b < N; ++b) out = uint16_t(out | (((v >> lines[b]) & 1) << b));
    return out;
}

// A page of 0000-3FFF / 8000-BFFF, 0-31.
int page_of(uint16_t addr) { return ((addr >> 10) & 0x0f) | ((addr & 0x8000) ? 16 : 0); }

// The trap windows: clear the latch, then the one that sets it. 2120 is
// left out: its page (2000-23FF) holds Pac-Man code the game runs all the
// time (~7,000 reads a frame, each a call when trapped), and nothing reaches
// the window with the latch set - the power-on ROM check passes it with the
// latch already clear (it walks into 0038 first), and the random numbers
// Pac-Man's code reads from its ROM in blue time come from 0000-1FFF.
constexpr uint16_t kClearWindows[] = {0x0038, 0x03b0, 0x1600, 0x3ff0, 0x8000, 0x97f0};
constexpr uint16_t kSetWindow = 0x3ff8;

} // namespace

const romset::Set kRomSet = {
    "mspacman", nullptr, "Ms. Pac-Man", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

// Patch windows (US patent 4,525,599; the list as in MAME's driver).
constexpr Patch kPatches[kPatchCount] = {
    {0x0410, 0x008}, {0x08e0, 0x1d8}, {0x0a30, 0x118}, {0x0bd0, 0x0d8}, {0x0c20, 0x120},
    {0x0e58, 0x168}, {0x0ea8, 0x198}, {0x1000, 0x020}, {0x1008, 0x010}, {0x1288, 0x098},
    {0x1348, 0x048}, {0x1688, 0x088}, {0x16b0, 0x188}, {0x16d8, 0x0c8}, {0x16f8, 0x1c8},
    {0x19a8, 0x0a8}, {0x19b8, 0x1a8}, {0x2060, 0x148}, {0x2108, 0x018}, {0x21a0, 0x1a0},
    {0x2298, 0x0a0}, {0x23e0, 0x0e8}, {0x2418, 0x000}, {0x2448, 0x058}, {0x2470, 0x140},
    {0x2488, 0x080}, {0x24b0, 0x180}, {0x24d8, 0x0c0}, {0x24f8, 0x1c0}, {0x2748, 0x050},
    {0x2780, 0x090}, {0x27b8, 0x190}, {0x2800, 0x028}, {0x2b20, 0x100}, {0x2b30, 0x110},
    {0x2bf0, 0x1d0}, {0x2cc0, 0x0d0}, {0x2cd8, 0x0e0}, {0x2cf0, 0x1e0}, {0x2d60, 0x160},
};

// Pages holding a patch window (the table is in address order).
constexpr int patched_pages()
{
    int n = 0, last = -1;
    for (const Patch& w : kPatches)
        if ((w.addr >> 10) != last) { last = w.addr >> 10; ++n; }
    return n;
}
static_assert(patched_pages() == Machine::kPatchedPages, "patch pages");

uint16_t u5_address(uint16_t i) { return rewire(uint16_t(i & 0x7ff), kU5Lines); }
uint16_t u67_address(uint16_t i) { return rewire(uint16_t(i & 0xfff), kU67Lines); }
uint8_t  decode_data(uint8_t raw) { return uint8_t(rewire(raw, kDataLines)); }

void Machine::init(const pacman::Roms& roms, const uint8_t* aux, uint32_t audio_rate, bool swap_rgb565_bytes)
{
    pacman::Machine::init(roms, audio_rate, swap_rgb565_bytes);

    const uint8_t* u5 = aux;
    const uint8_t* u6 = aux + 0x800;
    const uint8_t* u7 = aux + 0x1800;
    for (uint16_t i = 0; i < 0x800; ++i) aux_[i] = decode_data(u5[u5_address(i)]);
    for (uint16_t i = 0; i < 0x1000; ++i) {
        aux_[0x800 + i] = decode_data(u6[u67_address(i) ^ 0x800]);
        u7_[i] = decode_data(u7[u67_address(i)]);
    }

    // Decode off: Pac-Man's ROMs, mirrored at 8000.
    for (int p = 0; p < kRomPages; ++p) page_[0][p] = roms.cpu + (p & 15) * 0x400;
    // Decode on: Pac-Man pages, the patched ones copied with their windows.
    int used = 0;
    for (int p = 0; p < 12; ++p) {
        uint8_t* copy = nullptr;
        for (const Patch& w : kPatches) {
            if ((w.addr >> 10) != p) continue;
            if (!copy) {
                copy = patched_[used++];
                std::memcpy(copy, roms.cpu + p * 0x400, 0x400);
            }
            std::memcpy(copy + (w.addr & 0x3ff), aux_ + w.aux, 8);
        }
        page_[1][p] = copy ? copy : roms.cpu + p * 0x400;
    }
    for (int p = 12; p < 16; ++p) page_[1][p] = u7_ + (p - 12) * 0x400;
    for (int p = 16; p < 22; ++p) page_[1][p] = aux_ + (p - 16) * 0x400;
    for (int p = 22; p < 24; ++p) page_[1][p] = roms.cpu + 0x1800 + (p - 22) * 0x400;
    for (int p = 24; p < 32; ++p) page_[1][p] = roms.cpu + 0x2000 + (p - 24) * 0x400;

    for (auto& w : window_) w[0] = w[1] = kNoWindow;
    auto add_window = [this](uint16_t a) {
        uint16_t* w = window_[page_of(a)];
        w[w[0] == kNoWindow ? 0 : 1] = a & 0x3f8;
    };
    for (uint16_t a : kClearWindows) add_window(a);
    add_window(kSetWindow);

    cpu_.mem_read = aux_read;
    cpu_.mem_write = aux_write;
    set_decode(true);
}

// Pages with a trap window go through the callbacks (aux_read); the others
// are mapped straight to the ROMs the latch selects.
void Machine::set_decode(bool on)
{
    decode_ = on;
    for (int p = 0; p < kRomPages; ++p) {
        const uint16_t addr = uint16_t(((p & 16) ? 0x8000 : 0) | ((p & 15) << 10));
        cpu_.map_read(addr, 0x400, window_[p][0] != kNoWindow ? nullptr : page_[on][p]);
    }
}

void Machine::trap(uint16_t addr)
{
    const bool on = (addr & 0xfff8) == kSetWindow;
    if (on == decode_) return;
    if (!on) {
        bool hit = false;
        for (uint16_t w : kClearWindows) hit = hit || (addr & 0xfff8) == w;
        if (!hit) return;
    }
    set_decode(on);
}

// The trap pages and the Pac-Man board's I/O. Any access to a trap window
// (instruction fetch, read or write) changes the latch first; the read then
// comes from the ROMs the new state selects.
uint8_t Machine::aux_read(void* ctx, uint16_t addr)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr & 0x4000) return pacman::Machine::mem_read(ctx, addr);
    const int p = page_of(addr);
    const uint16_t w = addr & 0x3f8;
    if (w == m.window_[p][0] || w == m.window_[p][1]) m.trap(addr);
    return m.page_[m.decode_][p][addr & 0x3ff];
}

void Machine::aux_write(void* ctx, uint16_t addr, uint8_t v)
{
    Machine& m = *static_cast<Machine*>(ctx);
    if (addr & 0x4000) pacman::Machine::mem_write(ctx, addr, v);
    else m.trap(addr);
}

} // namespace mspacman
