#!/usr/bin/env python3
"""Checks the arcade_host run on the synthetic ROM set (make_fake_romset.py):
the sprite moved left then right as scripted, and the WAV holds the 440 Hz
tone. Usage: check_fake_run.py OUTDIR"""
import struct
import sys
import wave

from PIL import Image


def sprite_left(path):
    im = Image.open(path).convert("RGB")
    for x in range(224 - 15):
        if all(im.getpixel((x + dx, 150)) == (0, 0, 255) for dx in range(16)) and \
           all(im.getpixel((x + 8, y)) == (0, 0, 255) for y in range(144, 160)):
            return x
    return None


def main():
    out = sys.argv[1]
    start, moved_left, moved_right = (sprite_left(f"{out}/pacman_{f:05d}.png") for f in (30, 120, 210))
    print(f"sprite left edge: frame 30 {start}, 120 {moved_left}, 210 {moved_right}")
    ok = None not in (start, moved_left, moved_right) and moved_left < start - 50 and moved_right > moved_left + 30
    w = wave.open(f"{out}/run.wav")
    data = w.readframes(w.getnframes())
    s = struct.unpack("<%dh" % (len(data) // 2), data)
    z = [i for i in range(1, len(s)) if s[i - 1] < 0 <= s[i]]
    hz = (len(z) - 1) * w.getframerate() / (z[-1] - z[0]) if len(z) > 1 else 0
    print(f"tone {hz:.2f} Hz, {w.getnframes()} samples at {w.getframerate()} Hz")
    ok = ok and abs(hz - 440.0) < 1.0
    print("pipeline check:", "ok" if ok else "FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
