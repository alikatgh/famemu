// famemu SNES engine — SuperFX (GSU) coprocessor: full instruction set with
// the hardware's one-byte prefetch pipe (so branch delay slots behave), TO/
// FROM/WITH register prefixes, ALT1-3 modes, PLOT/RPIX bitmap plotting in
// 4/16/256-color modes, ROM buffer (R14), and the SNES-side register file at
// $3000-$301F/$3030-$303F. Game Pak RAM lives in the cart SRAM (banks 70/71).
//
// Clean-room from public documentation (fullsnes / GSU manual). Simplified:
// no instruction cache modeling (code reads go straight to ROM/RAM), no
// ROM/RAM bus arbitration against the S-CPU, uniform instruction timing.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class SnesSystem;

class SuperFx {
public:
    static constexpr size_t kStateBytes = 128;

    explicit SuperFx(SnesSystem& sys) : sys_(sys) { reset(); }

    void reset() {
        std::memset(r_, 0, sizeof r_);
        sfr_ = 0;
        pbr_ = rombr_ = rambr_ = 0;
        cbr_ = 0;
        scbr_ = scmr_ = clsr_ = cfgr_ = bramr_ = 0;
        colr_ = por_ = 0;
        romdr_ = 0;
        pipe_ = 0x01;  // NOP
        sreg_ = dreg_ = 0;
        ramaddr_ = 0;
        irq_ = false;
    }

    // ---- S-CPU visibility (returns -1 / false when not GSU-mapped) --------
    int snes_read(uint32_t a24);
    bool snes_write(uint32_t a24, uint8_t v);
    bool irq_pending() const { return irq_ && !(cfgr_ & 0x80); }

    void run_line();

    template <class S>
    void serialize(S& s) {
        s.io(r_); s.io(sfr_); s.io(pbr_); s.io(rombr_); s.io(rambr_);
        s.io(cbr_); s.io(scbr_); s.io(scmr_); s.io(clsr_); s.io(cfgr_);
        s.io(bramr_); s.io(colr_); s.io(por_); s.io(romdr_); s.io(pipe_);
        s.io(sreg_); s.io(dreg_); s.io(ramaddr_); s.io(irq_);
    }

private:
    SnesSystem& sys_;
    uint16_t r_[16];
    uint16_t sfr_;
    uint8_t pbr_, rombr_, rambr_;
    uint16_t cbr_;
    uint8_t scbr_, scmr_, clsr_, cfgr_, bramr_;
    uint8_t colr_, por_;
    uint8_t romdr_;
    uint8_t pipe_;
    uint8_t sreg_, dreg_;
    uint16_t ramaddr_;             // last RAM address (for SBK)
    bool irq_;

    // SFR flags
    static constexpr uint16_t Z = 0x0002, CY = 0x0004, S = 0x0008,
                              OV = 0x0010, GO = 0x0020, ALT1 = 0x0100,
                              ALT2 = 0x0200, B = 0x1000;

    uint8_t mem_read(uint8_t bank, uint16_t a) const;   // ROM/RAM (impl)
    uint8_t* ram() const;                               // Game Pak RAM
    uint32_t ram_mask() const;

    uint8_t ram_rd(uint16_t a) const {
        return ram()[((static_cast<uint32_t>(rambr_ & 1) << 16) | a) & ram_mask()];
    }
    void ram_wr(uint16_t a, uint8_t v) {
        ram()[((static_cast<uint32_t>(rambr_ & 1) << 16) | a) & ram_mask()] = v;
    }

    uint8_t pipe() {
        const uint8_t v = pipe_;
        pipe_ = mem_read(pbr_, r_[15]);
        r_[15] = static_cast<uint16_t>(r_[15] + 1);
        return v;
    }

    void rombuf_update() { romdr_ = mem_read(rombr_, r_[14]); }

    uint16_t& sr() { return r_[sreg_]; }
    void set_dreg(uint16_t v) {
        r_[dreg_] = v;
        if (dreg_ == 14) rombuf_update();
        // dreg==15: branch target; pipe already holds the delay slot.
    }
    void flags_zs(uint16_t v) {
        sfr_ = static_cast<uint16_t>((sfr_ & ~(Z | S)) | (v ? 0 : Z) |
                                     ((v & 0x8000) ? S : 0));
    }
    void regs_reset() {
        sreg_ = dreg_ = 0;
        sfr_ &= static_cast<uint16_t>(~(B | ALT1 | ALT2));
    }
    int alt() const { return ((sfr_ & ALT1) ? 1 : 0) | ((sfr_ & ALT2) ? 2 : 0); }

    // ---- PLOT/RPIX bitmap addressing ---------------------------------------
    int screen_bpp() const {
        switch (scmr_ & 3) { case 0: return 2; case 1: return 4; default: return 8; }
    }
    int screen_tiles_h() const {
        const int h = ((scmr_ >> 5) & 1) | ((scmr_ >> 2) & 2);  // bits 5,3
        switch (h) { case 0: return 16; case 1: return 20; default: return 24; }
    }
    uint32_t char_base(int x, int y) const {
        const int bpp = screen_bpp();
        const uint32_t tile =
            static_cast<uint32_t>((x >> 3) * screen_tiles_h() + (y >> 3));
        return (static_cast<uint32_t>(scbr_) << 10) + tile * (bpp * 8) +
               ((y & 7) * 2);
    }
    void plot_pixel(int x, int y, uint8_t color) {
        const int bpp = screen_bpp();
        const uint32_t base = char_base(x, y);
        const int bit = 7 - (x & 7);
        for (int p = 0; p < bpp; ++p) {
            const uint32_t a = base + (p >> 1) * 16 + (p & 1);
            uint8_t b = ram()[a & ram_mask()];
            b = static_cast<uint8_t>((b & ~(1 << bit)) |
                                     (((color >> p) & 1) << bit));
            ram()[a & ram_mask()] = b;
        }
    }
    uint8_t read_pixel(int x, int y) const {
        const int bpp = screen_bpp();
        const uint32_t base = char_base(x, y);
        const int bit = 7 - (x & 7);
        uint8_t c = 0;
        for (int p = 0; p < bpp; ++p) {
            const uint32_t a = base + (p >> 1) * 16 + (p & 1);
            c |= static_cast<uint8_t>(((ram()[a & ram_mask()] >> bit) & 1) << p);
        }
        return c;
    }

    void step();
};

}  // namespace famemu::snes
