#!/bin/sh
# Host tests for the emulator core: tools/host_tests/build.sh [--quick]
#   1. unit tests (CRC-32, ZIP, ROM sets, WSG, Z80 timing / interrupts,
#      Pac-Man wiring, 8080 timing / flags / interrupts, 8035 timing / flags
#      / timer / interrupts / ports, WAV parser, sample player, Space
#      Invaders sound models and wiring, Rally-X scroll / radar maths,
#      crash sound model and wiring, Donkey Kong wiring), -O2 and with
#      -fsanitize=address,undefined
#   2. ZEXDOC and ZEXALL (downloaded on first use into build/zex - GPL test
#      programs, never committed), -O2; the sanitizer build runs ZEXDOC for
#      a limited number of T-states (the full run takes ~10x longer)
#   3. the 8080 tests 8080PRE, TST8080, CPUTEST and 8080EXM (downloaded on
#      first use into build/i8080 - not ours, never committed): the short
#      ones in both builds, 8080EXM in full at -O2 and for a limited number
#      of states with the sanitizers
#   4. throughput benchmarks (emulated MHz on this host)
#   5. the host runner end to end on a synthetic ROM set
#   6. the 6809 core (flags, every indexed mode, cycles, interrupts incl.
#      CWAI / SYNC, small programs), -O2 and with the sanitizers
#   7. the 6800 / 6802 / 6808 core (flags, cycles, interrupts incl. WAI),
#      -O2 and with the sanitizers
#   8. the Williams parts (6821 PIA, SC1 blitter, sound board, the Robotron
#      and Defender boards on hand-made programs), -O2 and with the
#      sanitizers
#   9. the Namco Galaga-family parts (06XX / 51XX / 53XX, starfield, 54XX
#      model, the three-CPU board and idle skipping on hand-made programs,
#      Galaga / Dig Dug video), -O2 and with the sanitizers
#  10. every unprefixed Z80 opcode's time, the SN76489, Ms. Pac-Man's aux
#      board and the Mr. Do! machine on hand-made programs and synthetic
#      graphics, -O2 and with the sanitizers
#  11. the 6502: Klaus Dormann's functional test and the decimal test
#      (downloaded on first use into build/m6502 - not ours, never
#      committed; the decimal test's source is assembled by asm6502.py with
#      the N / V / Z checks on), both builds; atari_tests (6502 timing /
#      interrupts / opcode hook, POKEY, the vector display, DVG, Asteroids
#      sound and wiring, AVG, math box, Tempest wiring), -O2 and with the
#      sanitizers; bzone_tests (the AVG's clip window, the Battlezone sound
#      models, the treads, the board's wiring, idle skipping), both builds
#  12. Burger Time: the AY-3-8910 (tones, noise, envelopes, bus, ports),
#      the DECO CPU-7 opcode scramble and the board's wiring on hand-made
#      programs, -O2 and with the sanitizers
#  13. the Q*bert board (8088 + 6502 sound board, video, the speech and
#      knocker models, the diagonal stick, idle skipping) on hand-made
#      programs, -O2 and with the sanitizers
#  14. the Star Wars board (TMS5220, matrix processor on synthetic PROMs,
#      divider, LFSR, RIOT, the AVG's Star Wars variant, wiring on hand-made
#      6809 programs), -O2 and with the sanitizers
#  15. the 8088: hand-made checks and the SingleStepTests 8088 suite (the
#      start of each opcode's file, ~600 tests each, fetched on first use
#      into build/i8088 by fetch_i8088_tests.py - not ours, never
#      committed), both builds
# and the ROM store / USB protocol (core/), with the web page's loader
# (web/roms/loader.js, in Node when present) against a fake board.
# --quick skips the full ZEXDOC / ZEXALL / 8080EXM runs.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build/host_tests
ZEX=$ROOT/build/zex
I80=$ROOT/build/i8080
M65=$ROOT/build/m6502
I88=$ROOT/build/i8088
mkdir -p "$OUT" "$ZEX" "$I80" "$M65" "$I88"
QUICK=0
[ "$1" = "--quick" ] && QUICK=1

EMU="$ROOT/emu/cpu/z80.cpp $ROOT/emu/cpu/i8080.cpp $ROOT/emu/cpu/i8035.cpp $ROOT/emu/cpu/m6502.cpp \
     $ROOT/emu/cpu/i8088.cpp $ROOT/emu/rom/crc32.cpp $ROOT/emu/rom/zip.cpp $ROOT/emu/rom/romset.cpp \
     $ROOT/emu/util/inflate.cpp $ROOT/emu/sound/namco_wsg.cpp $ROOT/emu/sound/invaders_sound.cpp \
     $ROOT/emu/sound/rallyx_bang.cpp $ROOT/emu/sound/dkong_sound.cpp $ROOT/emu/sound/namco54.cpp \
     $ROOT/emu/sound/samples.cpp $ROOT/emu/sound/sn76489.cpp $ROOT/emu/machines/pacman.cpp \
     $ROOT/emu/machines/invaders.cpp $ROOT/emu/machines/rallyx.cpp $ROOT/emu/machines/dkong.cpp \
     $ROOT/emu/machines/namco_io.cpp $ROOT/emu/machines/starfield05.cpp $ROOT/emu/cpu/m6809.cpp \
     $ROOT/emu/cpu/m6800.cpp \
     $ROOT/emu/machines/namco_galaga_common.cpp $ROOT/emu/machines/galaga.cpp $ROOT/emu/machines/digdug.cpp \
     $ROOT/emu/machines/mspacman.cpp $ROOT/emu/machines/mrdo.cpp $ROOT/emu/sound/ay8910.cpp \
     $ROOT/emu/machines/btime.cpp $ROOT/emu/sound/qbert_sound.cpp $ROOT/emu/machines/qbert.cpp \
     $ROOT/emu/sound/pokey.cpp $ROOT/emu/sound/asteroids_sound.cpp $ROOT/emu/machines/vector_display.cpp \
     $ROOT/emu/machines/atari_dvg.cpp $ROOT/emu/machines/asteroids.cpp $ROOT/emu/machines/atari_avg.cpp \
     $ROOT/emu/machines/atari_mathbox.cpp $ROOT/emu/machines/tempest.cpp \
     $ROOT/emu/sound/bzone_sound.cpp $ROOT/emu/machines/bzone.cpp \
     $ROOT/emu/machines/pia6821.cpp $ROOT/emu/machines/williams_blitter.cpp \
     $ROOT/emu/sound/williams_sound.cpp $ROOT/emu/machines/williams.cpp $ROOT/emu/sound/qix_sound.cpp \
     $ROOT/emu/machines/qix.cpp $ROOT/emu/sound/tms5220.cpp $ROOT/emu/machines/riot6532.cpp \
     $ROOT/emu/machines/starwars_mathbox.cpp $ROOT/emu/machines/starwars.cpp"
FLAGS="-std=c++17 -Wall -Wextra -Werror"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"

# The emulator is compiled once per build (-O2 and with the sanitizers), on
# every core, into a library the tests link: compiling all of emu/ again for
# each test program, twice, took most of CI's 10 minutes (Tom, 2026-10-20).
JOBS=$(nproc 2>/dev/null || echo 2)
emu_lib() {     # emu_lib <name> <flags...>
    name=$1
    shift
    mkdir -p "$OUT/obj_$name"
    rm -f "$OUT/obj_$name"/*.o "$OUT/libemu_$name.a"
    printf '%s\n' $EMU "$ROOT/emu/games.cpp" |
        xargs -P "$JOBS" -I{} sh -c 'g++ $0 -c "$1" -o "$2/$(basename "$1" .cpp).o"' "$FLAGS $*" {} "$OUT/obj_$name"
    ar rcs "$OUT/libemu_$name.a" "$OUT/obj_$name"/*.o
}
echo "== emulator library (-O2, sanitizers; $JOBS jobs)"
emu_lib o2 -O2
emu_lib san $SAN
LIB_O2="$OUT/libemu_o2.a"
LIB_SAN="$OUT/libemu_san.a"

echo "== build"
g++ $FLAGS -O2 -o "$OUT/unit_tests" "$ROOT/tools/host_tests/unit_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/unit_tests_san" "$ROOT/tools/host_tests/unit_tests.cpp" "$LIB_SAN"
g++ $FLAGS -O2 -o "$OUT/zex_test" "$ROOT/tools/host_tests/zex_test.cpp" "$ROOT/emu/cpu/z80.cpp"
g++ $FLAGS $SAN -o "$OUT/zex_test_san" "$ROOT/tools/host_tests/zex_test.cpp" "$ROOT/emu/cpu/z80.cpp"
g++ $FLAGS -O2 -o "$OUT/i8080_test" "$ROOT/tools/host_tests/i8080_test.cpp" "$ROOT/emu/cpu/i8080.cpp"
g++ $FLAGS $SAN -o "$OUT/i8080_test_san" "$ROOT/tools/host_tests/i8080_test.cpp" "$ROOT/emu/cpu/i8080.cpp"
g++ $FLAGS -O2 -o "$OUT/m6502_test" "$ROOT/tools/host_tests/m6502_test.cpp" "$ROOT/emu/cpu/m6502.cpp"
g++ $FLAGS $SAN -o "$OUT/m6502_test_san" "$ROOT/tools/host_tests/m6502_test.cpp" "$ROOT/emu/cpu/m6502.cpp"
g++ $FLAGS -O2 -o "$OUT/i8088_test" "$ROOT/tools/host_tests/i8088_test.cpp" "$ROOT/emu/cpu/i8088.cpp"
g++ $FLAGS $SAN -o "$OUT/i8088_test_san" "$ROOT/tools/host_tests/i8088_test.cpp" "$ROOT/emu/cpu/i8088.cpp"

echo "== ROM store and USB loading (core/)"
CORE="$ROOT/core/rom_store.cpp $ROOT/core/usb_import.cpp $ROOT/emu/rom/crc32.cpp"
g++ $FLAGS -O2 -o "$OUT/core_tests" "$ROOT/tools/host_tests/core_tests.cpp" $CORE
g++ $FLAGS $SAN -o "$OUT/core_tests_san" "$ROOT/tools/host_tests/core_tests.cpp" $CORE
"$OUT/core_tests"
"$OUT/core_tests_san"

echo "== unit tests"
python3 "$ROOT/tools/host_tests/make_fixtures.py" "$OUT/fixtures"
"$OUT/unit_tests" "$OUT/fixtures"
"$OUT/unit_tests_san" "$OUT/fixtures"

echo "== 6809 tests"
g++ $FLAGS -O2 -o "$OUT/m6809_tests" "$ROOT/tools/host_tests/m6809_tests.cpp" "$ROOT/emu/cpu/m6809.cpp"
g++ $FLAGS $SAN -o "$OUT/m6809_tests_san" "$ROOT/tools/host_tests/m6809_tests.cpp" "$ROOT/emu/cpu/m6809.cpp"
"$OUT/m6809_tests"
"$OUT/m6809_tests_san"

echo "== 6800 tests"
g++ $FLAGS -O2 -o "$OUT/m6800_tests" "$ROOT/tools/host_tests/m6800_tests.cpp" "$ROOT/emu/cpu/m6800.cpp"
g++ $FLAGS $SAN -o "$OUT/m6800_tests_san" "$ROOT/tools/host_tests/m6800_tests.cpp" "$ROOT/emu/cpu/m6800.cpp"
"$OUT/m6800_tests"
"$OUT/m6800_tests_san"

echo "== Williams board tests"
g++ $FLAGS -O2 -o "$OUT/williams_tests" "$ROOT/tools/host_tests/williams_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/williams_tests_san" "$ROOT/tools/host_tests/williams_tests.cpp" "$LIB_SAN"
"$OUT/williams_tests"
"$OUT/williams_tests_san"

echo "== Qix board tests"
g++ $FLAGS -O2 -o "$OUT/qix_tests" "$ROOT/tools/host_tests/qix_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/qix_tests_san" "$ROOT/tools/host_tests/qix_tests.cpp" "$LIB_SAN"
"$OUT/qix_tests"
"$OUT/qix_tests_san"

echo "== Namco board tests"
g++ $FLAGS -O2 -o "$OUT/namco_tests" "$ROOT/tools/host_tests/namco_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/namco_tests_san" "$ROOT/tools/host_tests/namco_tests.cpp" "$LIB_SAN"
"$OUT/namco_tests"
"$OUT/namco_tests_san"

echo "== Ms. Pac-Man aux board, Mr. Do!, SN76489"
g++ $FLAGS -O2 -o "$OUT/mspacman_mrdo_tests" "$ROOT/tools/host_tests/mspacman_mrdo_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/mspacman_mrdo_tests_san" "$ROOT/tools/host_tests/mspacman_mrdo_tests.cpp" "$LIB_SAN"
"$OUT/mspacman_mrdo_tests"
"$OUT/mspacman_mrdo_tests_san"
echo "== Atari board tests (6502, POKEY, vector display, DVG, AVG, math box, Asteroids, Tempest)"
g++ $FLAGS -O2 -o "$OUT/atari_tests" "$ROOT/tools/host_tests/atari_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/atari_tests_san" "$ROOT/tools/host_tests/atari_tests.cpp" "$LIB_SAN"
"$OUT/atari_tests"
"$OUT/atari_tests_san"
echo "== Battlezone tests (AVG clip window, sound models, treads, board wiring, idle skipping)"
g++ $FLAGS -O2 -o "$OUT/bzone_tests" "$ROOT/tools/host_tests/bzone_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/bzone_tests_san" "$ROOT/tools/host_tests/bzone_tests.cpp" "$LIB_SAN"
"$OUT/bzone_tests"
"$OUT/bzone_tests_san"

echo "== Star Wars tests (TMS5220, matrix processor, divider, LFSR, RIOT, AVG variant, board wiring)"
g++ $FLAGS -O2 -o "$OUT/starwars_tests" "$ROOT/tools/host_tests/starwars_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/starwars_tests_san" "$ROOT/tools/host_tests/starwars_tests.cpp" "$LIB_SAN"
"$OUT/starwars_tests"
"$OUT/starwars_tests_san"

echo "== Burger Time tests (AY-3-8910, DECO CPU-7, board wiring)"
g++ $FLAGS -O2 -o "$OUT/btime_tests" "$ROOT/tools/host_tests/btime_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/btime_tests_san" "$ROOT/tools/host_tests/btime_tests.cpp" "$LIB_SAN"
"$OUT/btime_tests"
"$OUT/btime_tests_san"
echo "== Q*bert board tests"
g++ $FLAGS -O2 -o "$OUT/qbert_tests" "$ROOT/tools/host_tests/qbert_tests.cpp" "$LIB_O2"
g++ $FLAGS $SAN -o "$OUT/qbert_tests_san" "$ROOT/tools/host_tests/qbert_tests.cpp" "$LIB_SAN"
"$OUT/qbert_tests"
"$OUT/qbert_tests_san"

echo "== host runner on a synthetic ROM set (no game code)"
"$ROOT/tools/host/build.sh" "$OUT" "$LIB_O2" > /dev/null
python3 "$ROOT/tools/host_tests/make_fake_romset.py" "$OUT/fake"
rm -rf "$OUT/fake_out" && mkdir -p "$OUT/fake_out"
"$OUT/arcade_host" pacman "$OUT/fake" --frames 240 --png-every 30 --out "$OUT/fake_out" \
    --wav "$OUT/fake_out/run.wav" --input "60+60:left,150+40:right" | tail -1
python3 "$ROOT/tools/host_tests/check_fake_run.py" "$OUT/fake_out"
for game in pacman invaders rallyx dkong galaga digdug mspacman mrdo btime qbert asteroid tempest robotron defender qix starwars; do
    "$OUT/arcade_host" $game "$OUT/fixtures" > /dev/null 2>&1 && { echo "runner accepted a folder without $game ROMs"; exit 1; }
done
echo "missing ROMs reported: ok"
"$OUT/arcade_host" --check-table

echo "== the web page's ROM loader (web/roms/loader.js) against a fake board"
g++ $FLAGS -O2 -o "$OUT/fake_board" "$ROOT/tools/host_tests/fake_board.cpp" \
    "$ROOT/core/rom_store.cpp" "$ROOT/core/usb_import.cpp" "$LIB_O2"
if command -v node > /dev/null 2>&1; then
    "$OUT/arcade_host" --list-json > "$OUT/games.json"
    node --check "$ROOT/web/roms/loader.js"
    # Real MAME sets in roms/ (local runs only, git-ignored) are used when present.
    node "$ROOT/tools/host_tests/loader_test.mjs" --board "$OUT/fake_board" --games "$OUT/games.json" \
        --fake "$OUT/fake/pacman.zip" --roms "$ROOT/roms"
else
    echo "node not found - skipping the loader test"
fi

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

echo "== 8080 tests"
# 8080PRE / 8080EXM (Ian Bartholomew's 8080 ports of the exercisers), TST8080
# (Microcosm Associates) and CPUTEST (SuperSoft), from a public emulator
# repository.
fetch80() {
    if [ ! -s "$I80/$1" ]; then
        curl -sSfL -o "$I80/$1" "https://raw.githubusercontent.com/superzazu/8080/master/cpu_tests/$1"
    fi
    echo "$2  $I80/$1" | sha256sum -c --quiet -
}
fetch80 8080PRE.COM 18eb3c79cba42c0718f160be6a1853cb64cdce7aa47d65780189a57bdd98c4e0
fetch80 TST8080.COM 9561c6fb6c99efe3de00eb77e4044fd102151058b39ac2d7bce10483838a08e7
fetch80 CPUTEST.COM e61a9a75348c774486c2207080ea4effbf6c2367fdace31b0731081a4144030b
fetch80 8080EXM.COM 6e3286e11bb1a8f47b8ee1280b4a067be813193363e3223c99b0d21912f44aeb
# Prints the result line; the whole output when the test fails.
run80() {
    if "$@" > "$OUT/i8080.log" 2>&1; then tail -1 "$OUT/i8080.log"; else cat "$OUT/i8080.log"; exit 1; fi
}
for t in "$OUT/i8080_test" "$OUT/i8080_test_san"; do
    run80 "$t" "$I80/8080PRE.COM" --expect "Preliminary tests complete"
    run80 "$t" "$I80/TST8080.COM" --expect "CPU IS OPERATIONAL"
    run80 "$t" "$I80/CPUTEST.COM" --expect "CPU TESTS OK"
done
"$OUT/i8080_test_san" "$I80/8080EXM.COM" --bench 300
if [ $QUICK = 0 ]; then
    "$OUT/i8080_test" "$I80/8080EXM.COM" --expect "Tests complete"
fi

echo "== 6502 tests"
# Klaus Dormann's 6502 functional test (GPL) and the decimal mode test
# (Bruce Clark's, in the same repository), pinned by SHA-256.
K65=https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master
fetch65() {
    if [ ! -s "$M65/$2" ]; then
        curl -sSfL -o "$M65/$2" "$K65/$1"
    fi
    echo "$3  $M65/$2" | sha256sum -c --quiet -
}
fetch65 bin_files/6502_functional_test.bin 6502_functional_test.bin \
    fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
fetch65 6502_decimal_test.a65 6502_decimal_test.a65 \
    dfbe4b907c5821d47d9f7f74eeb51197cf4b9f5260375bc1c6c48c008220e1a0
DEC_AT=$(python3 "$ROOT/tools/host_tests/asm6502.py" "$M65/6502_decimal_test.a65" "$M65/decimal.bin" \
    chk_n=1 chk_v=1 chk_z=1)
for t in "$OUT/m6502_test" "$OUT/m6502_test_san"; do
    # 3469: the success trap of this version of the functional test
    "$t" functional "$M65/6502_functional_test.bin" 3469
    "$t" decimal "$M65/decimal.bin" "$DEC_AT"
done

echo "== 8088 tests"
# SingleStepTests 8088 (MIT, Daniel Balsom), pinned commit and SHA-256s in
# fetch_i8088_tests.py / i8088_tests.sha256.
python3 "$ROOT/tools/host_tests/fetch_i8088_tests.py" "$I88"
for t in "$OUT/i8088_test" "$OUT/i8088_test_san"; do
    if python3 "$ROOT/tools/host_tests/fetch_i8088_tests.py" "$I88" --cat | "$t" - > "$OUT/i8088.log" 2>&1
    then tail -1 "$OUT/i8088.log"; else cat "$OUT/i8088.log"; exit 1; fi
done

echo "== benchmark"
"$OUT/zex_test" "$ZEX/zexdoc.com" --bench 3000
"$OUT/i8080_test" "$I80/8080EXM.COM" --bench 2000
"$OUT/m6502_test" functional "$M65/6502_functional_test.bin" 3469 --bench 96
"$OUT/i8088_test" --bench 1000
