// Atari math box - see atari_mathbox.h.
//
// Registers R0-R15 are 16-bit; R12-R15 are the microprogram's scratch
// registers and keep their values between commands (command 12 tests R15
// as the last 0B / 11 left it).
#include "atari_mathbox.h"

namespace atari_mathbox {

namespace {

inline int16_t w16(int v) { return int16_t(uint16_t(v)); }
// A 16-bit shift left (no signed overflow).
inline int16_t shl(int16_t v, int n) { return int16_t(uint16_t(uint16_t(v) << n)); }

} // namespace

void Mathbox::reset()
{
    for (int16_t& r : r_) r = 0;
    result_ = 0;
}

// Products of 16-bit registers, their halves summed with the board's
// rounding (each low half halved, added; a negative sum carries one into
// the high half).
void Mathbox::rotate(bool both)
{
    int16_t* r = r_;
    int32_t p = int32_t(r[0]) * r[4];
    r[12] = w16(p >> 16);
    r[14] = w16(p);
    p = -int32_t(r[1]) * r[5];
    r[7] = w16(p >> 16);
    const int16_t low = w16(p);
    r[7] = w16(r[7] + r[12]);
    r[14] = w16((r[14] >> 1) & 0x7fff);
    r[12] = w16((low >> 1) & 0x7fff);
    if (w16(r[12] + r[14]) < 0) r[7] = w16(r[7] + 1);
    result_ = r[7];
    if (!both) return;
    r[7] = w16(r[7] + r[2]);
    rotate_y(true);
}

void Mathbox::rotate_y(bool go_on)
{
    int16_t* r = r_;
    int32_t p = int32_t(r[1]) * r[4];
    r[12] = w16(p >> 16);
    r[9] = w16(p);
    p = int32_t(r[0]) * r[5];
    r[8] = w16(p >> 16);
    const int16_t low = w16(p);
    r[8] = w16(r[8] + r[12]);
    r[9] = w16((r[9] >> 1) & 0x7fff);
    r[12] = w16((low >> 1) & 0x7fff);
    r[9] = w16(r[9] + r[12]);
    if (r[9] < 0) r[8] = w16(r[8] + 1);
    r[9] = shl(r[9], 1);
    result_ = r[8];
    if (!go_on) return;
    r[8] = w16(r[8] + r[3]);
    r[9] = w16(r[9] & 0xff00);
    divide(r[9], r[8]);
}

// (num_hi : num_lo) / R7, R6 + 1 quotient bits, the sign applied after.
void Mathbox::divide(int16_t num_lo, int16_t num_hi)
{
    int16_t* r = r_;
    int16_t q = num_hi;
    r[14] = w16(r[7] ^ q);                  // the result's sign
    r[13] = q;
    if (q >= 0) {
        q = num_lo;
    } else {
        // Negate the 32-bit numerator.
        r[13] = w16(-q - 1);
        q = w16(-num_lo - 1);
        if (q < 0 && int(q) + 1 < 0) r[13] = w16(r[13] + 1);
        q = w16(q + 1);
    }
    r[12] = r[7] >= 0 ? r[7] : w16(-r[7]);
    r[15] = r[6];
    do {
        r[13] = w16(r[13] - r[12]);
        const int msb = (q & 0x8000) != 0;
        q = shl(q, 1);
        if (r[13] >= 0) q = w16(q + 1);
        else            r[13] = w16(r[13] + r[12]);
        r[13] = w16(shl(r[13], 1) + msb);
        r[15] = w16(r[15] - 1);
    } while (r[15] >= 0);
    result_ = r[14] >= 0 ? q : w16(-q);
}

void Mathbox::go(int command, uint8_t data)
{
    int16_t* r = r_;
    auto set_lo = [&](int n) { r[n] = w16((r[n] & 0xff00) | data); result_ = r[n]; };
    auto set_hi = [&](int n) { r[n] = w16((r[n] & 0x00ff) | (data << 8)); result_ = r[n]; };
    switch (command & 31) {
    // Register loads (each also the result).
    case 0x00: set_lo(0); break;
    case 0x01: set_hi(0); break;
    case 0x02: set_lo(1); break;
    case 0x03: set_hi(1); break;
    case 0x04: set_lo(2); break;
    case 0x05: set_hi(2); break;
    case 0x06: set_lo(3); break;
    case 0x07: set_hi(3); break;
    case 0x08: set_lo(4); break;
    case 0x09: set_hi(4); break;
    case 0x0a: set_lo(5); break;
    case 0x0c: r[6] = data; result_ = r[6]; break;
    case 0x0d: set_lo(10); break;
    case 0x0e: set_hi(10); break;
    case 0x0f: set_lo(11); break;
    case 0x10: set_hi(11); break;
    case 0x15: set_lo(7); break;
    case 0x16: set_hi(7); break;
    case 0x1a: set_lo(8); break;
    case 0x1b: set_hi(8); break;
    // Read-backs.
    case 0x17: result_ = r[7]; break;
    case 0x18: result_ = r[9]; break;
    case 0x19: result_ = r[8]; break;
    // Rotate (R0, R1 = cos / sin) the point (R4 - R2, R5 - R3): 0B gives
    // the first coordinate only; 11 goes on through the perspective divide.
    case 0x0b:
        r[5] = w16((r[5] & 0x00ff) | (data << 8));
        r[15] = -1;
        r[4] = w16(r[4] - r[2]);
        r[5] = w16(r[5] - r[3]);
        rotate(false);
        break;
    case 0x11:
        r[5] = w16((r[5] & 0x00ff) | (data << 8));
        r[15] = 0;
        rotate(true);
        break;
    case 0x12: rotate_y(r[15] >= 0); break;
    case 0x13: divide(r[9], r[8]); break;
    case 0x14: divide(r[10], r[11]); break;
    case 0x1c:
        // Binary search of the window edge, R6 + 1 steps.
        r[5] = w16((r[5] & 0x00ff) | (data << 8));
        do {
            r[14] = w16((r[4] + r[7]) >> 1);
            r[15] = w16((r[5] + r[8]) >> 1);
            if (r[11] < r[14] && r[15] < r[14] && int(r[14]) + r[15] >= 0) {
                r[7] = r[14];
                r[8] = r[15];
            } else {
                r[4] = r[14];
                r[5] = r[15];
            }
            r[6] = w16(r[6] - 1);
        } while (r[6] >= 0);
        result_ = r[8];
        break;
    case 0x1d:
        r[3] = w16((r[3] & 0x00ff) | (data << 8));
        r[2] = w16(r[2] - r[0]);
        if (r[2] < 0) r[2] = w16(-r[2]);
        r[3] = w16(r[3] - r[1]);
        if (r[3] < 0) r[3] = w16(-r[3]);
        [[fallthrough]];
    case 0x1e: {
        // Distance ~ max + 3/8 min of |dx|, |dy|.
        int16_t lo = r[3] >= r[2] ? r[2] : r[3];
        int16_t hi = r[3] >= r[2] ? r[3] : r[2];
        lo = w16(lo >> 2);
        hi = w16(hi + lo);
        lo = w16(lo >> 1);
        r[12] = lo;
        r[13] = hi = w16(lo + hi);
        result_ = hi;
        break;
    }
    default:
        break;
    }
}

} // namespace atari_mathbox
