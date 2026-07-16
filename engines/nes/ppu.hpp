// famemu NES engine — 2C02 PPU. Dot-driven: background shifters, Loopy
// v/t/x/w scrolling, per-scanline sprite evaluation, dot-accurate sprite-0
// hit (homebrew split-scroll relies on it). Clean-room from NESdev docs.
#pragma once

#include <cstdint>
#include <cstring>

#include "cart.hpp"

namespace famemu::nes {

class Ppu {
public:
    static constexpr int kWidth = 256, kHeight = 240;

    explicit Ppu(Cart& cart) : cart_(cart) { reset(); }

    void reset() {
        ctrl_ = mask_ = status_ = oam_addr_ = 0;
        v_ = t_ = 0; fine_x_ = 0; w_ = false; buffer_ = 0;
        scanline_ = 261; dot_ = 0; odd_frame_ = false;
        frame_ = 0; nmi_request_ = false;
        std::memset(oam_, 0, sizeof oam_);
        std::memset(vram_, 0, sizeof vram_);
        std::memset(palette_, 0, sizeof palette_);
        std::memset(fb_, 0, sizeof fb_);
    }

    // Advance one PPU dot (3 per CPU cycle).
    void tick();

    // CPU-visible registers $2000-$2007.
    uint8_t read_reg(uint16_t addr);
    void write_reg(uint16_t addr, uint8_t v);

    void oam_dma_write(uint8_t i, uint8_t v) { oam_[(oam_addr_ + i) & 0xFF] = v; }

    // Frame output: 256x240 NES color indices (0..63). Stable after vblank.
    const uint8_t* framebuffer() const { return fb_; }
    uint64_t frame_count() const { return frame_; }

    // NMI line: set at (241,1) when enabled; consumed by the system.
    bool take_nmi() { bool n = nmi_request_; nmi_request_ = false; return n; }

    // Save states: one field list serves both directions (state.hpp).
    template <class S>
    void serialize(S& s) {
        s.io(ctrl_); s.io(mask_); s.io(status_); s.io(oam_addr_);
        s.io(v_); s.io(t_); s.io(fine_x_); s.io(w_); s.io(buffer_);
        s.io(vram_); s.io(palette_); s.io(oam_);
        s.io(scanline_); s.io(dot_); s.io(odd_frame_); s.io(frame_);
        s.io(nmi_request_);
        s.io(nt_latch_); s.io(at_latch_); s.io(bg_lo_latch_); s.io(bg_hi_latch_);
        s.io(bg_shift_lo_); s.io(bg_shift_hi_);
        s.io(at_shift_lo_); s.io(at_shift_hi_);
        s.io(scan_sprites_); s.io(scan_count_);
        s.io(fb_);
    }

    // After loading a (possibly corrupt/tampered) save-state, clamp the render
    // counters into valid ranges. Otherwise a garbage signed `scanline_` / `dot_`
    // / `scan_count_` indexes `fb_` / `scan_sprites_` out of bounds on the next
    // frame — an OOB write/read that crashes (found by the corrupt-state fuzz
    // test). Valid states are always in range, so this is a no-op for them.
    void post_load() {
        if (scanline_ < 0 || scanline_ > 261) scanline_ = 0;
        if (dot_ < 0 || dot_ > 340) dot_ = 0;
        if (scan_count_ < 0 || scan_count_ > 8) scan_count_ = 0;
    }

private:
    Cart& cart_;

    // Registers / latches.
    uint8_t ctrl_, mask_, status_, oam_addr_;
    uint16_t v_, t_;       // current / temp VRAM address (Loopy)
    uint8_t fine_x_;
    bool w_;               // first/second write toggle
    uint8_t buffer_;       // $2007 read buffer

    // Memory.
    uint8_t vram_[0x800];    // two nametables, mirrored via cart
    uint8_t palette_[32];
    uint8_t oam_[256];

    // Timing.
    int scanline_, dot_;
    bool odd_frame_;
    uint64_t frame_;
    bool nmi_request_ = false;

    // Background pipeline.
    uint8_t nt_latch_ = 0, at_latch_ = 0, bg_lo_latch_ = 0, bg_hi_latch_ = 0;
    uint16_t bg_shift_lo_ = 0, bg_shift_hi_ = 0;
    uint16_t at_shift_lo_ = 0, at_shift_hi_ = 0;

    // Sprites for the current scanline (evaluated on the previous one).
    struct ScanSprite {
        uint8_t x, attr, pat_lo, pat_hi;
        bool is_sprite0;
    };
    ScanSprite scan_sprites_[8];
    int scan_count_ = 0;

    uint8_t fb_[kWidth * kHeight];

    // -- internal helpers ------------------------------------------------
    bool rendering() const { return mask_ & 0x18; }
    uint16_t mirror_nt(uint16_t addr) const;
    uint8_t vram_read(uint16_t addr);
    void vram_write(uint16_t addr, uint8_t val);
    void inc_coarse_x();
    void inc_y();
    void copy_x() { v_ = (v_ & ~0x041F) | (t_ & 0x041F); }
    void copy_y() { v_ = (v_ & ~0x7BE0) | (t_ & 0x7BE0); }
    void bg_fetch_step();
    void reload_shifters();
    void shift_bg();
    void evaluate_sprites(int next_line);
    void render_dot();
};

}  // namespace famemu::nes
