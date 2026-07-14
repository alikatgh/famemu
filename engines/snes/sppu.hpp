// famemu SNES engine — S-PPU scanline renderer, general-purpose scope:
// all BG modes 0-7 (incl. 8bpp, direct color, offset-per-tile in 2/4/6,
// Mode 7 with EXTBG), OBJ with per-line range/time limits + priority
// rotation, windows 1/2 with per-layer logic + color window, mosaic,
// color math (subscreen or fixed-color add/sub, clip-to-black), INIDISP
// brightness, VRAM/CGRAM/OAM read ports with prefetch, H/V counter
// latching, 256x224 RGB out.
//
// Modes 5/6 and pseudo-hires render true 512-wide frames (even subpixel =
// subscreen, odd = mainscreen, like hardware); the frame width switches to
// 512 when the first hires line renders, re-expanding earlier rows.
// Interlace (SETINI bits 0/1) samples the current field's lines.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class SPpu {
public:
    static constexpr int kWidth = 256, kHeight = 224;   // base resolution
    static constexpr int kMaxWidth = 512;

    SPpu() { reset(); }

    void reset() {
        std::memset(vram_, 0, sizeof vram_);
        std::memset(cgram_, 0, sizeof cgram_);
        std::memset(oam_, 0, sizeof oam_);
        std::memset(fb_, 0, sizeof fb_);
        inidisp_ = 0x80;  // force blank at power-on
        obsel_ = bgmode_ = mosaic_ = tm_ = ts_ = cgwsel_ = cgadsub_ = 0;
        coldata_r_ = coldata_g_ = coldata_b_ = 0;
        bg1sc_ = bg2sc_ = bg3sc_ = bg4sc_ = bg12nba_ = bg34nba_ = 0;
        for (int i = 0; i < 4; ++i) { bghofs_[i] = bgvofs_[i] = 0; }
        w12sel_ = w34sel_ = wobjsel_ = 0;
        wh_[0] = wh_[1] = wh_[2] = wh_[3] = 0;
        wbglog_ = wobjlog_ = 0;
        tmw_ = tsw_ = 0;
        setini_ = 0;
        scroll_latch_ = 0;
        m7sel_ = 0;
        m7a_ = m7b_ = m7c_ = m7d_ = 0;
        m7cx_ = m7cy_ = m7hofs_ = m7vofs_ = 0;
        m7_latch_ = 0;
        m7b_byte_ = 0;
        vmain_ = 0;
        vmadd_ = 0;
        vram_prefetch_ = 0;
        oamadd_reload_ = 0;
        oamadd_ = 0;
        oam_prio_rotate_ = false;
        oam_lo_latch_ = 0;
        cgadd_ = 0;
        cg_latch_ = false;
        cg_low_ = 0;
        cg_rd_latch_ = false;
        ophct_ = opvct_ = 0;
        hv_latched_ = false;
        oph_byte_ = opv_byte_ = false;
        stat77_ = 0x01;   // OBJ time/range flags + version
        field_ = false;
        ppu1_mdr_ = ppu2_mdr_ = 0;
    }

    // CPU-visible $21xx access (reg = low byte of $21xx).
    void write(uint8_t reg, uint8_t v);
    uint8_t read(uint8_t reg, uint8_t open_bus);

    void render_line(int y);                 // y in [0, 223] (whole line)
    // Mid-scanline ("dot-level") rendering: the system opens each visible
    // line, catches the raster up to the CPU's dot before any $21xx write,
    // and finishes the line after the CPU slice.
    void begin_line(int y);
    void catch_up(int dot) {                 // dot 0-339 -> pixel 0-255
        if (line_y_ < 0) return;
        int x = dot - 22;                    // ~22 dots of left border
        if (x <= line_dot_) return;
        if (x > 256) x = 256;
        render_segment(line_dot_, x);
        line_dot_ = x;
    }
    void finish_line();
    void frame_start();                      // line 0: field toggle
    void vblank_begin();                     // OAM address reload, flag clear
    void latch_counters(int h, int v) {      // $2137 / WRIO bit7
        ophct_ = static_cast<uint16_t>(h & 0x1FF);
        opvct_ = static_cast<uint16_t>(v & 0x1FF);
        hv_latched_ = true;
    }
    bool overscan() const { return setini_ & 0x04; }
    void set_pal(bool pal) { pal_ = pal; }

    const uint8_t* framebuffer() const { return fb_; }  // RGB888
    int width() const { return width_; }                 // 256 or 512
    // debug: color-math state
    void dbg_colormath(uint8_t* out8) const {
        out8[0] = cgadsub_; out8[1] = coldata_r_; out8[2] = coldata_g_; out8[3] = coldata_b_;
        out8[4] = ts_; out8[5] = cgwsel_; out8[6] = bg2sc_; out8[7] = bg12nba_;
    }

    template <class S>
    void serialize(S& s) {
        s.io(vram_); s.io(cgram_); s.io(oam_); s.io(fb_); s.io(width_);
        s.io(inidisp_); s.io(obsel_); s.io(bgmode_); s.io(mosaic_);
        s.io(tm_); s.io(ts_);
        s.io(cgwsel_); s.io(cgadsub_);
        s.io(coldata_r_); s.io(coldata_g_); s.io(coldata_b_);
        s.io(bg1sc_); s.io(bg2sc_); s.io(bg3sc_); s.io(bg4sc_);
        s.io(bg12nba_); s.io(bg34nba_);
        s.io(bghofs_); s.io(bgvofs_);
        s.io(w12sel_); s.io(w34sel_); s.io(wobjsel_); s.io(wh_);
        s.io(wbglog_); s.io(wobjlog_); s.io(tmw_); s.io(tsw_);
        s.io(setini_);
        s.io(m7sel_); s.io(m7a_); s.io(m7b_); s.io(m7c_); s.io(m7d_);
        s.io(m7cx_); s.io(m7cy_); s.io(m7hofs_); s.io(m7vofs_);
        s.io(m7_latch_); s.io(m7b_byte_);
        s.io(scroll_latch_); s.io(vmain_); s.io(vmadd_); s.io(vram_prefetch_);
        s.io(oamadd_reload_); s.io(oamadd_); s.io(oam_prio_rotate_);
        s.io(oam_lo_latch_);
        s.io(cgadd_); s.io(cg_latch_); s.io(cg_low_); s.io(cg_rd_latch_);
        s.io(ophct_); s.io(opvct_); s.io(hv_latched_);
        s.io(oph_byte_); s.io(opv_byte_);
        s.io(stat77_); s.io(field_);
        s.io(ppu1_mdr_); s.io(ppu2_mdr_);
    }

private:
    uint8_t vram_[0x10000];      // 64 KB (word-addressed as 32K words)
    uint8_t cgram_[512];         // 256 x BGR555
    uint8_t oam_[544];
    uint8_t fb_[kMaxWidth * kHeight * 3];
    int width_ = kWidth;

    uint8_t inidisp_, obsel_, bgmode_, mosaic_, tm_, ts_, cgwsel_, cgadsub_;
    uint8_t coldata_r_, coldata_g_, coldata_b_;
    uint8_t bg1sc_, bg2sc_, bg3sc_, bg4sc_, bg12nba_, bg34nba_;
    uint16_t bghofs_[4], bgvofs_[4];
    uint8_t w12sel_, w34sel_, wobjsel_;
    uint8_t wh_[4];                       // $2126-29 window 1/2 left/right
    uint8_t wbglog_, wobjlog_;
    uint8_t tmw_, tsw_;
    uint8_t setini_;
    uint8_t m7sel_;                       // $211A: flips + out-of-map mode
    int16_t m7a_, m7b_, m7c_, m7d_;       // $211B-$211E, 8.8 fixed
    int16_t m7cx_, m7cy_;                 // $211F/$2120, 13-bit signed
    int16_t m7hofs_, m7vofs_;             // $210D/$210E via the M7 latch
    uint8_t m7_latch_;
    uint8_t m7b_byte_;                    // last $211C byte, for MPY
    uint8_t scroll_latch_;
    uint8_t vmain_;
    uint16_t vmadd_;             // word address
    uint16_t vram_prefetch_;     // $2139/$213A read buffer
    uint16_t oamadd_reload_;     // $2102/03 (9-bit word address)
    uint16_t oamadd_;            // current byte address
    bool oam_prio_rotate_;       // $2103 bit 7
    uint8_t oam_lo_latch_;       // low-table pair write latch
    uint8_t cgadd_;
    bool cg_latch_;
    uint8_t cg_low_;
    bool cg_rd_latch_;
    uint16_t ophct_, opvct_;     // latched H/V counters
    bool hv_latched_;
    bool oph_byte_, opv_byte_;   // read-twice toggles
    uint8_t stat77_;
    bool field_;                 // STAT78 interlace field
    bool pal_ = false;           // STAT78 bit 4
    uint8_t ppu1_mdr_, ppu2_mdr_;

    // Per-line OBJ buffer (rebuilt by evaluate_objects()).
    struct ObjPix { uint8_t color_idx; uint8_t prio; bool opaque; };
    ObjPix obj_line_[kWidth];

    static int16_t sign13(uint16_t v) {  // 13-bit two's complement
        return static_cast<int16_t>((v & 0x1000) ? (v | 0xE000) : (v & 0x1FFF));
    }
    int vram_step() const {
        switch (vmain_ & 3) {
            case 0: return 1;
            case 1: return 32;
            default: return 128;
        }
    }
    uint16_t vram_remap(uint16_t a) const {  // VMAIN bits 2-3 address remap
        switch ((vmain_ >> 2) & 3) {
            default: return a;
            case 1: return static_cast<uint16_t>((a & 0xFF00) | ((a & 0x001F) << 3) | ((a >> 5) & 7));
            case 2: return static_cast<uint16_t>((a & 0xFE00) | ((a & 0x003F) << 3) | ((a >> 6) & 7));
            case 3: return static_cast<uint16_t>((a & 0xFC00) | ((a & 0x007F) << 3) | ((a >> 7) & 7));
        }
    }
    uint16_t vram_word(uint16_t waddr) const {
        return static_cast<uint16_t>(vram_[(waddr * 2) & 0xFFFF] |
                                     (vram_[(waddr * 2 + 1) & 0xFFFF] << 8));
    }
    uint16_t cg_color(int idx) const {
        return static_cast<uint16_t>(cgram_[idx * 2] | (cgram_[idx * 2 + 1] << 8));
    }

    // A resolved layer pixel: 15-bit color + source layer for color math.
    struct Pixel { uint16_t c15; uint8_t layer; bool valid; };
    static constexpr uint8_t kBG1 = 0, kBG2 = 1, kBG3 = 2, kBG4 = 3,
                             kOBJ = 4, kBACK = 5;

    // BG bit depth per mode (0 = BG absent in that mode).
    static const uint8_t kModeBpp[8][4];

    struct BgPix { uint8_t color_idx; uint8_t prio; bool opaque; uint8_t pal; };
    void fetch_bg_pixel(int bg, int x, int y, BgPix& out);  // x in dot space
    bool hires_mode() const {
        const int m = bgmode_ & 7;
        return m == 5 || m == 6 || (setini_ & 0x08);
    }
    void render_line_512(int y, uint8_t* row);
    void render_line_512_segment(int y, uint8_t* row, int a, int b);
    void expand_rows_to_512(int upto_y);
    void render_segment(int a, int b);       // pixels [a, b) of the open line
    int line_y_ = -1;                        // open line, -1 = none
    int line_dot_ = 0;                       // pixels already rendered
    void fetch_mode7_pixel(int bg, int x, int y, BgPix& out);
    void evaluate_objects(int y);
    bool window_state(int layer, int x) const;  // layer 0-3 BG, 4 OBJ, 5 MATH
    Pixel resolve_screen(uint8_t designation, uint8_t win_mask, int x,
                         int wx, int y, BgPix* bp, bool* bd);
    uint16_t bg_pixel_color(int bg, const BgPix& p) const;  // palette/direct
};

}  // namespace famemu::snes
