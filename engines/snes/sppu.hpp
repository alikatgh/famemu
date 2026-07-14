// famemu SNES engine — S-PPU scanline renderer, scoped to KORA's footprint
// first (docs/PLATFORM.md): Mode 1 (BG1/BG2 4bpp + BG3 2bpp w/ priority + OBJ),
// color math (subscreen or fixed-color add/sub — KORA subtracts BG2 cloud
// shadows via TS/CGWSEL, falling back to COLDATA for day/night), INIDISP
// brightness, 256x224 out. Grows toward Mode 7 with the crossing scene.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class SPpu {
public:
    static constexpr int kWidth = 256, kHeight = 224;

    SPpu() { reset(); }

    void reset() {
        std::memset(vram_, 0, sizeof vram_);
        std::memset(cgram_, 0, sizeof cgram_);
        std::memset(oam_, 0, sizeof oam_);
        std::memset(fb_, 0, sizeof fb_);
        inidisp_ = 0x80;  // force blank at power-on
        obsel_ = bgmode_ = tm_ = ts_ = cgwsel_ = cgadsub_ = 0;
        coldata_r_ = coldata_g_ = coldata_b_ = 0;
        bg1sc_ = bg2sc_ = bg3sc_ = bg12nba_ = bg34nba_ = 0;
        bg1hofs_ = bg1vofs_ = bg2hofs_ = bg2vofs_ = bg3hofs_ = bg3vofs_ = 0;
        scroll_latch_ = 0;
        vmain_ = 0;
        vmadd_ = 0;
        oamadd_ = 0;
        cgadd_ = 0;
        cg_latch_ = false;
        cg_low_ = 0;
    }

    // CPU-visible $21xx writes (subset; unknown regs are accepted silently).
    void write(uint8_t reg, uint8_t v);
    uint8_t read(uint8_t reg);

    void render_line(int y);                 // y in [0, 223]
    const uint8_t* framebuffer() const { return fb_; }  // RGB888
    // debug: color-math state
    void dbg_colormath(uint8_t* out8) const {
        out8[0] = cgadsub_; out8[1] = coldata_r_; out8[2] = coldata_g_; out8[3] = coldata_b_;
        out8[4] = ts_; out8[5] = cgwsel_; out8[6] = bg2sc_; out8[7] = bg12nba_;
    }

    template <class S>
    void serialize(S& s) {
        s.io(vram_); s.io(cgram_); s.io(oam_); s.io(fb_);
        s.io(inidisp_); s.io(obsel_); s.io(bgmode_); s.io(tm_); s.io(ts_);
        s.io(cgwsel_); s.io(cgadsub_);
        s.io(coldata_r_); s.io(coldata_g_); s.io(coldata_b_);
        s.io(bg1sc_); s.io(bg2sc_); s.io(bg3sc_); s.io(bg12nba_); s.io(bg34nba_);
        s.io(bg1hofs_); s.io(bg1vofs_); s.io(bg2hofs_); s.io(bg2vofs_);
        s.io(bg3hofs_); s.io(bg3vofs_);
        s.io(scroll_latch_); s.io(vmain_); s.io(vmadd_); s.io(oamadd_);
        s.io(cgadd_); s.io(cg_latch_); s.io(cg_low_);
    }

private:
    uint8_t vram_[0x10000];      // 64 KB (word-addressed as 32K words)
    uint8_t cgram_[512];         // 256 x BGR555
    uint8_t oam_[544];
    uint8_t fb_[kWidth * kHeight * 3];

    uint8_t inidisp_, obsel_, bgmode_, tm_, ts_, cgwsel_, cgadsub_;
    uint8_t coldata_r_, coldata_g_, coldata_b_;
    uint8_t bg1sc_, bg2sc_, bg3sc_, bg12nba_, bg34nba_;
    uint16_t bg1hofs_, bg1vofs_, bg2hofs_, bg2vofs_, bg3hofs_, bg3vofs_;
    uint8_t scroll_latch_;
    uint8_t vmain_;
    uint16_t vmadd_;             // word address
    uint16_t oamadd_;
    uint8_t cgadd_;
    bool cg_latch_;
    uint8_t cg_low_;

    int vram_step() const {
        switch (vmain_ & 3) {
            case 0: return 1;
            case 1: return 32;
            default: return 128;
        }
    }
    uint16_t vram_word(uint16_t waddr) const {
        return static_cast<uint16_t>(vram_[(waddr * 2) & 0xFFFF] |
                                     (vram_[(waddr * 2 + 1) & 0xFFFF] << 8));
    }
    uint16_t cg_color(int idx) const {
        return static_cast<uint16_t>(cgram_[idx * 2] | (cgram_[idx * 2 + 1] << 8));
    }

    struct Pixel { uint8_t color_idx; uint8_t layer; uint8_t prio; };
    // layer ids for color-math selection
    static constexpr uint8_t kBG1 = 0, kBG2 = 1, kBG3 = 2, kOBJ = 4, kBACK = 5;

    void fetch_bg_pixel(int bg, int x, int y, Pixel& out);   // bg: 0=BG1 1=BG2 2=BG3
    bool fetch_obj_pixel(int x, int y, Pixel& out);
    Pixel resolve_screen(uint8_t mask, int x, int y);        // TM/TS designation
};

}  // namespace famemu::snes
