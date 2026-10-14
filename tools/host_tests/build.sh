#!/bin/sh
# Host tests for the emulator core: tools/host_tests/build.sh [--quick]
#   1. unit tests (CRC-32, ZIP, ROM sets, WSG, Z80 timing / interrupts,
#      Pac-Man wiring), -O2 and with -fsanitize=address,undefined
#   2. ZEXDOC and ZEXALL (downloaded on first use into build/zex - GPL test
#      programs, never committed), -O2; the sanitizer build runs ZEXDOC for
#      a limited number of T-states (the full run takes ~10x longer)
#   3. throughput benchmark (emulated MHz on this host)
#   4. the host runner end to end on a synthetic ROM set
# --quick skips the full ZEXDOC / ZEXALL runs.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build/host_tests
ZEX=$ROOT/build/zex
mkdir -p "$OUT" "$ZEX"
QUICK=0
[ "$1" = "--quick" ] && QUICK=1

EMU="$ROOT/emu/cpu/z80.cpp $ROOT/emu/rom/crc32.cpp $ROOT/emu/rom/zip.cpp $ROOT/emu/rom/romset.cpp \
     $ROOT/emu/util/inflate.cpp $ROOT/emu/sound/namco_wsg.cpp $ROOT/emu/machines/pacman.cpp"
FLAGS="-std=c++17 -Wall -Wextra -Werror"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

echo "== build"
g++ $FLAGS -O2 -o "$OUT/unit_tests" "$ROOT/tools/host_tests/unit_tests.cpp" $EMU
g++ $FLAGS $SAN -o "$OUT/unit_tests_san" "$ROOT/tools/host_tests/unit_tests.cpp" $EMU
g++ $FLAGS -O2 -o "$OUT/zex_test" "$ROOT/tools/host_tests/zex_test.cpp" "$ROOT/emu/cpu/z80.cpp"
g++ $FLAGS $SAN -o "$OUT/zex_test_san" "$ROOT/tools/host_tests/zex_test.cpp" "$ROOT/emu/cpu/z80.cpp"

echo "== unit tests"
python3 "$ROOT/tools/host_tests/make_fixtures.py" "$OUT/fixtures"
"$OUT/unit_tests" "$OUT/fixtures"
"$OUT/unit_tests_san" "$OUT/fixtures"

echo "== host runner on a synthetic ROM set (no game code)"
"$ROOT/tools/host/build.sh" "$OUT" > /dev/null
python3 "$ROOT/tools/host_tests/make_fake_romset.py" "$OUT/fake"
rm -rf "$OUT/fake_out" && mkdir -p "$OUT/fake_out"
"$OUT/arcade_host" pacman "$OUT/fake" --frames 240 --png-every 30 --out "$OUT/fake_out" \
    --wav "$OUT/fake_out/run.wav" --input "60+60:left,150+40:right" | tail -1
python3 "$ROOT/tools/host_tests/check_fake_run.py" "$OUT/fake_out"
"$OUT/arcade_host" pacman "$OUT/fixtures" > /dev/null 2>&1 && { echo "runner accepted a folder without ROMs"; exit 1; }
echo "missing ROMs reported: ok"

echo "== zexdoc / zexall"
# Frank D. Cringle's exercisers (GPL), from a public emulator repository.
fetch() {
    if [ ! -s "$ZEX/$1" ]; then
        curl -sSfL -o "$ZEX/$1" "https://raw.githubusercontent.com/anotherlin/z80emu/master/testfiles/$1"
    fi
    echo "$2  $ZEX/$1" | sha256sum -c --quiet -
}
fetch zexdoc.com 34923a7ed82285d3038b2d54bd64899e12173eebb61f9d07b4fc72e78af2ae8f
fetch zexall.com 6e2da55147a04f28d303d5da6a1e6b771557ac244653590a0f24a2d39c8537e8
"$OUT/zex_test_san" "$ZEX/zexdoc.com" --bench 300
if [ $QUICK = 0 ]; then
    "$OUT/zex_test" "$ZEX/zexdoc.com"
    "$OUT/zex_test" "$ZEX/zexall.com"
fi

echo "== benchmark"
"$OUT/zex_test" "$ZEX/zexdoc.com" --bench 3000
