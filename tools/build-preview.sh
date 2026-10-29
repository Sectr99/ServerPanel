#!/bin/sh
# Builds and runs the preview renderer: writes preview.png in the project root.
# Usage: tools/build-preview.sh <path-to-SDL2-x86_64-w64-mingw32-folder> [samples]
# With "samples": renders every scenario (normal, hot, idle, offline, night) to samples/<scenario>/ instead.
set -e
SDL=${1:?"pass the SDL2 mingw folder (from SDL2-devel-*-mingw.zip)"}
ZIG=${ZIG:-zig}
cd "$(dirname "$0")/.."
LGFX=.pio/libdeps/p4/LovyanGFX/src
OUT=.pio/preview
mkdir -p "$OUT"
"$ZIG" c++ -O2 -target x86_64-windows-gnu -DLGFX_SDL -w $PREVIEW_FLAGS \
  -I"$LGFX" -I"$SDL/include" -I"$SDL/include/SDL2" \
  tools/preview.cpp src/ui.cpp $(find "$LGFX/lgfx" -name '*.cpp' -o -name '*.c' | grep -v '/platforms/[^s]') \
  "$SDL/lib/libSDL2.dll.a" -o "$OUT/preview.exe"
cp "$SDL/bin/SDL2.dll" "$OUT/"
if [ "$2" = samples ]; then
  for sc in normal hot idle offline night; do mkdir -p "samples/$sc" && "$OUT/preview.exe" $sc "samples/$sc/"; done
else
  "$OUT/preview.exe"
fi
