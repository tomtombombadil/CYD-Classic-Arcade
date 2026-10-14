#!/usr/bin/env python3
"""Writes OUTDIR/pacman.zip: a SYNTHETIC stand-in for the Pac-Man ROM set,
to test the whole host pipeline (ZIP -> ROM matcher -> machine -> PNG / WAV
-> input script) before real ROMs are at hand. Nothing in it comes from the
game: a tiny hand-written Z80 program (fills the screen with tiles, plays a
440 Hz tone, moves a sprite with left / right), made-up graphics and PROMs.
The last 4 bytes of each file are chosen so its CRC-32 equals the real
file's, which is all the matcher looks at. Test output only - git-ignored.

Usage: make_fake_romset.py OUTDIR"""
import math
import os
import sys
import zipfile
import zlib


def forge_crc(data, target):
    """Returns data with its last 4 bytes replaced so crc32 == target. CRC-32
    is affine over GF(2), so solve for the 32 free bits."""
    base = bytearray(data[:-4]) + b"\0\0\0\0"
    c0 = zlib.crc32(bytes(base))
    cols = []
    for bit in range(32):
        trial = bytearray(base)
        trial[len(base) - 4 + bit // 8] ^= 1 << (bit % 8)
        cols.append(zlib.crc32(bytes(trial)) ^ c0)
    want = target ^ c0
    # Gaussian elimination: rows = output bits, 32 unknowns.
    rows = []
    for out_bit in range(32):
        mask = 0
        for bit in range(32):
            if (cols[bit] >> out_bit) & 1:
                mask |= 1 << bit
        rows.append([mask, (want >> out_bit) & 1])
    pivots = []
    r = 0
    for col in range(32):
        piv = next((i for i in range(r, 32) if (rows[i][0] >> col) & 1), None)
        if piv is None:
            continue
        rows[r], rows[piv] = rows[piv], rows[r]
        for i in range(32):
            if i != r and (rows[i][0] >> col) & 1:
                rows[i][0] ^= rows[r][0]
                rows[i][1] ^= rows[r][1]
        pivots.append(col)
        r += 1
    x = 0
    for i, col in enumerate(pivots):
        if rows[i][1]:
            x |= 1 << col
    out = bytearray(base)
    for k in range(4):
        out[len(base) - 4 + k] = (x >> (8 * k)) & 0xFF
    assert zlib.crc32(bytes(out)) == target
    return bytes(out)


PROGRAM = bytes([
    0xF3, 0x31, 0xC0, 0x4F, 0xED, 0x5E, 0x3E, 0x3F, 0xED, 0x47,  # DI; LD SP; IM 2; I=3F
    0x3E, 0x80, 0xD3, 0x00,                                      # vector 80
    0x21, 0x00, 0x40, 0x75, 0x23, 0x7C, 0xFE, 0x44, 0x20, 0xF9,  # video RAM = low address byte
    0x36, 0x01, 0x23, 0x7C, 0xFE, 0x48, 0x20, 0xF8,              # colour RAM = 1
    0x21, 0x50, 0x50, 0x36, 0x06, 0x23, 0x36, 0x0C, 0x23, 0x36, 0x02,
    0x23, 0x36, 0x01, 0x23, 0x36, 0x00, 0x23, 0x36, 0x0A,        # voice 0: f 0x012C6, volume 10
    0xAF, 0x32, 0x45, 0x50,                                      # waveform 0
    0x3E, 0x70, 0x32, 0x10, 0x4C,                                # sprite position variable
    0x3E, 0x01, 0x32, 0x00, 0x50, 0x32, 0x01, 0x50, 0xFB,        # IRQ + sound on, EI
    0x32, 0xC0, 0x50, 0x18, 0xFB,                                # loop: kick the watchdog
])
ISR = bytes([
    0xF5, 0xE5, 0xAF, 0x32, 0x00, 0x50,                          # save; acknowledge
    0x21, 0x10, 0x4C, 0x3A, 0x00, 0x50,                          # HL = var; A = IN0
    0xCB, 0x4F, 0x20, 0x01, 0x34,                                # left: INC (HL)
    0xCB, 0x57, 0x20, 0x01, 0x35,                                # right: DEC (HL)
    0x7E, 0x32, 0x60, 0x50, 0x3E, 0x80, 0x32, 0x61, 0x50,        # sprite 0 position
    0x3E, 0x04, 0x32, 0xF0, 0x4F, 0x3E, 0x01, 0x32, 0xF1, 0x4F,  # code 1, colour 1
    0x32, 0x00, 0x50, 0xE1, 0xF1, 0xFB, 0xED, 0x4D,              # IRQ on; restore; EI; RETI
])


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    cpu = bytearray(0x4000)
    cpu[0:len(PROGRAM)] = PROGRAM
    cpu[0x100:0x100 + len(ISR)] = ISR
    cpu[0x3F80:0x3F82] = b"\x00\x01"
    gfx = bytearray(0x2000)
    for i in range(0x1000):                       # tiles: a busy made-up pattern
        gfx[i] = ((i * 37) ^ (i >> 4)) & 0xFF
    gfx[0x1040:0x1080] = b"\xff" * 64             # sprite 1 solid
    pal = bytearray(32)
    pal[1:4] = bytes([0x07, 0x38, 0xC0])          # red, green, blue
    lut = bytearray(256)
    lut[4:8] = bytes([0, 1, 2, 3])
    wave = bytearray(512)
    for i in range(32):
        wave[i] = int(round(7.5 + 7.5 * math.sin(2 * math.pi * (i + 0.5) / 32)))

    files = [
        ("pacman.6e", bytes(cpu[0x0000:0x1000]), 0xC1E6AB10),
        ("pacman.6f", bytes(cpu[0x1000:0x2000]), 0x1A6FB2D4),
        ("pacman.6h", bytes(cpu[0x2000:0x3000]), 0xBCDD1BEB),
        ("pacman.6j", bytes(cpu[0x3000:0x4000]), 0x817D94E3),
        ("pacman.5e", bytes(gfx[0x0000:0x1000]), 0x0C944964),
        ("pacman.5f", bytes(gfx[0x1000:0x2000]), 0x958FEDF9),
        ("82s123.7f", bytes(pal), 0x2FC650BD),
        ("82s126.4a", bytes(lut), 0x3EB3A8E4),
        ("82s126.1m", bytes(wave[0:256]), 0xA9CC86BF),
    ]
    with zipfile.ZipFile(os.path.join(out, "pacman.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for name, data, crc in files:
            z.writestr("fake_" + name, forge_crc(data, crc))


if __name__ == "__main__":
    main()
