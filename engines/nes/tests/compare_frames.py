#!/usr/bin/env python3
"""Gate-5 parity check: compare two 256x240 PPM frames (famemu core vs the
Nestopia reference) by mapping every pixel to its nearest canonical NES color
index — the two emulators use different RGB palettes, so index space is the
common ground (the 64 colors are well separated, memory-safe to nearest-match).

usage: compare_frames.py a.ppm b.ppm [--threshold 0.995]
exit 0 when the match ratio >= threshold.
"""
import sys

KPAL = [
    0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
    0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0x000000,0x000000,0x000000,
    0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
    0x6B6D00,0x388700,0x0C9300,0x008F32,0x007C8D,0x000000,0x000000,0x000000,
    0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
    0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0x000000,0x000000,
    0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
    0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0x000000,0x000000,
]
PR = [(c >> 16) & 0xFF for c in KPAL]
PG = [(c >> 8) & 0xFF for c in KPAL]
PB = [c & 0xFF for c in KPAL]


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6 header: magic, dims, maxval, then raw
    parts, i = [], 0
    while len(parts) < 4:
        j = data.index(b"\n", i)
        line = data[i:j]
        i = j + 1
        if line.startswith(b"#"):
            continue
        parts.extend(line.split())
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[i:i + w * h * 3]


CACHE = {}


def idx(r, g, b):
    key = (r << 16) | (g << 8) | b
    v = CACHE.get(key)
    if v is None:
        v = min(range(64),
                key=lambda k: (r - PR[k]) ** 2 + (g - PG[k]) ** 2 + (b - PB[k]) ** 2)
        CACHE[key] = v
    return v


def main():
    a_path, b_path = sys.argv[1], sys.argv[2]
    threshold = 0.995
    if "--threshold" in sys.argv:
        threshold = float(sys.argv[sys.argv.index("--threshold") + 1])
    wa, ha, a = read_ppm(a_path)
    wb, hb, b = read_ppm(b_path)
    if (wa, ha) != (wb, hb):
        print(f"size mismatch: {wa}x{ha} vs {wb}x{hb}")
        return 1
    total = wa * ha
    bad = 0
    worst = {}
    for p in range(total):
        ia = idx(a[p * 3], a[p * 3 + 1], a[p * 3 + 2])
        ib = idx(b[p * 3], b[p * 3 + 1], b[p * 3 + 2])
        if ia != ib:
            bad += 1
            worst[(ia, ib)] = worst.get((ia, ib), 0) + 1
    ratio = (total - bad) / total
    print(f"match: {total - bad}/{total} = {ratio:.4%}  (threshold {threshold:.2%})")
    if bad:
        top = sorted(worst.items(), key=lambda kv: -kv[1])[:5]
        for (ia, ib), n in top:
            print(f"  index {ia:02X} vs {ib:02X}: {n} px")
    return 0 if ratio >= threshold else 1


if __name__ == "__main__":
    sys.exit(main())
