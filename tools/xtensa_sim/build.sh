#!/bin/sh
# The Xtensa simulator (Claude's side, Linux): tools/xtensa_sim/build.sh OUT [extra compiler flags]
#   OUT/xsim      the simulator (host)
#   OUT/game.elf  emu/ + xdrv.cpp built for the ESP32-S3 with the firmware's
#                 flags (ESP-IDF's -mlongcalls -fno-builtin-memcpy ...,
#                 components/arcade_core's -O2 -freorder-blocks-and-partition)
# Objects are kept in OUT/obj and rebuilt when their source or a header is
# newer. The toolchain: $XTENSA_TOOLCHAIN (default /tmp/claude-0/xt/xtensa-esp-elf,
# Espressif's xtensa-esp-elf GCC 14.2). Extra flags, e.g. -ffp-contract=off:
# the board fuses a * b + c into MADD.S, so without it some frames' audio CRCs
# differ from the PC's (the pictures never do).
#
# Then: arcade_host <game> <zip> --frames N --input SCRIPT --dump DIR
#       OUT/xsim OUT/game.elf DIR <game> N [--skip K] [--hash F] [--prof F] [--param ...]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$1
[ -n "$OUT" ] || { echo "usage: $0 OUT [flags]"; exit 2; }
shift
XT=${XTENSA_TOOLCHAIN:-/tmp/claude-0/xt/xtensa-esp-elf}
CC="$XT/bin/xtensa-esp-elf-g++ -mdynconfig=$XT/lib/xtensa_esp32s3.so"
FL="-mlongcalls -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-bzero -fno-builtin-stpcpy -fno-builtin-strncpy \
    -std=gnu++2b -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fstrict-volatile-bitfields \
    -Wno-unused-parameter -freorder-blocks-and-partition $*"
mkdir -p "$OUT/obj"
g++ -std=c++17 -O2 -ffp-contract=off -Wall -o "$OUT/xsim" "$ROOT/tools/xtensa_sim/xsim.cpp"
OBJS=""
for f in $(cd "$ROOT" && find emu -name '*.cpp' | sort); do
    o="$OUT/obj/$(echo "$f" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    OBJS="$OBJS $o"
    need=0
    [ -f "$o" ] || need=1
    if [ $need = 0 ]; then
        if [ "$ROOT/$f" -nt "$o" ]; then need=1; else
            for h in $(cd "$ROOT" && g++ -std=c++17 -MM "$f" | tr ' \\' '\n\n' | grep '\.h$'); do
                [ "$ROOT/$h" -nt "$o" ] && need=1 && break
            done
        fi
    fi
    [ $need = 1 ] && $CC $FL -c "$ROOT/$f" -o "$o"
done
$CC $FL -I"$ROOT" -c "$ROOT/tools/xtensa_sim/xdrv.cpp" -o "$OUT/obj/xdrv.o"
$XT/bin/xtensa-esp-elf-gcc -mdynconfig=$XT/lib/xtensa_esp32s3.so -c "$ROOT/tools/xtensa_sim/crt0.S" -o "$OUT/obj/crt0.o"
$CC -nostdlib -nostartfiles -T "$ROOT/tools/xtensa_sim/link.ld" -o "$OUT/game.elf" "$OUT/obj/crt0.o" "$OUT/obj/xdrv.o" $OBJS \
    -L"$XT/xtensa-esp-elf/lib/esp32s3" -L"$XT/lib/gcc/xtensa-esp-elf/14.2.0/esp32s3" \
    -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -Wl,--gc-sections
echo "built $OUT/xsim $OUT/game.elf"
