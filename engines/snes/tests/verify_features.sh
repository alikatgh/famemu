#!/usr/bin/env bash
# Lockstep-verify the general-purpose SNES engine features against snes9x:
# run each feature/coprocessor test ROM through both cores and require
# (near-)identical frames. Needs the parent famicom project layout
# (game/dump_ppm + third_party/snes9x); skips with success if absent so the
# suite still runs standalone.
#
# Tolerance matches kora/snes/verify_famemu.sh: channels within 7/255,
# stragglers < 0.2% of the frame. dump_ppm gives snes9x 24 internal boot
# frames; famemu gets the same 24 up front. Test ROMs sample mid-window
# (case switches every 32/64 frames), so up to ~15 frames of init skew
# between the cores cannot flip a sample into the wrong window.
set -euo pipefail
cd "$(dirname "$0")"

ROOT=../../..                       # famicom-rf-hackrf-decoder/
CORE="$ROOT/../third_party/snes9x/libretro/snes9x_libretro.dylib"
DUMP="$ROOT/../game/dump_ppm"
SNES_DUMP="$ROOT/build/snes_dump"
DATA=data
OUT="${TMPDIR:-/tmp}/snes_verify_features"
mkdir -p "$OUT"

if [ ! -f "$CORE" ] || [ ! -x "$DUMP" ]; then
    echo "SKIP: snes9x core or dump_ppm not present (parent project layout needed)"
    exit 0
fi
[ -x "$SNES_DUMP" ] || { echo "build snes_dump first"; exit 2; }

fail=0
check_frame() {  # rom name frame
    local rom="$1" name="$2" n="$3"
    rm -f "$OUT/${name}_${n}_fam"*.ppm "$OUT/${name}_${n}_s9x.ppm"
    FAMEMU_RAW=1 "$DUMP" "$CORE" "$DATA/$rom" 0 "$OUT/${name}_${n}_s9x.ppm" 0 0 "0:$n" >/dev/null 2>&1
    "$SNES_DUMP" "$DATA/$rom" "$OUT/${name}_${n}_fam" "0:$((24 + n))" >/dev/null
    python3 - "$name" "$n" "$OUT" <<'EOF' || fail=1
import glob, sys
from PIL import Image
import numpy as np
name, n, out = sys.argv[1], sys.argv[2], sys.argv[3]
fam = sorted(glob.glob(f'{out}/{name}_{n}_fam_*.ppm'))[-1]
a = np.asarray(Image.open(fam).convert('RGB'), dtype=np.int16)
b = np.asarray(Image.open(f'{out}/{name}_{n}_s9x.ppm').convert('RGB'), dtype=np.int16)
frac = (np.abs(a - b).max(axis=2) > 7).mean()
status = 'OK ' if frac < 0.002 else 'FAIL'
print(f'{status} {name} f={n}: {frac*100:.3f}% pixels beyond tolerance')
sys.exit(0 if frac < 0.002 else 1)
EOF
}

# PPU features: ten 64-frame cases, sampled mid-window.
for c in 0 1 2 3 4 5 6 7 8 9; do
    check_frame featppu.sfc featppu $((64 * c + 40))
done

# HiROM + CPU registers: eight 32-frame result slots.
for s in 0 1 2 3 4 5 6 7; do
    check_frame hirom.sfc hirom $((32 * s + 16))
done

# SA-1: eight 32-frame result slots.
for s in 0 1 2 3 4 5 6 7; do
    check_frame sa1.sfc sa1 $((32 * s + 16))
done

# SuperFX: the GSU-plotted bitmap, well after STOP.
check_frame sfx.sfc sfx 120

# OBJ range/time limit + drop-order probe (80 slivers on one line).
check_frame sprlimit.sfc sprlimit 30

exit $fail
