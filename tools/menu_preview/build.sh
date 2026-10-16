#!/bin/sh
# Builds and runs the menu preview: tools/menu_preview/build.sh <LovyanGFX dir> [out dir]
# (LovyanGFX: the pinned version, e.g. external/LovyanGFX after a firmware
# build, or a clone of lovyan03/LovyanGFX at the tag in tools/fetch_deps.py).
# Writes <out>/<w>x<h>_<page>.ppm (and .png when Python has Pillow).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LGFX=${1:?LovyanGFX directory}
OUT=${2:-$ROOT/build/menu_preview}
mkdir -p "$OUT/obj"
L="$LGFX/src"
# The emulators' sources, for the game table (as tools/host/build.sh lists them)
eval "$(sed -n '/^EMU="/,/"$/p' "$ROOT/tools/host/build.sh")"
# LovyanGFX's C parts (fonts, image decoders), once
for c in $(find "$L/lgfx/utility" "$L/lgfx/Fonts" -name '*.c'); do
    o="$OUT/obj/$(echo "$c" | sed 's|.*/src/||; s|/|_|g; s|\.c$|.o|')"
    [ -f "$o" ] || gcc -O1 -c "$c" -o "$o" -I"$L"
done
[ -f "$OUT/obj/lgfx_v1.o" ] || g++ -std=c++17 -O1 -DLGFX_LINUX_FB -I"$L" -c "$L/lgfx/v1/lgfx_v1.cpp" -o "$OUT/obj/lgfx_v1.o"
g++ -std=c++17 -O1 -Wall -DLGFX_LINUX_FB -I"$L" -I"$ROOT/main" -I"$ROOT" -o "$OUT/menu_preview" \
    "$ROOT/tools/menu_preview/preview.cpp" "$ROOT/main/ui.cpp" "$ROOT/main/menu_model.cpp" $EMU \
    "$OUT"/obj/*.o -lpthread
"$OUT/menu_preview" "$OUT"
python3 - "$OUT" <<'EOF' || true
import sys, glob, os
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], '*.ppm'))):
    Image.open(p).save(p[:-4] + '.png')
print('PNGs in', sys.argv[1])
EOF
