// famemu SNES engine — SPC7110 context-modeled arithmetic decompressor.
//
// Clean port of the public SPC7110 emulator by byuu & neviksti (v0.03,
// 2008-08-10), released under an ISC-style permissive licence:
//
//   Permission to use, copy, modify, and/or distribute this software for any
//   purpose with or without fee is hereby granted, provided that the above
//   copyright notice and this permission notice appear in all copies.
//
// The original method-local `static` decoder state is hoisted to instance
// members here so each machine (and every save state) is independent; the
// algorithm, evolution/context tables, and morton de-interleave are
// unchanged from the reverse-engineered reference.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class Spc7110Decomp {
public:
    Spc7110Decomp() { init_morton(); reset(); }

    void set_rom(const uint8_t* rom, uint32_t size) { rom_ = rom; rom_size_ = size; }

    // Data-ROM addressing (the data ROM sits at cart offset $100000).
    uint32_t datarom_size() const {
        return rom_size_ > 0x500000 ? rom_size_ - 0x200000 : rom_size_ - 0x100000;
    }
    uint32_t datarom_addr(uint32_t addr) const {
        const uint32_t size = datarom_size() ? datarom_size() : 1;
        return 0x100000 + (addr % size);
    }
    uint8_t rom_at(uint32_t a) const {
        return (rom_ && rom_size_) ? rom_[a % rom_size_] : 0;
    }

    void reset() {
        // Mode 3 is invalid → the port returns 0x00 until a real decode starts.
        mode_ = 3;
        rd_ = wr_ = len_ = 0;
        offset_ = 0;
        for (int i = 0; i < 32; ++i) { ctx_[i].index = 0; ctx_[i].invert = 0; }
        std::memset(buf_, 0, sizeof buf_);
        val_ = in_ = span_ = 0; in_count_ = 0;
        out_ = out0_ = out1_ = inverts_ = lps_ = 0;
        for (int i = 0; i < 16; ++i) pixelorder_[i] = i;
        std::memset(bitplane_, 0, sizeof bitplane_);
        bp_index_ = 0;
    }

    void init(unsigned mode, unsigned offset, unsigned index) {
        mode_ = mode;
        offset_ = offset;
        rd_ = wr_ = len_ = 0;
        for (int i = 0; i < 32; ++i) { ctx_[i].index = 0; ctx_[i].invert = 0; }
        for (int i = 0; i < 16; ++i) pixelorder_[i] = i;
        bp_index_ = 0;
        switch (mode_) {
            case 0: mode0(true); break;
            case 1: mode1(true); break;
            case 2: mode2(true); break;
            default: break;
        }
        while (index--) read();
    }

    uint8_t read() {
        if (len_ == 0) {
            switch (mode_) {
                case 0: mode0(false); break;
                case 1: mode1(false); break;
                case 2: mode2(false); break;
                default: return 0x00;
            }
        }
        const uint8_t data = buf_[rd_++];
        rd_ &= kBuf - 1;
        --len_;
        return data;
    }

    template <class S>
    void serialize(S& s) {
        s.io(mode_); s.io(offset_); s.io(rd_); s.io(wr_); s.io(len_);
        s.io(buf_);
        for (int i = 0; i < 32; ++i) { s.io(ctx_[i].index); s.io(ctx_[i].invert); }
        s.io(val_); s.io(in_); s.io(span_); s.io(in_count_);
        s.io(out_); s.io(out0_); s.io(out1_); s.io(inverts_); s.io(lps_);
        s.io(pixelorder_); s.io(bitplane_); s.io(bp_index_);
    }

private:
    static constexpr int kBuf = 64;                 // decomp_buffer_size

    const uint8_t* rom_ = nullptr;
    uint32_t rom_size_ = 0;

    struct Ctx { uint8_t index; uint8_t invert; };
    Ctx ctx_[32];

    unsigned mode_;
    uint32_t offset_;
    uint8_t buf_[kBuf];
    unsigned rd_, wr_, len_;

    // arithmetic-decoder state (was method-static in the reference)
    uint8_t val_, in_, span_;
    int in_count_;
    uint32_t out_, out0_, out1_;
    int inverts_, lps_;
    unsigned pixelorder_[16];
    uint8_t bitplane_[16];
    uint8_t bp_index_;

    // ---- data-ROM sequential fetch ----------------------------------------
    uint8_t dataread() {
        const uint32_t size = datarom_size() ? datarom_size() : 1;
        while (offset_ >= size) offset_ -= size;
        return rom_at(0x100000 + offset_++);
    }
    void wbuf(uint8_t d) { buf_[wr_++] = d; wr_ &= kBuf - 1; ++len_; }

    // ---- evolution / context tables (verbatim from the reference) ---------
    static const uint8_t kEvo[53][4];               // {prob, nextlps, nextmps, toggle}
    static const uint8_t kMode2Ctx[32][2];

    uint8_t probability(unsigned n)   const { return kEvo[ctx_[n].index][0]; }
    uint8_t next_lps(unsigned n)      const { return kEvo[ctx_[n].index][1]; }
    uint8_t next_mps(unsigned n)      const { return kEvo[ctx_[n].index][2]; }
    uint8_t toggle_invert(unsigned n) const { return kEvo[ctx_[n].index][3]; }

    // ---- morton de-interleave (built once) --------------------------------
    static uint16_t morton16_[2][256];
    static uint32_t morton32_[4][256];
    static bool morton_ready_;
    static void init_morton() {
        if (morton_ready_) return;
        for (unsigned i = 0; i < 256; ++i) {
            #define MAP(x, y) static_cast<uint32_t>(((i >> (x)) & 1) << (y))
            morton16_[1][i] = static_cast<uint16_t>(
                MAP(7,15)+MAP(6,7)+MAP(5,14)+MAP(4,6)+MAP(3,13)+MAP(2,5)+MAP(1,12)+MAP(0,4));
            morton16_[0][i] = static_cast<uint16_t>(
                MAP(7,11)+MAP(6,3)+MAP(5,10)+MAP(4,2)+MAP(3,9)+MAP(2,1)+MAP(1,8)+MAP(0,0));
            morton32_[3][i] = MAP(7,31)+MAP(6,23)+MAP(5,15)+MAP(4,7)+MAP(3,30)+MAP(2,22)+MAP(1,14)+MAP(0,6);
            morton32_[2][i] = MAP(7,29)+MAP(6,21)+MAP(5,13)+MAP(4,5)+MAP(3,28)+MAP(2,20)+MAP(1,12)+MAP(0,4);
            morton32_[1][i] = MAP(7,27)+MAP(6,19)+MAP(5,11)+MAP(4,3)+MAP(3,26)+MAP(2,18)+MAP(1,10)+MAP(0,2);
            morton32_[0][i] = MAP(7,25)+MAP(6,17)+MAP(5,9)+MAP(4,1)+MAP(3,24)+MAP(2,16)+MAP(1,8)+MAP(0,0);
            #undef MAP
        }
        morton_ready_ = true;
    }
    static uint16_t morton_2x8(uint32_t d) {
        return static_cast<uint16_t>(morton16_[0][d & 255] + morton16_[1][(d >> 8) & 255]);
    }
    static uint32_t morton_4x8(uint32_t d) {
        return morton32_[0][d & 255] + morton32_[1][(d >> 8) & 255]
             + morton32_[2][(d >> 16) & 255] + morton32_[3][(d >> 24) & 255];
    }

    void mode0(bool init);
    void mode1(bool init);
    void mode2(bool init);
};

// ---- decoder modes (ports of byuu/neviksti mode0/1/2) ----------------------

inline void Spc7110Decomp::mode0(bool init) {
    if (init) {
        out_ = inverts_ = lps_ = 0;
        span_ = 0xFF;
        val_ = dataread();
        in_ = dataread();
        in_count_ = 8;
        return;
    }
    while (len_ < (kBuf >> 1)) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint8_t mask = static_cast<uint8_t>((1 << (bit & 3)) - 1);
            uint8_t con = static_cast<uint8_t>(mask + ((inverts_ & mask) ^ (lps_ & mask)));
            if (bit > 3) con += 15;

            const unsigned prob = probability(con);
            const unsigned mps = (((out_ >> 15) & 1) ^ ctx_[con].invert);

            unsigned flag_lps;
            if (val_ <= span_ - prob) { span_ = span_ - prob; out_ = (out_ << 1) + mps; flag_lps = 0; }
            else { val_ = val_ - (span_ - (prob - 1)); span_ = prob - 1; out_ = (out_ << 1) + 1 - mps; flag_lps = 1; }

            unsigned shift = 0;
            while (span_ < 0x7F) {
                ++shift;
                span_ = static_cast<uint8_t>((span_ << 1) + 1);
                val_ = static_cast<uint8_t>((val_ << 1) + (in_ >> 7));
                in_ <<= 1;
                if (--in_count_ == 0) { in_ = dataread(); in_count_ = 8; }
            }

            lps_ = (lps_ << 1) + static_cast<int>(flag_lps);
            inverts_ = (inverts_ << 1) + ctx_[con].invert;

            if (flag_lps & toggle_invert(con)) ctx_[con].invert ^= 1;
            if (flag_lps) ctx_[con].index = next_lps(con);
            else if (shift) ctx_[con].index = next_mps(con);
        }
        wbuf(static_cast<uint8_t>(out_));
    }
}

inline void Spc7110Decomp::mode1(bool init) {
    if (init) {
        for (unsigned i = 0; i < 4; ++i) pixelorder_[i] = i;
        out_ = inverts_ = lps_ = 0;
        span_ = 0xFF;
        val_ = dataread();
        in_ = dataread();
        in_count_ = 8;
        return;
    }
    unsigned realorder[4];
    while (len_ < (kBuf >> 1)) {
        for (unsigned pixel = 0; pixel < 8; ++pixel) {
            const unsigned a = (out_ >> 2) & 3;
            const unsigned b0 = (out_ >> 14) & 3;
            const unsigned c = (out_ >> 16) & 3;
            unsigned con = (a == b0) ? (b0 != c) : (b0 == c) ? 2 : 4 - (a == c);

            unsigned m, n;
            for (m = 0; m < 4; ++m) if (pixelorder_[m] == a) break;
            for (n = m; n > 0; --n) pixelorder_[n] = pixelorder_[n - 1];
            pixelorder_[0] = a;

            for (m = 0; m < 4; ++m) realorder[m] = pixelorder_[m];
            for (m = 0; m < 4; ++m) if (realorder[m] == c) break;
            for (n = m; n > 0; --n) realorder[n] = realorder[n - 1];
            realorder[0] = c;
            for (m = 0; m < 4; ++m) if (realorder[m] == b0) break;
            for (n = m; n > 0; --n) realorder[n] = realorder[n - 1];
            realorder[0] = b0;
            for (m = 0; m < 4; ++m) if (realorder[m] == a) break;
            for (n = m; n > 0; --n) realorder[n] = realorder[n - 1];
            realorder[0] = a;

            for (unsigned bit = 0; bit < 2; ++bit) {
                const unsigned prob = probability(con);
                unsigned flag_lps;
                if (val_ <= span_ - prob) { span_ = span_ - prob; flag_lps = 0; }
                else { val_ = val_ - (span_ - (prob - 1)); span_ = prob - 1; flag_lps = 1; }

                unsigned shift = 0;
                while (span_ < 0x7F) {
                    ++shift;
                    span_ = static_cast<uint8_t>((span_ << 1) + 1);
                    val_ = static_cast<uint8_t>((val_ << 1) + (in_ >> 7));
                    in_ <<= 1;
                    if (--in_count_ == 0) { in_ = dataread(); in_count_ = 8; }
                }

                lps_ = (lps_ << 1) + static_cast<int>(flag_lps);
                inverts_ = (inverts_ << 1) + ctx_[con].invert;

                if (flag_lps & toggle_invert(con)) ctx_[con].invert ^= 1;
                if (flag_lps) ctx_[con].index = next_lps(con);
                else if (shift) ctx_[con].index = next_mps(con);

                con = 5 + (con << 1) + ((lps_ ^ inverts_) & 1);
            }

            const unsigned b = realorder[(lps_ ^ inverts_) & 3];
            out_ = (out_ << 2) + b;
        }
        const uint16_t data = morton_2x8(out_);
        wbuf(static_cast<uint8_t>(data >> 8));
        wbuf(static_cast<uint8_t>(data >> 0));
    }
}

inline void Spc7110Decomp::mode2(bool init) {
    if (init) {
        for (unsigned i = 0; i < 16; ++i) pixelorder_[i] = i;
        bp_index_ = 0;
        out0_ = out1_ = inverts_ = lps_ = 0;
        span_ = 0xFF;
        val_ = dataread();
        in_ = dataread();
        in_count_ = 8;
        return;
    }
    unsigned realorder[16];
    while (len_ < (kBuf >> 1)) {
        for (unsigned pixel = 0; pixel < 8; ++pixel) {
            const unsigned a = (out0_ >> 0) & 15;
            const unsigned b0 = (out0_ >> 28) & 15;
            const unsigned c = (out1_ >> 0) & 15;
            unsigned con = 0;
            const unsigned refcon = (a == b0) ? (b0 != c) : (b0 == c) ? 2 : 4 - (a == c);

            unsigned m, n;
            for (m = 0; m < 16; ++m) if (pixelorder_[m] == a) break;
            for (n = m; n > 0; --n) pixelorder_[n] = pixelorder_[n - 1];
            pixelorder_[0] = a;

            for (m = 0; m < 16; ++m) realorder[m] = pixelorder_[m];
            for (m = 0; m < 16; ++m) if (realorder[m] == c) break;
            for (n = m; n > 0; --n) realorder[n] = realorder[n - 1];
            realorder[0] = c;
            for (m = 0; m < 16; ++m) if (realorder[m] == b0) break;
            for (n = m; n > 0; --n) realorder[n] = realorder[n - 1];
            realorder[0] = b0;
            for (m = 0; m < 16; ++m) if (realorder[m] == a) break;
            for (n = m; n > 0; --n) realorder[n] = realorder[n - 1];
            realorder[0] = a;

            for (unsigned bit = 0; bit < 4; ++bit) {
                const unsigned prob = probability(con);
                unsigned flag_lps;
                if (val_ <= span_ - prob) { span_ = span_ - prob; flag_lps = 0; }
                else { val_ = val_ - (span_ - (prob - 1)); span_ = prob - 1; flag_lps = 1; }

                unsigned shift = 0;
                while (span_ < 0x7F) {
                    ++shift;
                    span_ = static_cast<uint8_t>((span_ << 1) + 1);
                    val_ = static_cast<uint8_t>((val_ << 1) + (in_ >> 7));
                    in_ <<= 1;
                    if (--in_count_ == 0) { in_ = dataread(); in_count_ = 8; }
                }

                lps_ = (lps_ << 1) + static_cast<int>(flag_lps);
                const unsigned invertbit = ctx_[con].invert;
                inverts_ = (inverts_ << 1) + static_cast<int>(invertbit);

                if (flag_lps & toggle_invert(con)) ctx_[con].invert ^= 1;
                if (flag_lps) ctx_[con].index = next_lps(con);
                else if (shift) ctx_[con].index = next_mps(con);

                con = kMode2Ctx[con][flag_lps ^ invertbit] + (con == 1 ? refcon : 0);
            }

            const unsigned b = realorder[(lps_ ^ inverts_) & 0x0F];
            out1_ = (out1_ << 4) + ((out0_ >> 28) & 0x0F);
            out0_ = (out0_ << 4) + b;
        }
        const uint32_t data = morton_4x8(out0_);
        wbuf(static_cast<uint8_t>(data >> 24));
        wbuf(static_cast<uint8_t>(data >> 16));
        bitplane_[bp_index_++] = static_cast<uint8_t>(data >> 8);
        bitplane_[bp_index_++] = static_cast<uint8_t>(data >> 0);
        if (bp_index_ == 16) {
            for (unsigned i = 0; i < 16; ++i) wbuf(bitplane_[i]);
            bp_index_ = 0;
        }
    }
}

// ---- tables (verbatim from byuu/neviksti) ----------------------------------
inline const uint8_t Spc7110Decomp::kEvo[53][4] = {
    {0x5a,1,1,1},{0x25,6,2,0},{0x11,8,3,0},{0x08,10,4,0},{0x03,12,5,0},{0x01,15,5,0},
    {0x5a,7,7,1},{0x3f,19,8,0},{0x2c,21,9,0},{0x20,22,10,0},{0x17,23,11,0},{0x11,25,12,0},
    {0x0c,26,13,0},{0x09,28,14,0},{0x07,29,15,0},{0x05,31,16,0},{0x04,32,17,0},{0x03,34,18,0},
    {0x02,35,5,0},
    {0x5a,20,20,1},{0x48,39,21,0},{0x3a,40,22,0},{0x2e,42,23,0},{0x26,44,24,0},{0x1f,45,25,0},
    {0x19,46,26,0},{0x15,25,27,0},{0x11,26,28,0},{0x0e,26,29,0},{0x0b,27,30,0},{0x09,28,31,0},
    {0x08,29,32,0},{0x07,30,33,0},{0x05,31,34,0},{0x04,33,35,0},{0x04,33,36,0},{0x03,34,37,0},
    {0x02,35,38,0},{0x02,36,5,0},
    {0x58,39,40,1},{0x4d,47,41,0},{0x43,48,42,0},{0x3b,49,43,0},{0x34,50,44,0},{0x2e,51,45,0},
    {0x29,44,46,0},{0x25,45,24,0},
    {0x56,47,48,1},{0x4f,47,49,0},{0x47,48,50,0},{0x41,49,51,0},{0x3c,50,52,0},{0x37,51,43,0},
};

inline const uint8_t Spc7110Decomp::kMode2Ctx[32][2] = {
    {1,2},{3,8},{13,14},{15,16},{17,18},{19,20},{21,22},{23,24},{25,26},{25,26},
    {25,26},{25,26},{25,26},{27,28},{29,30},{31,31},{31,31},{31,31},{31,31},{31,31},
    {31,31},{31,31},{31,31},{31,31},{31,31},{31,31},{31,31},{31,31},{31,31},{31,31},
    {31,31},{31,31},
};

inline uint16_t Spc7110Decomp::morton16_[2][256];
inline uint32_t Spc7110Decomp::morton32_[4][256];
inline bool Spc7110Decomp::morton_ready_ = false;

}  // namespace famemu::snes
