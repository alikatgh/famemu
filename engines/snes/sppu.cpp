// famemu SNES engine — S-PPU implementation. See sppu.hpp.
#include "sppu.hpp"

namespace famemu::snes {

// BG bit depth per mode (0 = BG absent in that mode). Mode 7 special-cased.
const uint8_t SPpu::kModeBpp[8][4] = {
    {2, 2, 2, 2},  // 0
    {4, 4, 2, 0},  // 1
    {4, 4, 0, 0},  // 2 (offset-per-tile)
    {8, 4, 0, 0},  // 3
    {8, 2, 0, 0},  // 4 (offset-per-tile)
    {4, 2, 0, 0},  // 5 (hires)
    {4, 0, 0, 0},  // 6 (hires, offset-per-tile)
    {0, 0, 0, 0},  // 7
};

void SPpu::write(uint8_t reg, uint8_t v) {
    switch (reg) {
        case 0x00: inidisp_ = v; break;
        case 0x01: obsel_ = v; break;
        case 0x02:
            oamadd_reload_ = static_cast<uint16_t>((oamadd_reload_ & 0x100) | v);
            oamadd_ = static_cast<uint16_t>((oamadd_reload_ << 1) & 0x3FF);
            break;
        case 0x03:
            oamadd_reload_ = static_cast<uint16_t>(((v & 1) << 8) | (oamadd_reload_ & 0xFF));
            oam_prio_rotate_ = v & 0x80;
            oamadd_ = static_cast<uint16_t>((oamadd_reload_ << 1) & 0x3FF);
            break;
        case 0x04:  // OAMDATA: low table writes land as latched pairs
            if (oamadd_ < 0x200) {
                if (oamadd_ & 1) {
                    oam_[oamadd_ - 1] = oam_lo_latch_;
                    oam_[oamadd_] = v;
                } else {
                    oam_lo_latch_ = v;
                }
            } else {
                oam_[0x200 | (oamadd_ & 0x1F)] = v;
            }
            oamadd_ = (oamadd_ + 1) & 0x3FF;
            break;
        case 0x05: bgmode_ = v; break;
        case 0x06: mosaic_ = v; break;
        case 0x07: bg1sc_ = v; break;
        case 0x08: bg2sc_ = v; break;
        case 0x09: bg3sc_ = v; break;
        case 0x0A: bg4sc_ = v; break;
        case 0x0B: bg12nba_ = v; break;
        case 0x0C: bg34nba_ = v; break;
        case 0x0D:  // BG1HOFS doubles as M7HOFS (separate latch, 13-bit signed)
            bghofs_[0] = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
            scroll_latch_ = v;   // write-twice: new<<8|prev, simplified
            m7hofs_ = sign13(static_cast<uint16_t>((v << 8) | m7_latch_));
            m7_latch_ = v;
            break;
        case 0x0E:
            bgvofs_[0] = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
            scroll_latch_ = v;
            m7vofs_ = sign13(static_cast<uint16_t>((v << 8) | m7_latch_));
            m7_latch_ = v;
            break;
        case 0x0F: case 0x11: case 0x13: {
            const int bg = 1 + (reg - 0x0F) / 2;
            bghofs_[bg] = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
            scroll_latch_ = v;
            break;
        }
        case 0x10: case 0x12: case 0x14: {
            const int bg = 1 + (reg - 0x10) / 2;
            bgvofs_[bg] = static_cast<uint16_t>((v << 8) | scroll_latch_) & 0x3FF;
            scroll_latch_ = v;
            break;
        }
        case 0x1A: m7sel_ = v; break;
        // $211B-$2120 share one write-twice latch: value = new<<8 | prev.
        case 0x1B: m7a_ = static_cast<int16_t>((v << 8) | m7_latch_); m7_latch_ = v; break;
        case 0x1C:
            m7b_ = static_cast<int16_t>((v << 8) | m7_latch_);
            m7_latch_ = v;
            m7b_byte_ = v;  // MPY multiplies M7A by the latest M7B byte
            break;
        case 0x1D: m7c_ = static_cast<int16_t>((v << 8) | m7_latch_); m7_latch_ = v; break;
        case 0x1E: m7d_ = static_cast<int16_t>((v << 8) | m7_latch_); m7_latch_ = v; break;
        case 0x1F: m7cx_ = sign13(static_cast<uint16_t>((v << 8) | m7_latch_)); m7_latch_ = v; break;
        case 0x20: m7cy_ = sign13(static_cast<uint16_t>((v << 8) | m7_latch_)); m7_latch_ = v; break;
        case 0x15: vmain_ = v; break;
        case 0x16:
            vmadd_ = (vmadd_ & 0xFF00) | v;
            vram_prefetch_ = vram_word(vram_remap(vmadd_));
            break;
        case 0x17:
            vmadd_ = static_cast<uint16_t>((v << 8) | (vmadd_ & 0x00FF));
            vram_prefetch_ = vram_word(vram_remap(vmadd_));
            break;
        case 0x18: {  // VMDATAL
            const uint16_t a = vram_remap(vmadd_);
            vram_[(a * 2) & 0xFFFF] = v;
            if (!(vmain_ & 0x80)) vmadd_ += vram_step();
            break;
        }
        case 0x19: {  // VMDATAH
            const uint16_t a = vram_remap(vmadd_);
            vram_[(a * 2 + 1) & 0xFFFF] = v;
            if (vmain_ & 0x80) vmadd_ += vram_step();
            break;
        }
        case 0x21: cgadd_ = v; cg_latch_ = false; cg_rd_latch_ = false; break;
        case 0x22:
            if (!cg_latch_) { cg_low_ = v; cg_latch_ = true; }
            else {
                cgram_[cgadd_ * 2] = cg_low_;
                cgram_[cgadd_ * 2 + 1] = v & 0x7F;
                ++cgadd_;
                cg_latch_ = false;
            }
            break;
        case 0x23: w12sel_ = v; break;
        case 0x24: w34sel_ = v; break;
        case 0x25: wobjsel_ = v; break;
        case 0x26: case 0x27: case 0x28: case 0x29: wh_[reg - 0x26] = v; break;
        case 0x2A: wbglog_ = v; break;
        case 0x2B: wobjlog_ = v; break;
        case 0x2C: tm_ = v; break;
        case 0x2D: ts_ = v; break;
        case 0x2E: tmw_ = v; break;
        case 0x2F: tsw_ = v; break;
        case 0x30: cgwsel_ = v; break;
        case 0x31: cgadsub_ = v; break;
        case 0x32:  // COLDATA: bit5/6/7 select channels, low 5 bits intensity
            if (v & 0x20) coldata_r_ = v & 0x1F;
            if (v & 0x40) coldata_g_ = v & 0x1F;
            if (v & 0x80) coldata_b_ = v & 0x1F;
            break;
        case 0x33: setini_ = v; break;
        default: break;
    }
}

uint8_t SPpu::read(uint8_t reg, uint8_t open_bus) {
    switch (reg) {
        case 0x34: case 0x35: case 0x36: {  // MPY: M7A * latest M7B byte
            const int32_t r = static_cast<int32_t>(m7a_) *
                              static_cast<int8_t>(m7b_byte_);
            return ppu1_mdr_ = static_cast<uint8_t>(r >> (8 * (reg - 0x34)));
        }
        case 0x38: {  // OAMDATAREAD
            const uint16_t a = (oamadd_ < 0x200) ? oamadd_
                                                 : (0x200 | (oamadd_ & 0x1F));
            const uint8_t v = oam_[a];
            oamadd_ = (oamadd_ + 1) & 0x3FF;
            return ppu1_mdr_ = v;
        }
        case 0x39: {  // VMDATALREAD (prefetched)
            const uint8_t v = static_cast<uint8_t>(vram_prefetch_);
            if (!(vmain_ & 0x80)) {
                vram_prefetch_ = vram_word(vram_remap(vmadd_));
                vmadd_ += vram_step();
            }
            return ppu1_mdr_ = v;
        }
        case 0x3A: {
            const uint8_t v = static_cast<uint8_t>(vram_prefetch_ >> 8);
            if (vmain_ & 0x80) {
                vram_prefetch_ = vram_word(vram_remap(vmadd_));
                vmadd_ += vram_step();
            }
            return ppu1_mdr_ = v;
        }
        case 0x3B: {  // CGDATAREAD
            uint8_t v;
            if (!cg_rd_latch_) { v = cgram_[cgadd_ * 2]; cg_rd_latch_ = true; }
            else {
                v = static_cast<uint8_t>(cgram_[cgadd_ * 2 + 1] | (open_bus & 0x80));
                ++cgadd_;
                cg_rd_latch_ = false;
            }
            return ppu2_mdr_ = v;
        }
        case 0x3C: {  // OPHCT (read-twice: low, then bit8)
            uint8_t v;
            if (!oph_byte_) { v = static_cast<uint8_t>(ophct_); }
            else { v = static_cast<uint8_t>(((ophct_ >> 8) & 1) | (open_bus & 0xFE)); }
            oph_byte_ = !oph_byte_;
            return ppu2_mdr_ = v;
        }
        case 0x3D: {
            uint8_t v;
            if (!opv_byte_) { v = static_cast<uint8_t>(opvct_); }
            else { v = static_cast<uint8_t>(((opvct_ >> 8) & 1) | (open_bus & 0xFE)); }
            opv_byte_ = !opv_byte_;
            return ppu2_mdr_ = v;
        }
        case 0x3E:  // STAT77: OBJ time (bit7) / range (bit6) + PPU1 version
            return ppu1_mdr_ = static_cast<uint8_t>(stat77_ | (open_bus & 0x10));
        case 0x3F: {  // STAT78: field, latch flag, NTSC, PPU2 version
            uint8_t v = 0x03;                          // version
            if (field_) v |= 0x80;
            if (hv_latched_) v |= 0x40;
            hv_latched_ = false;
            oph_byte_ = opv_byte_ = false;
            return ppu2_mdr_ = static_cast<uint8_t>(v | (open_bus & 0x20));
        }
        default:
            return open_bus;
    }
}

void SPpu::frame_start() {
    field_ = !field_;
    stat77_ &= 0x3F;                                   // range/time clear
}

void SPpu::vblank_begin() {
    if (!(inidisp_ & 0x80))
        oamadd_ = static_cast<uint16_t>((oamadd_reload_ << 1) & 0x3FF);
}

// ---- windows -------------------------------------------------------------

bool SPpu::window_state(int layer, int x) const {
    uint8_t sel = (layer < 2) ? w12sel_ : (layer < 4) ? w34sel_ : wobjsel_;
    const int sh = (layer < 4) ? (layer & 1) * 4 : (layer - 4) * 4;
    sel = static_cast<uint8_t>(sel >> sh);
    const bool en1 = sel & 2, en2 = sel & 8;
    if (!en1 && !en2) return false;
    const bool in1 = ((x >= wh_[0] && x <= wh_[1]) != ((sel & 1) != 0));
    const bool in2 = ((x >= wh_[2] && x <= wh_[3]) != ((sel & 4) != 0));
    if (en1 && !en2) return in1;
    if (!en1 && en2) return in2;
    const uint8_t lg = (layer < 4)
        ? static_cast<uint8_t>((wbglog_ >> (layer * 2)) & 3)
        : static_cast<uint8_t>((wobjlog_ >> ((layer - 4) * 2)) & 3);
    switch (lg) {
        case 0: return in1 || in2;
        case 1: return in1 && in2;
        case 2: return in1 != in2;
        default: return in1 == in2;
    }
}

// ---- BG fetch --------------------------------------------------------------

// Tilemap entry for pixel (sx, sy) given SC register and tile pixel size.
static uint16_t map_entry(const uint8_t* vram, uint8_t sc, int sx, int sy,
                          int tsz) {
    const uint16_t map_base = static_cast<uint16_t>((sc >> 2) << 10);
    const int sh = (tsz == 16) ? 4 : 3;
    const int tx = (sx >> sh) & 31, ty = (sy >> sh) & 31;
    int screen = 0;
    const int sc_size = sc & 3;
    const int xbit = 0x100 << (sh - 3), ybit = xbit;
    if ((sc_size & 1) && (sx & xbit)) screen += 1;
    if ((sc_size & 2) && (sy & ybit)) screen += (sc_size & 1) ? 2 : 1;
    const uint32_t w = static_cast<uint32_t>(map_base + screen * 0x400 + ty * 32 + tx);
    return static_cast<uint16_t>(vram[(w * 2) & 0xFFFF] | (vram[(w * 2 + 1) & 0xFFFF] << 8));
}

void SPpu::fetch_bg_pixel(int bg, int x, int y, BgPix& out) {
    out.opaque = false;
    out.color_idx = 0;
    out.prio = 0;
    out.pal = 0;
    const int mode = bgmode_ & 7;
    const int bpp = kModeBpp[mode][bg];
    if (!bpp) return;

    if (mosaic_ & (1 << bg)) {
        const int sz = (mosaic_ >> 4) + 1;
        x -= x % sz;
        y -= y % sz;
    }

    uint16_t hofs = bghofs_[bg], vofs = bgvofs_[bg];

    // Offset-per-tile (modes 2/4/6): BG3's tilemap holds per-column scroll
    // replacements for BG1/BG2, valid from the second visible tile column on.
    // The column decision includes the BG's own fine scroll (bsnes/snes9x).
    const int optcx = x + (hofs & 7);
    if ((mode == 2 || mode == 4 || mode == 6) && bg < 2 && optcx >= 8) {
        const int optx = (optcx & ~7) - 8 + (bghofs_[2] & 0x3F8);
        const uint16_t e_h = map_entry(vram_, bg3sc_, optx, bgvofs_[2] & 0x3FF, 8);
        const uint16_t apply_bit = (bg == 0) ? 0x2000 : 0x4000;
        if (mode == 4) {
            if (e_h & apply_bit) {
                if (e_h & 0x8000) vofs = e_h & 0x3FF;
                else hofs = static_cast<uint16_t>((hofs & 7) | (e_h & 0x3F8));
            }
        } else {
            const uint16_t e_v = map_entry(vram_, bg3sc_,
                                           optx, (bgvofs_[2] + 8) & 0x3FF, 8);
            if (e_h & apply_bit)
                hofs = static_cast<uint16_t>((hofs & 7) | (e_h & 0x3F8));
            if (e_v & apply_bit) vofs = e_v & 0x3FF;
        }
    }

    // $210B/$210C: LOW nibble = BG1/BG3, HIGH nibble = BG2/BG4.
    const uint8_t nba = (bg == 0)   ? (bg12nba_ & 0x0F)
                        : (bg == 1) ? (bg12nba_ >> 4)
                        : (bg == 2) ? (bg34nba_ & 0x0F)
                                    : (bg34nba_ >> 4);
    const uint16_t tile_base = static_cast<uint16_t>(nba << 12);
    const uint8_t sc = (bg == 0) ? bg1sc_ : (bg == 1) ? bg2sc_
                       : (bg == 2) ? bg3sc_ : bg4sc_;
    // 16x16 tiles per BGMODE bits 4-7 (modes 5/6 force 16-wide tiles).
    const bool big = (bgmode_ & (0x10 << bg)) || mode == 5 || mode == 6;
    const int tsz = big ? 16 : 8;

    const int sx = (x + hofs) & 0x3FF;
    // fb row y is hardware scanline y+1 (the PPU never displays scanline 0),
    // so BG sampling is offset by one; OBJ needs no offset because OAM Y is
    // itself "appears one line below Y", which cancels in fb-row space.
    const int sy = (y + 1 + vofs) & 0x3FF;

    const uint16_t entry = map_entry(vram_, sc, sx, sy, tsz);
    const int tile = entry & 0x3FF;
    const int pal = (entry >> 10) & 7;
    const bool xflip = entry & 0x4000, yflip = entry & 0x8000;
    out.prio = (entry & 0x2000) ? 1 : 0;
    out.pal = static_cast<uint8_t>(pal);

    int cx = sx & (tsz - 1);
    if (xflip) cx = tsz - 1 - cx;
    int cy = sy & (tsz - 1);
    if (yflip) cy = tsz - 1 - cy;
    const int t = (tile + (cx >> 3) + (cy >> 3) * 16) & 0x3FF;
    const int row = cy & 7, col = cx & 7;

    // Planar tile data: 2bpp = 8 words/tile; 4bpp = 16; 8bpp = 32.
    const int words_per_tile = bpp * 4;
    const uint32_t taddr =
        static_cast<uint32_t>(tile_base) + t * words_per_tile + row;
    int idx = 0;
    for (int plane = 0; plane < bpp; plane += 2) {
        const uint32_t pa = taddr + plane * 4;
        const uint8_t b0 = vram_[(pa * 2) & 0xFFFF];
        const uint8_t b1 = vram_[(pa * 2 + 1) & 0xFFFF];
        idx |= ((b0 >> (7 - col)) & 1) << plane;
        idx |= ((b1 >> (7 - col)) & 1) << (plane + 1);
    }
    if (idx == 0) return;  // transparent

    // Palette mapping: 8bpp uses the full 256 colors; 4bpp = 16-color
    // palettes; 2bpp = 4-color palettes, with mode-0's per-BG base offset.
    if (bpp == 8) {
        out.color_idx = static_cast<uint8_t>(idx);
    } else if (bpp == 4) {
        out.color_idx = static_cast<uint8_t>(pal * 16 + idx);
    } else {
        const int base = (mode == 0) ? bg * 32 : 0;
        out.color_idx = static_cast<uint8_t>(base + pal * 4 + idx);
    }
    out.opaque = true;
}

// Mode 7: BG1 becomes a 128x128-tile 8bpp rotated/scaled plane. VRAM words
// interleave the map (low bytes, addr = ty*128+tx) and char data (high bytes,
// addr = tile*64 + row*8 + col). The per-line origin uses the hardware's
// odd 10-bit clip and 6-bit truncation (matches bsnes/snes9x). With EXTBG
// (SETINI bit 6), BG2 shows the same plane with pixel bit 7 as priority.
void SPpu::fetch_mode7_pixel(int bg, int x, int y, BgPix& out) {
    out.opaque = false;
    out.color_idx = 0;
    out.prio = 0;
    out.pal = 0;

    if (mosaic_ & (1 << bg)) {
        const int sz = (mosaic_ >> 4) + 1;
        x -= x % sz;
        y -= y % sz;
    }

    int sx = x, sy = y + 1;              // fb row y = scanline y+1
    if (m7sel_ & 0x01) sx = 255 - sx;
    if (m7sel_ & 0x02) sy = 255 - sy;

    auto clip = [](int n) { return (n & 0x2000) ? (n | ~0x3FF) : (n & 0x3FF); };
    const int a = m7a_, b = m7b_, c = m7c_, d = m7d_;
    const int hoff = clip(m7hofs_ - m7cx_), voff = clip(m7vofs_ - m7cy_);
    const int ox = ((a * hoff) & ~63) + ((b * voff) & ~63) + ((b * sy) & ~63) +
                   (m7cx_ << 8);
    const int oy = ((c * hoff) & ~63) + ((d * voff) & ~63) + ((d * sy) & ~63) +
                   (m7cy_ << 8);
    int px = (ox + a * sx) >> 8;
    int py = (oy + c * sx) >> 8;

    int tile;
    if ((px | py) & ~1023) {             // outside the 1024x1024 plane
        switch (m7sel_ >> 6) {
            case 2: return;              // transparent
            case 3: tile = 0; break;     // repeat tile 0
            default: px &= 1023; py &= 1023; tile = -1; break;  // wrap
        }
    } else {
        tile = -1;
    }
    if (tile < 0)
        tile = vram_[(((py >> 3) * 128 + (px >> 3)) * 2) & 0xFFFF];
    const uint8_t color =
        vram_[((tile * 64 + (py & 7) * 8 + (px & 7)) * 2 + 1) & 0xFFFF];
    if (bg == 1) {                       // EXTBG: 7-bit color, bit7 = priority
        out.prio = color >> 7;
        if ((color & 0x7F) == 0) return;
        out.color_idx = static_cast<uint8_t>(color & 0x7F);
    } else {
        if (color == 0) return;
        out.color_idx = color;           // direct 256-colour
    }
    out.opaque = true;
}

uint16_t SPpu::bg_pixel_color(int bg, const BgPix& p) const {
    const int mode = bgmode_ & 7;
    const bool direct =
        (cgwsel_ & 0x01) && bg == 0 &&
        (mode == 3 || mode == 4 || mode == 7);
    if (direct) {
        const uint8_t c = p.color_idx, tp = p.pal;
        const int r = ((c & 0x07) << 2) | ((tp & 1) << 1);
        const int g = (((c >> 3) & 0x07) << 2) | (tp & 2);
        const int b = (((c >> 6) & 0x03) << 3) | (tp & 4);
        return static_cast<uint16_t>(r | (g << 5) | (b << 10));
    }
    return cg_color(p.color_idx);
}

// ---- OBJ line evaluation -----------------------------------------------------

void SPpu::evaluate_objects(int y) {
    for (int i = 0; i < kWidth; ++i) obj_line_[i].opaque = false;
    const uint16_t obj_base = static_cast<uint16_t>((obsel_ & 7) << 13);
    const uint16_t name_gap =
        static_cast<uint16_t>(0x1000 + (((obsel_ >> 3) & 3) << 12));
    const int size_mode = (obsel_ >> 5) & 7;
    static const int kSmall[8] = {8, 8, 8, 16, 16, 32, 16, 16};
    static const int kLarge[8] = {16, 32, 64, 32, 64, 64, 32, 32};

    // Range: the first 32 sprites (from the rotation point) on this line.
    int in_range[32];
    int nrange = 0;
    const int first = oam_prio_rotate_
        ? ((oamadd_reload_ >> 1) & 0x7F) : 0;
    for (int i = 0; i < 128; ++i) {
        const int s = (first + i) & 127;
        const uint8_t* o = &oam_[s * 4];
        const uint8_t hi = oam_[512 + (s >> 2)];
        const int hi_shift = (s & 3) * 2;
        const bool large = (hi >> hi_shift) & 2;
        const int size = large ? kLarge[size_mode] : kSmall[size_mode];
        const int oy = o[1];
        int row = y - oy;
        if (row < 0) row += 256;                 // Y wraps
        if (row >= size) continue;
        const int x9 = o[0] | (((hi >> hi_shift) & 1) << 8);
        const int ox = (x9 >= 256) ? x9 - 512 : x9;
        if (ox <= -size || ox >= 256) continue;
        if (nrange == 32) { stat77_ |= 0x40; break; }
        in_range[nrange++] = s;
    }

    // Time: 34 8x1 slivers. Tiles are fetched from the LAST in-range sprite
    // backward, so a time-over drops the lowest-index (highest-priority)
    // sprites — matching hardware/snes9x. Drawing back-to-front with
    // overwrite keeps the low-index-wins overlap rule among survivors.
    int slivers = 0;
    for (int r = nrange - 1; r >= 0; --r) {
        const int s = in_range[r];
        const uint8_t* o = &oam_[s * 4];
        const uint8_t hi = oam_[512 + (s >> 2)];
        const int hi_shift = (s & 3) * 2;
        const bool large = (hi >> hi_shift) & 2;
        const int size = large ? kLarge[size_mode] : kSmall[size_mode];
        const int x9 = o[0] | (((hi >> hi_shift) & 1) << 8);
        const int ox = (x9 >= 256) ? x9 - 512 : x9;
        int row = y - o[1];
        if (row < 0) row += 256;

        const int tile = o[2] | ((o[3] & 1) << 8);
        const int pal = (o[3] >> 1) & 7;
        const int prio = (o[3] >> 4) & 3;
        const bool xf = o[3] & 0x40, yf = o[3] & 0x80;
        if (yf) row = size - 1 - row;

        for (int cx0 = 0; cx0 < size; cx0 += 8) {
            const int sx = ox + (xf ? size - 8 - cx0 : cx0);
            if (sx <= -8 || sx >= 256) continue;
            if (slivers == 34) { stat77_ |= 0x80; return; }
            ++slivers;
            // 16x16+ sprites tile like 8x8 cells in a 16-wide name grid;
            // row and column each wrap within their nibble of the name.
            const int trow = row >> 3, tcol = cx0 >> 3;
            const int t = (tile & 0x100) | ((tile + trow * 16) & 0xF0) |
                          ((tile + tcol) & 0x0F);
            uint32_t taddr = obj_base + (t & 0xFF) * 16 + (row & 7);
            if (t & 0x100) taddr += name_gap;   // OBSEL bits 3-4 page gap
            const uint8_t b0 = vram_[(taddr * 2) & 0xFFFF];
            const uint8_t b1 = vram_[(taddr * 2 + 1) & 0xFFFF];
            const uint8_t b2 = vram_[((taddr + 8) * 2) & 0xFFFF];
            const uint8_t b3 = vram_[((taddr + 8) * 2 + 1) & 0xFFFF];
            for (int px = 0; px < 8; ++px) {
                const int x = sx + (xf ? 7 - px : px);
                if (x < 0 || x >= 256) continue;
                const int idx = ((b0 >> (7 - px)) & 1) |
                                (((b1 >> (7 - px)) & 1) << 1) |
                                (((b2 >> (7 - px)) & 1) << 2) |
                                (((b3 >> (7 - px)) & 1) << 3);
                if (idx == 0) continue;
                obj_line_[x].opaque = true;
                obj_line_[x].color_idx = static_cast<uint8_t>(128 + pal * 16 + idx);
                obj_line_[x].prio = static_cast<uint8_t>(prio);
            }
        }
    }
}

// ---- screen resolution -----------------------------------------------------

namespace {
struct Ent { uint8_t layer, prio; };
// Layer priority tables, FRONT to BACK (layer: 0-3 = BG1-4, 4 = OBJ).
const Ent kM0[] = {{4,3},{0,1},{1,1},{4,2},{0,0},{1,0},{4,1},{2,1},{3,1},{4,0},{2,0},{3,0}};
const Ent kM1[] = {{4,3},{0,1},{1,1},{4,2},{0,0},{1,0},{4,1},{2,1},{4,0},{2,0}};
const Ent kM1p[] = {{2,1},{4,3},{0,1},{1,1},{4,2},{0,0},{1,0},{4,1},{4,0},{2,0}};
const Ent kM26[] = {{4,3},{0,1},{4,2},{1,1},{4,1},{0,0},{4,0},{1,0}};
const Ent kM7[] = {{4,3},{4,2},{4,1},{0,0},{4,0}};
const Ent kM7e[] = {{4,3},{4,2},{1,1},{4,1},{0,0},{4,0},{1,0}};
}  // namespace

SPpu::Pixel SPpu::resolve_screen(uint8_t designation, uint8_t win_mask,
                                 int x, int y, BgPix* bp, bool* bd) {
    const int mode = bgmode_ & 7;
    const Ent* tab;
    int n;
    if (mode == 7) {
        if (setini_ & 0x40) { tab = kM7e; n = 7; }
        else { tab = kM7; n = 5; }
    } else if (mode == 0) { tab = kM0; n = 12; }
    else if (mode == 1) {
        if (bgmode_ & 0x08) { tab = kM1p; n = 10; }
        else { tab = kM1; n = 10; }
    } else { tab = kM26; n = 8; }

    // bp/bd: per-x BG cache shared between main- and sub-screen resolutions.
    for (int i = 0; i < n; ++i) {
        const int l = tab[i].layer;
        if (!(designation & (1 << l))) continue;
        if ((win_mask & (1 << l)) && window_state(l, x)) continue;
        if (l == kOBJ) {
            const ObjPix& op = obj_line_[x];
            if (!op.opaque || op.prio != tab[i].prio) continue;
            Pixel out;
            out.c15 = cg_color(op.color_idx);
            out.layer = kOBJ;
            out.valid = true;
            // stash palette exemption in layer choice: caller re-checks
            return out;
        }
        if (!bd[l]) {
            if (mode == 7) fetch_mode7_pixel(l, x, y, bp[l]);
            else fetch_bg_pixel(l, x, y, bp[l]);
            bd[l] = true;
        }
        const BgPix& p = bp[l];
        if (!p.opaque || p.prio != tab[i].prio) continue;
        Pixel out;
        out.c15 = bg_pixel_color(l, p);
        out.layer = static_cast<uint8_t>(l);
        out.valid = true;
        return out;
    }
    Pixel out;
    out.c15 = cg_color(0);
    out.layer = kBACK;
    out.valid = false;
    return out;
}

void SPpu::render_line(int y) {
    uint8_t* row = fb_ + static_cast<size_t>(y) * kWidth * 3;
    if (inidisp_ & 0x80) {  // force blank
        std::memset(row, 0, kWidth * 3);
        return;
    }
    evaluate_objects(y);
    const float bright = static_cast<float>(inidisp_ & 0x0F) / 15.0f;
    const bool sub_used = (cgwsel_ & 0x02) != 0;
    const uint8_t clip_mode = cgwsel_ >> 6;
    const uint8_t math_mode = (cgwsel_ >> 4) & 3;

    for (int x = 0; x < kWidth; ++x) {
        BgPix bgpix[4];
        bool bgdone[4] = {false, false, false, false};
        const Pixel px = resolve_screen(tm_, tmw_, x, y, bgpix, bgdone);

        // Color window: clip-to-black and math-enable regions (CGWSEL).
        const bool mwin = window_state(5, x);
        const bool clipped =
            clip_mode == 3 || (clip_mode == 2 && mwin) || (clip_mode == 1 && !mwin);
        const bool math_ok =
            math_mode == 0 || (math_mode == 1 && mwin) || (math_mode == 2 && !mwin);

        int r, g, b;
        if (clipped) {
            r = g = b = 0;
        } else {
            r = px.c15 & 0x1F;
            g = (px.c15 >> 5) & 0x1F;
            b = (px.c15 >> 10) & 0x1F;
        }

        // Color math on the layers selected in CGADSUB. The operand is the
        // subscreen pixel when CGWSEL bit1 is set and one is opaque there,
        // else the COLDATA fixed color — and half-math never applies to the
        // fixed-color fallback nor when the main pixel was clipped to black.
        const uint8_t layer_bit =
            (px.layer <= kBG4) ? static_cast<uint8_t>(1 << px.layer)
            : (px.layer == kOBJ) ? 0x10 : 0x20;
        // OBJ palettes 0-3 (color_idx 128-191) never participate in math.
        const bool obj_exempt =
            px.layer == kOBJ && obj_line_[x].color_idx < 192;
        if ((cgadsub_ & layer_bit) && !obj_exempt && math_ok) {
            int sr = coldata_r_, sg = coldata_g_, sb = coldata_b_;
            bool half = cgadsub_ & 0x40;
            if (sub_used) {
                const Pixel sub = resolve_screen(ts_, tsw_, x, y, bgpix, bgdone);
                if (sub.valid) {
                    sr = sub.c15 & 0x1F;
                    sg = (sub.c15 >> 5) & 0x1F;
                    sb = (sub.c15 >> 10) & 0x1F;
                } else {
                    half = false;  // fixed-color fallback: no halving
                }
            }
            if (clipped) half = false;
            if (cgadsub_ & 0x80) {
                // Half-subtract drops the subtrahend's low bit before the
                // halving (matches snes9x's channel math, our lockstep ref).
                if (half) { sr &= ~1; sg &= ~1; sb &= ~1; }
                r -= sr; g -= sg; b -= sb;
            } else {
                r += sr; g += sg; b += sb;
            }
            if (half) { r >>= 1; g >>= 1; b >>= 1; }
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
