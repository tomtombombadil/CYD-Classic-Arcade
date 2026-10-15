// The Williams SC1 blitter - see williams_blitter.h.
#include "williams_blitter.h"

void WilliamsBlitter::reset()
{
    control_ = solid_ = width_ = height_ = 0;
    src_ = dst_ = 0;
}

int WilliamsBlitter::write(int reg, uint8_t v, m6809::Cpu& cpu, uint8_t* ram)
{
    switch (reg & 7) {
    case 0: control_ = v; return blit(cpu, ram);
    case 1: solid_ = v; break;
    case 2: src_ = uint16_t((src_ & 0x00ff) | (v << 8)); break;
    case 3: src_ = uint16_t((src_ & 0xff00) | v); break;
    case 4: dst_ = uint16_t((dst_ & 0x00ff) | (v << 8)); break;
    case 5: dst_ = uint16_t((dst_ & 0xff00) | v); break;
    case 6: width_ = v; break;
    default: height_ = v; break;
    }
    return 0;
}

// Each byte holds two pixels: the even (left) one in bits 4-7. A pixel is
// written when "it shows" (not foreground-only, or its source pixel isn't
// 0) differs from its skip bit - so foreground-only with a skip bit writes
// exactly the transparent pixels, as the chip does.
int WilliamsBlitter::blit(m6809::Cpu& cpu, uint8_t* ram)
{
    int w = width_ ^ size_xor, h = height_ ^ size_xor;
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    const bool src_screen = control_ & 0x01, dst_screen = control_ & 0x02;
    const bool slow = control_ & 0x04, fg_only = control_ & 0x08, solid = control_ & 0x10;
    const bool shift = control_ & 0x20, skip_odd = control_ & 0x40, skip_even = control_ & 0x80;
    const uint16_t sx = src_screen ? 0x100 : 1, sy = src_screen ? 1 : uint16_t(w);
    const uint16_t dx = dst_screen ? 0x100 : 1, dy = dst_screen ? 1 : uint16_t(w);

    unsigned pix = 0;                   // shift register (kept across rows, as on the chip)
    uint16_t srow = src_, drow = dst_;
    for (int y = 0; y < h; ++y) {
        uint16_t s = srow, d = drow;
        for (int x = 0; x < w; ++x) {
            uint8_t data = cpu.rd(s);
            if (shift) {
                pix = (pix << 8) | data;
                data = uint8_t(pix >> 4);
            }
            const bool show_even = !(fg_only && !(data & 0xf0));
            const bool show_odd = !(fg_only && !(data & 0x0f));
            uint8_t keep = 0xff;
            if (show_even != skip_even) keep &= 0x0f;
            if (show_odd != skip_odd) keep &= 0xf0;
            const uint8_t old = d < 0xc000 ? ram[d] : cpu.rd(d);
            const uint8_t val = uint8_t((old & keep) | ((solid ? solid_ : data) & ~keep));
            if (d < 0xc000) ram[d] = val;
            else            cpu.wr(d, val);
            s = uint16_t(s + sx);
            d = uint16_t(d + dx);
        }
        // In screen layout a row steps the low byte (the line) only.
        srow = src_screen ? uint16_t((srow & 0xff00) | ((srow + sy) & 0xff)) : uint16_t(srow + sy);
        drow = dst_screen ? uint16_t((drow & 0xff00) | ((drow + dy) & 0xff)) : uint16_t(drow + dy);
    }
    ++blits_;
    const int bytes = w * h;
    bytes_ += uint32_t(bytes);
    // MAME's estimate: the 4 MHz chip makes 2 accesses a byte, 2 clocks
    // each (4 when slow), plus a few to start; the CPU runs at 1 MHz.
    const int accesses = 2 * bytes;
    const int clocks = slow ? 4 + 4 * (accesses + 2) : 4 + 2 * (accesses + 3);
    return (clocks + 3) / 4;
}
