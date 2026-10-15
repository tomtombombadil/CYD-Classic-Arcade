#!/bin/sh
# Builds the headless runner: tools/host/build.sh [out dir] (default build/host)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/host}
mkdir -p "$OUT"
EMU="$ROOT/emu/cpu/z80.cpp $ROOT/emu/cpu/i8080.cpp $ROOT/emu/cpu/i8035.cpp $ROOT/emu/cpu/m6809.cpp $ROOT/emu/rom/crc32.cpp $ROOT/emu/rom/zip.cpp \
     $ROOT/emu/rom/romset.cpp $ROOT/emu/util/inflate.cpp $ROOT/emu/sound/namco_wsg.cpp \
     $ROOT/emu/sound/invaders_sound.cpp $ROOT/emu/sound/rallyx_bang.cpp $ROOT/emu/sound/dkong_sound.cpp \
     $ROOT/emu/sound/namco54.cpp $ROOT/emu/sound/samples.cpp \
     $ROOT/emu/machines/pacman.cpp $ROOT/emu/machines/invaders.cpp $ROOT/emu/machines/rallyx.cpp $ROOT/emu/machines/dkong.cpp \
     $ROOT/emu/machines/namco_io.cpp $ROOT/emu/machines/starfield05.cpp $ROOT/emu/machines/namco_galaga_common.cpp \
     $ROOT/emu/machines/galaga.cpp $ROOT/emu/machines/digdug.cpp $ROOT/emu/games.cpp"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/arcade_host" \
    "$ROOT/tools/host/arcade_host.cpp" "$ROOT/tools/host/writers.cpp" $EMU
echo "built $OUT/arcade_host"
