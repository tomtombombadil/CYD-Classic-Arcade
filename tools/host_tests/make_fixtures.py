#!/usr/bin/env python3
"""Writes the ZIP / loose-file fixtures for unit_tests (synthetic data only,
no ROMs). Usage: make_fixtures.py OUTDIR

gen(n, k, j) must match unit_tests.cpp."""
import os
import sys
import zipfile


def gen(n, k, j):
    return bytes(((i * k + j) ^ (i >> 3)) & 0xFF for i in range(n))


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    # The "parent": t.1 under another name (stored), p.1 deflated, some junk.
    with zipfile.ZipFile(os.path.join(out, "parent.zip"), "w") as z:
        z.writestr(zipfile.ZipInfo("parent_t1.bin"), gen(4096, 3, 1), zipfile.ZIP_STORED)
        z.writestr("p.1", gen(256, 7, 3), zipfile.ZIP_DEFLATED)
        z.writestr("readme.txt", b"not a rom\n" * 20, zipfile.ZIP_DEFLATED)
    # The "clone": t.2 in a folder (deflated), and a t.3 with the wrong data.
    with zipfile.ZipFile(os.path.join(out, "clone.zip"), "w") as z:
        z.writestr("testgame/t.2", gen(4096, 5, 2), zipfile.ZIP_DEFLATED)
        z.writestr("T.3", gen(4096, 9, 9), zipfile.ZIP_DEFLATED)
        z.comment = b"a zip comment to skip"
    # The right t.3 as a loose file under any name.
    with open(os.path.join(out, "anything.rom"), "wb") as f:
        f.write(gen(4096, 1, 0))
    # A broken ZIP (cut short).
    with open(os.path.join(out, "clone.zip"), "rb") as f:
        data = f.read()
    with open(os.path.join(out, "broken.zip"), "wb") as f:
        f.write(data[: len(data) // 2])


if __name__ == "__main__":
    main()
