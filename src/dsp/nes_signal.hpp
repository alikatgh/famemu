#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace famidec {

// The REAL NES composite signal. The PPU never outputs RGB — it emits a
// composite voltage directly, and each of its 64 colors *is* a specific
// chroma phase + luma level. This is Bisqwit's documented model (NESdev wiki,
// public domain): the voltage the NES emits for a 6-bit color at one of 12
// sub-phases of the color subcarrier. Feeding this to the RF decoder makes NES
// color and artifacts *emerge* from the real signal instead of being encoded.
inline float nes_ntsc_signal(int color6, int phase) {
    int color = color6 & 0x0F;
    int level = (color < 0x0E) ? ((color6 >> 4) & 0x03) : 1;
    // Voltage levels relative to sync (Bisqwit): [0..3] low, [4..7] high.
    static const float levels[8] = {0.350f, 0.518f, 0.962f, 1.550f,
                                    1.094f, 1.506f, 1.962f, 1.962f};
    float lo = levels[0 + level];
    float hi = levels[4 + level];
    if (color == 0) lo = hi;    // color $x0: emit only the high level
    if (color > 12) hi = lo;    // colors $xD..$xF: only the low level (black)
    bool in = ((color + phase) % 12) < 6;
    return in ? hi : lo;
}

// Standard 2C02 NES palette (RGB), used only to reverse-map an emulator's RGB
// output back to a NES color index (nearest match; the 64 colors are well
// separated so this is robust across emulator palettes).
class NesPalette {
public:
    NesPalette() {
        static const uint32_t kPal[64] = {
            0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
            0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0x000000,0x000000,0x000000,
            0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
            0x6B6D00,0x388700,0x0C9300,0x008F32,0x007C8D,0x000000,0x000000,0x000000,
            0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
            0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0x000000,0x000000,
            0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
            0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0x000000,0x000000,
        };
        for (int i = 0; i < 64; ++i) {
            pr_[i] = (kPal[i] >> 16) & 0xff;
            pg_[i] = (kPal[i] >> 8) & 0xff;
            pb_[i] = kPal[i] & 0xff;
        }
    }

    int index_of(uint8_t r, uint8_t g, uint8_t b) const {
        // Pure black is a 3-way tie between indices 13/14/15 (all 0x000000 in
        // kPal); the plain nearest-match loop below picks the lowest index
        // ($0D), which nes_ntsc_signal renders as 0.350V (blacker-than-black,
        // -11.6 IRE) instead of the safe reference black. Force $0F, whose
        // signal is a constant 0.518V regardless of phase.
        if (r == 0 && g == 0 && b == 0) return 0x0F;
        uint32_t key = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        int best = 0;
        long bestd = 1L << 60;
        for (int i = 0; i < 64; ++i) {
            long dr = int(r) - pr_[i], dg = int(g) - pg_[i], db = int(b) - pb_[i];
            long d = dr * dr + dg * dg + db * db;
            if (d < bestd) { bestd = d; best = i; }
        }
        cache_[key] = best;
        return best;
    }

private:
    int pr_[64], pg_[64], pb_[64];
    mutable std::unordered_map<uint32_t, int> cache_;
};

}  // namespace famidec
