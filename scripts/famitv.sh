#!/usr/bin/env bash
# Launch the RF-TV NES player: an NES ROM decoded through the analog RF chain.
# Usage: scripts/famitv.sh [path/to/rom.nes] [extra famitv_play flags...]
# With no ROM it runs the bundled free Blade Buster demo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="$ROOT/../third_party/nestopia/libretro/nestopia_libretro.dylib"
DEFAULT_ROM="$ROOT/../roms/bladebuster/Blade Buster by HLC! Project (20120301) (PD).nes"

ROM="${1:-$DEFAULT_ROM}"
shift || true  # drop the ROM arg; the rest pass through to famitv_play

if [ ! -f "$CORE" ]; then
  echo "Nestopia core not found: $CORE" >&2
  echo "Build it:  make -C \"$ROOT/../third_party/nestopia/libretro\" -j4" >&2
  exit 1
fi
if [ ! -f "$ROM" ]; then
  echo "ROM not found: $ROM" >&2
  exit 1
fi
if [ ! -x "$ROOT/build/famitv_play" ]; then
  echo "Building famitv_play..." >&2
  # famitv_play needs SDL2 + libretro.h but NOT libhackrf; tools-only keeps a
  # missing libhackrf a benign skip of famidec instead of a fatal configure error.
  cmake -B "$ROOT/build" -S "$ROOT" -DCMAKE_BUILD_TYPE=Release -DFAMITV_TOOLS_ONLY=ON >/dev/null
  cmake --build "$ROOT/build" -j >/dev/null
fi
if [ ! -x "$ROOT/build/famitv_play" ]; then
  echo "famitv_play was not built: it requires SDL2 and the Nestopia libretro.h" >&2
  echo "  (expected at $ROOT/../third_party/nestopia/libretro/libretro-common/include/libretro.h)." >&2
  echo "Install SDL2 and make sure that header is present, then re-run this script." >&2
  exit 1
fi
exec "$ROOT/build/famitv_play" "$CORE" "$ROM" "$@"
