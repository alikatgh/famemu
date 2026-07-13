// famemu NES engine — 2C02 PPU implementation. See ppu.hpp.
#include "ppu.hpp"

namespace famemu::nes {

// ---- memory map -----------------------------------------------------------

uint16_t Ppu::mirror_nt(uint16_t addr) const {
    addr &= 0x0FFF;
    switch (cart_.mirroring()) {
        case Mirroring::Vertical:   return addr & 0x07FF;
        case Mirroring::Horizontal: return static_cast<uint16_t>(((addr >> 1) & 0x0400) | (addr & 0x03FF));
        case Mirroring::FourScreen: return addr & 0x07FF;  // (extra VRAM not modeled)
        case Mirroring::SingleLow:  return addr & 0x03FF;
        case Mirroring::SingleHigh: return static_cast<uint16_t>(0x0400 | (addr & 0x03FF));
    }
    return addr & 0x07FF;
}

static inline uint8_t palette_index(uint16_t addr) {
    uint8_t i = addr & 0x1F;
    if ((i & 0x13) == 0x10) i &= 0x0F;  // $3F10/14/18/1C mirror $3F00/04/08/0C
    return i;
}

uint8_t Ppu::vram_read(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) return cart_.chr_read(addr);
    if (addr < 0x3F00) return vram_[mirror_nt(addr)];
    return palette_[palette_index(addr)];
}

void Ppu::vram_write(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;
    if (addr < 0x2000) cart_.chr_write(addr, val);
    else if (addr < 0x3F00) vram_[mirror_nt(addr)] = val;
    else palette_[palette_index(addr)] = val;
}

// ---- CPU-visible registers --------------------------------------------------

uint8_t Ppu::read_reg(uint16_t addr) {
    switch (addr & 7) {
        case 2: {  // PPUSTATUS
            uint8_t r = static_cast<uint8_t>((status_ & 0xE0) | (buffer_ & 0x1F));
            status_ &= 0x7F;  // clear vblank
            w_ = false;
            return r;
        }
        case 4: return oam_[oam_addr_];
        case 7: {  // PPUDATA (buffered below palettes)
            uint8_t r;
            if ((v_ & 0x3FFF) >= 0x3F00) {
                r = vram_read(v_);
                buffer_ = vram_read(static_cast<uint16_t>(v_ - 0x1000));
            } else {
                r = buffer_;
                buffer_ = vram_read(v_);
            }
            v_ = static_cast<uint16_t>(v_ + ((ctrl_ & 0x04) ? 32 : 1));
            return r;
        }
        default: return 0;
    }
}

void Ppu::write_reg(uint16_t addr, uint8_t val) {
    switch (addr & 7) {
        case 0: {  // PPUCTRL
            bool was_nmi = ctrl_ & 0x80;
            ctrl_ = val;
            t_ = static_cast<uint16_t>((t_ & ~0x0C00) | ((val & 0x03) << 10));
            // enabling NMI during vblank fires it immediately
            if (!was_nmi && (val & 0x80) && (status_ & 0x80)) nmi_request_ = true;
            break;
        }
        case 1: mask_ = val; break;
        case 3: oam_addr_ = val; break;
        case 4: oam_[oam_addr_++] = val; break;
        case 5:  // PPUSCROLL
            if (!w_) {
                t_ = static_cast<uint16_t>((t_ & ~0x001F) | (val >> 3));
                fine_x_ = val & 0x07;
            } else {
                t_ = static_cast<uint16_t>((t_ & ~0x73E0) | ((val & 0x07) << 12) |
                                           ((val & 0xF8) << 2));
            }
            w_ = !w_;
            break;
        case 6:  // PPUADDR
            if (!w_) {
                t_ = static_cast<uint16_t>((t_ & 0x00FF) | ((val & 0x3F) << 8));
            } else {
                t_ = static_cast<uint16_t>((t_ & 0xFF00) | val);
                v_ = t_;
            }
            w_ = !w_;
            break;
        case 7:
            vram_write(v_, val);
            v_ = static_cast<uint16_t>(v_ + ((ctrl_ & 0x04) ? 32 : 1));
            break;
        default: break;
    }
}

// ---- scrolling (Loopy) ------------------------------------------------------

void Ppu::inc_coarse_x() {
    if ((v_ & 0x001F) == 31) {
        v_ &= ~0x001F;
        v_ ^= 0x0400;  // switch horizontal nametable
    } else {
        ++v_;
    }
}

void Ppu::inc_y() {
    if ((v_ & 0x7000) != 0x7000) {
        v_ += 0x1000;  // fine Y
    } else {
        v_ &= ~0x7000;
        uint16_t coarse_y = (v_ >> 5) & 0x1F;
        if (coarse_y == 29) {
            coarse_y = 0;
            v_ ^= 0x0800;  // switch vertical nametable
        } else if (coarse_y == 31) {
            coarse_y = 0;  // out-of-bounds row: wrap without NT switch
        } else {
            ++coarse_y;
        }
        v_ = static_cast<uint16_t>((v_ & ~0x03E0) | (coarse_y << 5));
    }
}

// ---- background pipeline ------------------------------------------------------

void Ppu::reload_shifters() {
    bg_shift_lo_ = static_cast<uint16_t>((bg_shift_lo_ & 0xFF00) | bg_lo_latch_);
    bg_shift_hi_ = static_cast<uint16_t>((bg_shift_hi_ & 0xFF00) | bg_hi_latch_);
    at_shift_lo_ = static_cast<uint16_t>((at_shift_lo_ & 0xFF00) | ((at_latch_ & 1) ? 0xFF : 0x00));
    at_shift_hi_ = static_cast<uint16_t>((at_shift_hi_ & 0xFF00) | ((at_latch_ & 2) ? 0xFF : 0x00));
}

void Ppu::shift_bg() {
    bg_shift_lo_ <<= 1;
    bg_shift_hi_ <<= 1;
    at_shift_lo_ <<= 1;
    at_shift_hi_ <<= 1;
}

// One tile's worth of fetches, done together at the end of its 8-dot window
// (dots 8,16,...,256 and 328,336). Fine for NROM-class carts; MMC3's A12 IRQ
// will need the split-cycle fetch timing when that mapper lands.
void Ppu::bg_fetch_step() {
    nt_latch_ = vram_read(static_cast<uint16_t>(0x2000 | (v_ & 0x0FFF)));
    uint8_t at = vram_read(static_cast<uint16_t>(
        0x23C0 | (v_ & 0x0C00) | ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07)));
    uint8_t quad = static_cast<uint8_t>(((v_ >> 4) & 4) | (v_ & 2));
    at_latch_ = (at >> quad) & 0x03;
    uint16_t pat = static_cast<uint16_t>(((ctrl_ & 0x10) ? 0x1000 : 0x0000) +
                                         nt_latch_ * 16 + ((v_ >> 12) & 7));
    bg_lo_latch_ = vram_read(pat);
    bg_hi_latch_ = vram_read(static_cast<uint16_t>(pat + 8));
    reload_shifters();
    inc_coarse_x();
}

// ---- sprites -------------------------------------------------------------------

void Ppu::evaluate_sprites(int line) {
    scan_count_ = 0;
    if (line < 0 || line > 239) return;
    const int h = (ctrl_ & 0x20) ? 16 : 8;
    for (int i = 0; i < 64; ++i) {
        const uint8_t* o = &oam_[i * 4];
        // Sprites are delayed one scanline: OAM Y=y draws on lines y+1..y+h
        // (blargg sprite_hit 02.alignment #8 / 03.corners verify this).
        int row = line - o[0] - 1;
        if (row < 0 || row >= h) continue;
        if (scan_count_ == 8) {
            status_ |= 0x20;  // sprite overflow (simplified: no buggy scan)
            break;
        }
        const uint8_t attr = o[2];
        if (attr & 0x80) row = h - 1 - row;  // vertical flip
        uint16_t pat;
        if (h == 16) {
            uint8_t tile = static_cast<uint8_t>((o[1] & 0xFE) + (row >= 8 ? 1 : 0));
            pat = static_cast<uint16_t>(((o[1] & 1) ? 0x1000 : 0x0000) + tile * 16 + (row & 7));
        } else {
            pat = static_cast<uint16_t>(((ctrl_ & 0x08) ? 0x1000 : 0x0000) + o[1] * 16 + row);
        }
        uint8_t lo = vram_read(pat), hi = vram_read(static_cast<uint16_t>(pat + 8));
        if (attr & 0x40) {  // horizontal flip: reverse both bit planes
            auto rev = [](uint8_t b) {
                b = static_cast<uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
                b = static_cast<uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
                return static_cast<uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
            };
            lo = rev(lo);
            hi = rev(hi);
        }
        scan_sprites_[scan_count_++] = {o[3], attr, lo, hi, i == 0};
    }
}

// ---- per-dot pixel ---------------------------------------------------------------

void Ppu::render_dot() {
    const int x = dot_ - 1;

    // Background pixel.
    uint8_t bg_pat = 0, bg_attr = 0;
    if ((mask_ & 0x08) && (x >= 8 || (mask_ & 0x02))) {
        const int bit = 15 - fine_x_;
        bg_pat = static_cast<uint8_t>((((bg_shift_hi_ >> bit) & 1) << 1) |
                                      ((bg_shift_lo_ >> bit) & 1));
        bg_attr = static_cast<uint8_t>((((at_shift_hi_ >> bit) & 1) << 1) |
                                       ((at_shift_lo_ >> bit) & 1));
    }

    // First matching sprite pixel (OAM order = priority among sprites).
    uint8_t sp_pat = 0, sp_attr = 0;
    bool sp_is0 = false;
    if ((mask_ & 0x10) && (x >= 8 || (mask_ & 0x04))) {
        for (int i = 0; i < scan_count_; ++i) {
            const ScanSprite& sp = scan_sprites_[i];
            const int col = x - sp.x;
            if (col < 0 || col > 7) continue;
            const int bit = 7 - col;
            uint8_t pat = static_cast<uint8_t>((((sp.pat_hi >> bit) & 1) << 1) |
                                               ((sp.pat_lo >> bit) & 1));
            if (pat == 0) continue;
            sp_pat = pat;
            sp_attr = sp.attr;
            sp_is0 = sp.is_sprite0;
            break;
        }
    }

    // Sprite-0 hit: opaque sprite-0 pixel over opaque background pixel.
    if (sp_is0 && sp_pat && bg_pat && x < 255) {
#ifdef FAMEMU_PPU_DEBUG
        if (!(status_ & 0x40))
            std::fprintf(stderr, "s0hit sl=%d x=%d\n", scanline_, x);
#endif
        status_ |= 0x40;
    }
#ifdef FAMEMU_PPU_DEBUG
    if (scanline_ == 120 && bg_pat) {
        static int last_frame = -1;
        if (static_cast<int>(frame_) != last_frame) {
            last_frame = static_cast<int>(frame_);
            std::fprintf(stderr, "bg sl120 first-opaque x=%d (frame %d)\n", x,
                         last_frame);
        }
    }
#endif

    // Priority mux.
    uint8_t pal_addr;
    if (bg_pat == 0 && sp_pat == 0) pal_addr = 0;
    else if (bg_pat == 0)           pal_addr = static_cast<uint8_t>(0x10 | ((sp_attr & 3) << 2) | sp_pat);
    else if (sp_pat == 0)           pal_addr = static_cast<uint8_t>((bg_attr << 2) | bg_pat);
    else if (sp_attr & 0x20)        pal_addr = static_cast<uint8_t>((bg_attr << 2) | bg_pat);   // behind BG
    else                            pal_addr = static_cast<uint8_t>(0x10 | ((sp_attr & 3) << 2) | sp_pat);

    uint8_t color = palette_[palette_index(pal_addr)];
    if (mask_ & 0x01) color &= 0x30;  // grayscale
    fb_[scanline_ * kWidth + x] = color & 0x3F;
}

// ---- the dot clock -----------------------------------------------------------------

void Ppu::tick() {
    const bool render_line = scanline_ < 240 || scanline_ == 261;

    if (render_line) {
        if (rendering()) {
            if (scanline_ != 261 && dot_ >= 1 && dot_ <= 256) render_dot();

            const bool fetch_window =
                (dot_ >= 1 && dot_ <= 256) || (dot_ >= 321 && dot_ <= 336);
            if (fetch_window) {
                // Order matters: sample (render_dot above) → shift → reload.
                // Reloading before the shift gives the fresh tile one extra
                // shift and drags the whole background 1px left (caught by
                // blargg sprite_hit 02.alignment #3).
                shift_bg();
                if ((dot_ & 7) == 0) bg_fetch_step();
            }
            if (dot_ == 256) inc_y();
            if (dot_ == 257) {
                copy_x();
                evaluate_sprites(scanline_ == 261 ? 0 : scanline_ + 1);
            }
            if (scanline_ == 261 && dot_ >= 280 && dot_ <= 304) copy_y();
            // MMC3 scanline counter: clocked by the PPU A12 rise during the
            // sprite-fetch window; dot 260 is the standard approximation.
            if (dot_ == 260) cart_.ppu_scanline();
        } else if (scanline_ != 261 && dot_ >= 1 && dot_ <= 256) {
            // Forced blank: backdrop color.
            fb_[scanline_ * kWidth + (dot_ - 1)] = palette_[0] & 0x3F;
        }
    }

    if (scanline_ == 241 && dot_ == 1) {
        status_ |= 0x80;
        ++frame_;
        if (ctrl_ & 0x80) nmi_request_ = true;
    }
    if (scanline_ == 261 && dot_ == 1) status_ &= 0x1F;  // clear vbl/s0/overflow

    // Advance; odd frames drop the pre-render line's last dot when rendering.
    ++dot_;
    if (scanline_ == 261 && dot_ == 340 && odd_frame_ && rendering()) {
        dot_ = 0;
        scanline_ = 0;
        odd_frame_ = !odd_frame_;
        return;
    }
    if (dot_ > 340) {
        dot_ = 0;
        if (++scanline_ > 261) {
            scanline_ = 0;
            odd_frame_ = !odd_frame_;
        }
    }
}

}  // namespace famemu::nes
