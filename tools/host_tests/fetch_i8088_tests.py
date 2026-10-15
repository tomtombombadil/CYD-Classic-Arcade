#!/usr/bin/env python3
"""Fetches the start of each SingleStepTests 8088 test file (MIT, Daniel
Balsom's tests captured from a real 8088) into a folder, checking each
against the pinned SHA-256 in i8088_tests.sha256 - never committed.

Only the first 128 KB of each gzipped MOO file is fetched (an HTTP range,
~600 tests per opcode instead of 10,000 - the whole suite is ~500 MB);
i8088_test.cpp reads the tests up to where the data ends.

  fetch_i8088_tests.py OUTDIR             fetch (once) and check
  fetch_i8088_tests.py OUTDIR --cat       write the tests to stdout for
                                          i8088_test: per file a line
                                          "FILE <name> <flags mask hex>
                                          <bytes>" and the unpacked MOO data
  fetch_i8088_tests.py OUTDIR --manifest  write a new i8088_tests.sha256
                                          (after moving the pin)

The flags mask (from the suite's metadata.json, pinned too) clears the
flags the 8088 leaves undefined for that opcode.
"""
import hashlib
import json
import os
import subprocess
import sys
import zlib

COMMIT = "aea84484abc79d09639d855b7b0ab32bc9e4dbeb"
URL = "https://raw.githubusercontent.com/SingleStepTests/8088/%s/v2_binary/%s.MOO.gz"
META = "https://raw.githubusercontent.com/SingleStepTests/8088/%s/v2/metadata.json"
PART = 128 * 1024
HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "i8088_tests.sha256")


def fetch(name, path):
    subprocess.run(["curl", "-sSfL", "--retry", "8", "--retry-delay", "3", "-r", "0-%d" % (PART - 1),
                    "-o", path, URL % (COMMIT, name)], check=True)


def fetch_meta(path):
    subprocess.run(["curl", "-sSfL", "-o", path, META % COMMIT], check=True)


def mask_of(ops, name):
    """The opcode's flags-mask from metadata.json (0xFFFF when none)."""
    op, _, reg = name.partition(".")
    v = ops[op]
    if "reg" in v:
        v = v["reg"][reg or "0"]
    return int(v.get("flags-mask", 0xFFFF))


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    mode = sys.argv[2] if len(sys.argv) > 2 else ""
    meta_path = os.path.join(out, "metadata.json")
    if mode == "--manifest":
        fetch_meta(meta_path)
        with open(meta_path) as f:
            ops = json.load(f)["opcodes"]
        names = []
        for op, v in sorted(ops.items()):
            if v.get("status") == "prefix" or op in ("0F", "9B", "F4"):   # not in the suite
                continue
            if "reg" in v:
                regs = [r for r, rv in sorted(v["reg"].items()) if rv.get("status") != "undefined"]
                # 8F / C6 / C7: only reg 0 is defined, and the file has the bare name
                names += [op] if regs == ["0"] else ["%s.%s" % (op, r) for r in regs]
            elif v.get("status") != "undefined":
                names.append(op)
        lines = ["metadata.json %s\n" % sha256(meta_path)]
        for n in names:
            p = os.path.join(out, n + ".MOO.gz")
            if not os.path.exists(p):
                fetch(n, p)
            lines.append("%s %s\n" % (n, sha256(p)))
        with open(MANIFEST, "w") as f:
            f.writelines(lines)
        print("wrote %s (%d files)" % (MANIFEST, len(lines)))
        return
    with open(MANIFEST) as f:
        entries = [l.split() for l in f if l.strip()]
    if mode == "--cat":
        with open(meta_path) as f:
            ops = json.load(f)["opcodes"]
        w = sys.stdout.buffer
        for name, _ in entries[1:]:
            with open(os.path.join(out, name + ".MOO.gz"), "rb") as f:
                data = zlib.decompressobj(31).decompress(f.read())   # the cut-off end is dropped
            w.write(b"FILE %s %04x %d\n" % (name.encode(), mask_of(ops, name), len(data)))
            w.write(data)
        return
    for name, want in entries:
        p = meta_path if name == "metadata.json" else os.path.join(out, name + ".MOO.gz")
        for attempt in (0, 1):
            if attempt or not os.path.exists(p):
                if name == "metadata.json": fetch_meta(p)
                else: fetch(name, p)
            if sha256(p) == want:
                break
        else:
            sys.exit("%s: SHA-256 %s, expected %s" % (p, sha256(p), want))
    print("8088 tests: %d files checked" % len(entries))


if __name__ == "__main__":
    main()
