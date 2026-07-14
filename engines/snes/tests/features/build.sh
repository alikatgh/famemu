#!/usr/bin/env bash
# Assemble the feature/coprocessor test ROMs -> ../data/ (ca65/ld65), then
# patch real header checksums so both famemu's and snes9x's map scoring pick
# the intended layout.
set -euo pipefail
cd "$(dirname "$0")"

build() {  # src cfg out hdr_offset
    ca65 --cpu 65816 "$1" -o /tmp/feat_$$.o
    ld65 -C "$2" /tmp/feat_$$.o -o "../data/$3"
    rm -f /tmp/feat_$$.o
    python3 - "../data/$3" "$4" <<'EOF'
import sys
p, hdr = sys.argv[1], int(sys.argv[2], 16)
d = bytearray(open(p, 'rb').read())
d[hdr+0x1C:hdr+0x20] = b'\xFF\xFF\x00\x00'
s = sum(d) & 0xFFFF
d[hdr+0x1E] = s & 0xFF
d[hdr+0x1F] = s >> 8
d[hdr+0x1C] = ~d[hdr+0x1E] & 0xFF
d[hdr+0x1D] = ~d[hdr+0x1F] & 0xFF
open(p, 'wb').write(d)
EOF
    echo "built ../data/$3 ($(wc -c < ../data/$3 | tr -d ' ') bytes)"
}

build featppu.s ../mode7/lorom32.cfg featppu.sfc 7FC0
build hirom.s hirom64.cfg hirom.sfc FFC0
build sa1.s sa1rom.cfg sa1.sfc 7FC0
build sfx.s sfxrom.cfg sfx.sfc 7FC0
build sprlimit.s ../mode7/lorom32.cfg sprlimit.sfc 7FC0
