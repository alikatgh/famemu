// famemu SNES engine — S-PPU implementation. See sppu.hpp.
#include "sppu.hpp"

namespace famemu::snes {

void SPpu::write(uint8_t reg, uint8_t v) {
    switch (reg) {
        case 0x00: inidisp_ = v; break;
        case 0x01: obsel_ = v; break;
        case 0x02: oamadd_ = static_cast<uint16_t>((oamadd_ & 0x0200) | (v << 1)); break;
        case 0x03: oamadd_ = static_cast<uint16_t>(((v & 1) << 9) | (oamadd_ & 0x1FE)); break;
        case 0x04:
            if (oamadd_ < 544) oam_[oamadd_] = v;
            oamadd_ = (oamadd_ + 1) % 1024;  // wraps within table in practice
            break;
        case 0x05: bgmode_ = v; break;
        case 0x07: bg1sc_ = v; break;
        case 0x09: bg3sc_ = v; break;
        case 0x0B: bg12nba_ = v; break;
        case 0x0C: bg34nba_ = v; break;
        case 0x0D: bg1hofs_ = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
                   scroll_latch_ = v; break;   // write-twice: new<<8|prev, simplified
        case 0x0E: bg1vofs_ = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
                   scroll_latch_ = v; break;
        case 0x11: bg3hofs_ = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
                   scroll_latch_ = v; break;
        case 0x12: bg3vofs_ = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
                   scroll_latch_ = v; break;
        case 0x15: vmain_ = v; break;
        case 0x16: vmadd_ = (vmadd_ & 0xFF00) | v; break;
        case 0x17: vmadd_ = static_cast<uint16_t>((v << 8) | (vmadd_ & 0x00FF)); break;
        case 0x18:  // VMDATAL
            vram_[(vmadd_ * 2) & 0xFFFF] = v;
            if (!(vmain_ & 0x80)) vmadd_ += vram_step();
            break;
        case 0x19:  // VMDATAH
            vram_[(vmadd_ * 2 + 1) & 0xFFFF] = v;
            if (vmain_ & 0x80) vmadd_ += vram_step();
            break;
        case 0x21: cgadd_ = v; cg_latch_ = false; break;
        case 0x22:
            if (!cg_latch_) { cg_low_ = v; cg_latch_ = true; }
            else {
                cgram_[cgadd_ * 2] = cg_low_;
                cgram_[cgadd_ * 2 + 1] = v;
                ++cgadd_;
                cg_latch_ = false;
            }
            break;
        case 0x2C: tm_ = v; break;
        case 0x31: cgadsub_ = v; break;
        case 0x32:  // COLDATA: bit5/6/7 select channels, low 5 bits intensity
            if (v & 0x20) coldata_r_ = v & 0x1F;
            if (v & 0x40) coldata_g_ = v & 0x1F;
            if (v & 0x80) coldata_b_ = v & 0x1F;
            break;
        default: break;
    }
}

uint8_t SPpu::read(uint8_t) { return 0; }

void SPpu::fetch_bg_pixel(int bg, int x, int y, Pixel& out) {
    out.color_idx = 0;
    const bool is_bg1 = (bg == 0);
    const uint8_t sc = is_bg1 ? bg1sc_ : bg3sc_;
    // Mode 0: every BG is 2bpp (BG1 palette base 0, BG3 base 64).
    // Mode 1: BG1 4bpp, BG3 2bpp (KORA's mode). Others: treated as mode 1.
    const int mode = bgmode_ & 7;
    const int bpp = (mode == 0) ? 2 : (is_bg1 ? 4 : 2);
    // $210B/$210C: LOW nibble = BG1/BG3, high nibble = BG2/BG4.
    const uint16_t tile_base =
        static_cast<uint16_t>(((is_bg1 ? bg12nba_ : bg34nba_) & 0x0F) << 12);
    const int sx = (x + (is_bg1 ? bg1hofs_ : bg3hofs_)) & 0x3FF;
    const int sy = (y + (is_bg1 ? bg1vofs_ : bg3vofs_)) & 0x3FF;

    // Tilemap: base + 32x32 screens arranged by SC size bits.
    uint16_t map_base = static_cast<uint16_t>((sc >> 2) << 10);  // word addr
    int tx = (sx >> 3) & 31, ty = (sy >> 3) & 31;
    int screen = 0;
    const int sc_size = sc & 3;
    if ((sc_size & 1) && (sx & 0x100)) screen += 1;              // 64-wide
    if ((sc_size & 2) && (sy & 0x100)) screen += (sc_size & 1) ? 2 : 1;
    const uint16_t entry =
        vram_word(static_cast<uint16_t>(map_base + screen * 0x400 + ty * 32 + tx));

    const int tile = entry & 0x3FF;
    const int pal = (entry >> 10) & 7;
    const bool xflip = entry & 0x4000, yflip = entry & 0x8000;
    out.prio = (entry & 0x2000) ? 1 : 0;

    int row = sy & 7;
    if (yflip) row = 7 - row;
    int col = sx & 7;
    if (xflip) col = 7 - col;

    // Planar tile data: 2bpp = 8 words/tile; 4bpp = 16 words/tile.
    const uint16_t taddr =
        static_cast<uint16_t>(tile_base + tile * (bpp == 2 ? 8 : 16) + row);
    uint8_t b0 = vram_[(taddr * 2) & 0xFFFF];
    uint8_t b1 = vram_[(taddr * 2 + 1) & 0xFFFF];
    int idx = ((b0 >> (7 - col)) & 1) | (((b1 >> (7 - col)) & 1) << 1);
    if (bpp == 4) {
        uint8_t b2 = vram_[((taddr + 8) * 2) & 0xFFFF];
        uint8_t b3 = vram_[((taddr + 8) * 2 + 1) & 0xFFFF];
        idx |= (((b2 >> (7 - col)) & 1) << 2) | (((b3 >> (7 - col)) & 1) << 3);
    }
    if (idx == 0) return;  // transparent

    // Palette mapping: 4bpp = 16-color palettes; 2bpp = 4-color palettes,
    // with mode-0's per-BG base offset (BG1 +0, BG3 +64).
    if (bpp == 4) {
        out.color_idx = static_cast<uint8_t>(pal * 16 + idx);
    } else {
        const int base = (mode == 0 && !is_bg1) ? 64 : 0;
        out.color_idx = static_cast<uint8_t>(base + pal * 4 + idx);
    }
    out.layer = is_bg1 ? kBG1 : kBG3;
}

bool SPpu::fetch_obj_pixel(int x, int y, Pixel& out) {
    // OBSEL: name base (bits 0-2), size mode (bits 5-7). KORA: 8x8 / 16x16.
    const uint16_t obj_base = static_cast<uint16_t>((obsel_ & 7) << 13);
    const int size_mode = (obsel_ >> 5) & 7;
    static const int kSmall[8] = {8, 8, 8, 16, 16, 32, 16, 16};
    static const int kLarge[8] = {16, 32, 64, 32, 64, 64, 32, 32};

    bool found = false;
    int best_prio = -1;
    for (int i = 0; i < 128; ++i) {
        const uint8_t* o = &oam_[i * 4];
        const uint8_t hi = oam_[512 + (i >> 2)];
        const int hi_shift = (i & 3) * 2;
        const int x9 = o[0] | (((hi >> hi_shift) & 1) << 8);
        const bool large = (hi >> hi_shift) & 2;
        const int size = large ? kLarge[size_mode] : kSmall[size_mode];
        int ox = (x9 >= 256) ? x9 - 512 : x9;  // sign-extend 9-bit
        int oy = o[1];
        int row = y - oy;
        if (row < 0 || row >= size) continue;
        int col = x - ox;
        if (col < 0 || col >= size) continue;

        const int tile = o[2] | ((o[3] & 1) << 8);
        const int pal = (o[3] >> 1) & 7;
        const int prio = (o[3] >> 4) & 3;
        if (o[3] & 0x40) col = size - 1 - col;   // X flip
        if (o[3] & 0x80) row = size - 1 - row;   // Y flip

        // 16x16+ sprites tile like 8x8 cells in a 16-wide name grid.
        const int trow = row >> 3, tcol = col >> 3;
        const int t = (tile + trow * 16 + tcol) & 0x1FF;
        const uint16_t taddr = static_cast<uint16_t>(obj_base + t * 16 + (row & 7));
        const int c = col & 7;
        uint8_t b0 = vram_[(taddr * 2) & 0xFFFF];
        uint8_t b1 = vram_[(taddr * 2 + 1) & 0xFFFF];
        uint8_t b2 = vram_[((taddr + 8) * 2) & 0xFFFF];
        uint8_t b3 = vram_[((taddr + 8) * 2 + 1) & 0xFFFF];
        int idx = ((b0 >> (7 - c)) & 1) | (((b1 >> (7 - c)) & 1) << 1) |
                  (((b2 >> (7 - c)) & 1) << 2) | (((b3 >> (7 - c)) & 1) << 3);
        if (idx == 0) continue;
        if (prio > best_prio) {
            best_prio = prio;
            out.color_idx = static_cast<uint8_t>(128 + pal * 16 + idx);
            out.layer = kOBJ;
            out.prio = static_cast<uint8_t>(prio);
            found = true;
        }
    }
    return found;
}

void SPpu::render_line(int y) {
    uint8_t* row = fb_ + static_cast<size_t>(y) * kWidth * 3;
    if (inidisp_ & 0x80) {  // force blank
        std::memset(row, 0, kWidth * 3);
        return;
    }
    const float bright = static_cast<float>(inidisp_ & 0x0F) / 15.0f;
    const bool bg3_prio_mode = bgmode_ & 0x08;

    for (int x = 0; x < kWidth; ++x) {
        // Gather layer pixels.
        Pixel bg1{0, kBACK, 0}, bg3{0, kBACK, 0}, obj{0, kBACK, 0};
        bool has_obj = false;
        if (tm_ & 0x01) fetch_bg_pixel(0, x, y, bg1);
        if (tm_ & 0x04) fetch_bg_pixel(2, x, y, bg3);
        if (tm_ & 0x10) has_obj = fetch_obj_pixel(x, y, obj);

        // Mode-1 priority resolution (no BG2; KORA's TM=$15).
        Pixel px{0, kBACK, 0};
        auto pick = [&](const Pixel& c) {
            if (c.color_idx || c.layer == kOBJ) px = c;
        };
        // lowest first, later picks override
        if (bg3.color_idx && !bg3.prio && !bg3_prio_mode) pick(bg3);
        if (has_obj && obj.prio == 0) pick(obj);
        if (bg3.color_idx && !bg3.prio && bg3_prio_mode) pick(bg3);
        if (has_obj && obj.prio == 1) pick(obj);
        if (bg1.color_idx && !bg1.prio) pick(bg1);
        if (has_obj && obj.prio == 2) pick(obj);
        if (bg1.color_idx && bg1.prio) pick(bg1);
        if (has_obj && obj.prio == 3) pick(obj);
        if (bg3.color_idx && bg3.prio && bg3_prio_mode) pick(bg3);

        uint16_t c15 = cg_color(px.color_idx);
        int r = (c15 & 0x1F), g = (c15 >> 5) & 0x1F, b = (c15 >> 10) & 0x1F;

        // Color math: fixed-color add/sub on selected layers (KORA: subtract
        // COLDATA from BG1 + backdrop for day/night).
        const uint8_t layer_bit =
            (px.layer == kBG1) ? 0x01 : (px.layer == kBG3) ? 0x04
            : (px.layer == kOBJ) ? 0x10 : 0x20;
        if (cgadsub_ & layer_bit) {
            if (cgadsub_ & 0x80) {  // subtract
                r -= coldata_r_; g -= coldata_g_; b -= coldata_b_;
            } else {
                r += coldata_r_; g += coldata_g_; b += coldata_b_;
            }
            if (cgadsub_ & 0x40) { r >>= 1; g >>= 1; b >>= 1; }  // half
            r = r < 0 ? 0 : (r > 31 ? 31 : r);
            g = g < 0 ? 0 : (g > 31 ? 31 : g);
            b = b < 0 ? 0 : (b > 31 ? 31 : b);
        }

        row[x * 3 + 0] = static_cast<uint8_t>((r << 3 | r >> 2) * bright);
        row[x * 3 + 1] = static_cast<uint8_t>((g << 3 | g >> 2) * bright);
        row[x * 3 + 2] = static_cast<uint8_t>((b << 3 | b >> 2) * bright);
    }
}

}  // namespace famemu::snes
