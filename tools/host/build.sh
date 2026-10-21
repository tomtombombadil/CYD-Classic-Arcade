#!/bin/sh
# Builds the headless runner: tools/host/build.sh [out dir] [emulator library]
# (default build/host; the host tests pass the -O2 library they built, so
# emu/ isn't compiled again)
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/host}
mkdir -p "$OUT"
EMU="$ROOT/emu/cpu/z80.cpp $ROOT/emu/cpu/i8080.cpp $ROOT/emu/cpu/i8035.cpp $ROOT/emu/cpu/m6502.cpp \
     $ROOT/emu/cpu/i8088.cpp $ROOT/emu/rom/crc32.cpp $ROOT/emu/rom/zip.cpp $ROOT/emu/rom/romset.cpp \
     $ROOT/emu/util/inflate.cpp $ROOT/emu/sound/namco_wsg.cpp $ROOT/emu/sound/invaders_sound.cpp \
     $ROOT/emu/sound/rallyx_bang.cpp $ROOT/emu/sound/dkong_sound.cpp $ROOT/emu/sound/namco54.cpp \
     $ROOT/emu/sound/samples.cpp $ROOT/emu/sound/sn76489.cpp $ROOT/emu/machines/pacman.cpp \
     $ROOT/emu/machines/invaders.cpp $ROOT/emu/machines/rallyx.cpp $ROOT/emu/machines/dkong.cpp \
     $ROOT/emu/machines/namco_io.cpp $ROOT/emu/machines/starfield05.cpp $ROOT/emu/cpu/m6809.cpp \
     $ROOT/emu/cpu/m6800.cpp \
     $ROOT/emu/machines/namco_galaga_common.cpp $ROOT/emu/machines/galaga.cpp $ROOT/emu/machines/digdug.cpp \
     $ROOT/emu/machines/mspacman.cpp $ROOT/emu/machines/mrdo.cpp $ROOT/emu/games.cpp \
     $ROOT/emu/sound/ay8910.cpp $ROOT/emu/machines/btime.cpp $ROOT/emu/sound/qbert_sound.cpp \
     $ROOT/emu/machines/qbert.cpp $ROOT/emu/sound/pokey.cpp $ROOT/emu/sound/asteroids_sound.cpp \
     $ROOT/emu/machines/vector_display.cpp $ROOT/emu/machines/atari_dvg.cpp $ROOT/emu/machines/asteroids.cpp \
     $ROOT/emu/machines/atari_avg.cpp $ROOT/emu/machines/atari_mathbox.cpp $ROOT/emu/machines/tempest.cpp \
     $ROOT/emu/sound/bzone_sound.cpp $ROOT/emu/machines/bzone.cpp \
     $ROOT/emu/machines/pia6821.cpp \
     $ROOT/emu/machines/williams_blitter.cpp $ROOT/emu/sound/williams_sound.cpp \
     $ROOT/emu/machines/williams.cpp $ROOT/emu/sound/qix_sound.cpp $ROOT/emu/machines/qix.cpp \
     $ROOT/emu/sound/tms5220.cpp $ROOT/emu/machines/riot6532.cpp $ROOT/emu/machines/starwars_mathbox.cpp \
     $ROOT/emu/machines/starwars.cpp $ROOT/emu/machines/missile.cpp \
     $ROOT/emu/sound/zaxxon_sound.cpp $ROOT/emu/machines/zaxxon.cpp \
     $ROOT/emu/machines/galaxian_video.cpp $ROOT/emu/sound/galaxian_sound.cpp $ROOT/emu/machines/galaxian.cpp $ROOT/emu/machines/amidar.cpp \
     $ROOT/emu/machines/xevious.cpp"
[ -n "$2" ] && EMU="$2"
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/arcade_host" \
    "$ROOT/tools/host/arcade_host.cpp" "$ROOT/tools/host/writers.cpp" $EMU
echo "built $OUT/arcade_host"
