#!/usr/bin/env bash
# Build a generic LVGL → web shell for an arbitrary LVGL project.
#
# Usage:
#   ./scripts/build_generic_web.sh <project-dir> [width height] \
#       [--preload "src@/dest;..."] [--include "dir1;dir2"] [--out <dir>]
#
#   <project-dir>  directory containing your LVGL project's ui sources (must
#                  define `extern "C" void ui_init(void)`)
#   width height   display resolution (default 1280 720)
#   --preload      "src@/dest;..." files embedded into the wasm filesystem
#                  (images/fonts/audio your ui_init loads at runtime)
#   --include      extra include dirs your project needs
#   --out <dir>    where to copy index.* (default: <project-dir>/dist-web)
#
# Requires an emsdk install. Tested with 3.1.73 (same as CI).
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

PROJECT="${1:?usage: build_generic_web.sh <project-dir> [w h] [opts]}"; shift
W="${1:-1280}"; H="${2:-720}"
[ $# -ge 2 ] && shift 2
PRELOAD=""; INCLUDES=""; OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --preload) PRELOAD="$2"; shift 2;;
        --include) INCLUDES="$2"; shift 2;;
        --out)     OUT="$2";     shift 2;;
        *) echo "unknown option: $1" >&2; exit 1;;
    esac
done
[ -d "$PROJECT" ] || { echo "project dir not found: $PROJECT" >&2; exit 1; }
PROJECT="$(cd "$PROJECT" && pwd)"

# Locate emsdk (env EMSDK > ~/emsdk > /opt/emsdk).
EMSDK="${EMSDK:-}"
[ -z "$EMSDK" ] && [ -d "$HOME/emsdk" ] && EMSDK="$HOME/emsdk"
[ -z "$EMSDK" ] && [ -d /opt/emsdk ] && EMSDK=/opt/emsdk
[ -z "$EMSDK" ] && { echo "emsdk not found (set EMSDK=/path/to/emsdk)" >&2; exit 1; }
export PATH="$EMSDK/upstream/emscripten:$EMSDK/node/$(ls "$EMSDK/node" | head -1)/bin:$PATH"

NAME="$(basename "$PROJECT")"
BUILD="build-generic-$NAME"
OUT="${OUT:-$PROJECT/dist-web}"

echo "==> $NAME  ${W}x${H}  (build dir: $BUILD)"
emcmake cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DEMU_GENERIC_WEB=ON \
    -DEMU_LCD_W="$W" -DEMU_LCD_H="$H" \
    -DEMU_APP_UI_DIR="$PROJECT" \
    -DEMU_APP_INCLUDE_DIRS="$INCLUDES" \
    -DEMU_PRELOAD="$PRELOAD" \
    >/dev/null
cmake --build "$BUILD"
mkdir -p "$OUT"
cp "$BUILD"/index.html "$BUILD"/index.js "$BUILD"/index.wasm "$BUILD"/index.data "$BUILD"/coi-serviceworker.js "$OUT/" 2>/dev/null || true
echo "==> done. Open:"
echo "    cd $OUT && python3 -m http.server 8123"
echo "    → http://localhost:8123/"
