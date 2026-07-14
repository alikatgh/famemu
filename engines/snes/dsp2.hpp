// famemu SNES engine — DSP-2 HLE (Dungeon Master). Behavior per the public
// reverse-engineering of the chip's six commands: 4bpp bitplane conversion,
// transparency overlay, bitmap reverse, 16x16 multiply, and bitmap scaling
// with the hardware's fixed-point stepping.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class Dsp2 {
public:
    Dsp2() { reset(); }

    void reset() {
        waiting_ = true;
        cmd_ = 0;
        in_count_ = in_index_ = out_count_ = out_index_ = 0;
        op05_len_ = op06_len_ = 0;
        op05_transparent_ = 0;
        op0d_in_len_ = op0d_out_len_ = 0;
        op0d_has_len_ = false;
        std::memset(params_, 0, sizeof params_);
        std::memset(out_, 0, sizeof out_);
    }

    uint8_t read_dr() {
        if (!out_count_) return 0xFF;
        const uint8_t t = out_[out_index_++];
        if (out_index_ >= out_count_) out_count_ = 0;
        return t;
    }
    uint8_t read_sr() const { return 0x80; }

    void write_dr(uint8_t byte) {
        if (waiting_) {
            cmd_ = byte;
            in_index_ = 0;
            waiting_ = false;
            switch (byte) {
                case 0x01: in_count_ = 32; break;
                case 0x03: in_count_ = 1; break;
                case 0x05: in_count_ = 1; break;
                case 0x06: in_count_ = 1; break;
                case 0x09: in_count_ = 4; break;
                case 0x0D: in_count_ = 2; op0d_has_len_ = false; break;
                default: in_count_ = 0; waiting_ = true; break;
            }
            return;
        }
        if (in_index_ < static_cast<int>(sizeof params_))
            params_[in_index_] = byte;
        ++in_index_;
        if (in_index_ < in_count_) return;
        waiting_ = true;
        out_index_ = 0;
        switch (cmd_) {
            case 0x01: op01(); out_count_ = 32; break;
            case 0x03: op05_transparent_ = params_[0]; break;
            case 0x05:
                if (op05_len_) {
                    op05();
                    out_count_ = op05_len_;
                    op05_len_ = 0;
                } else {
                    op05_len_ = params_[0];
                    in_index_ = 0;
                    in_count_ = 2 * op05_len_;
                    if (op05_len_) waiting_ = false;
                }
                break;
            case 0x06:
                if (op06_len_) {
                    op06();
                    out_count_ = op06_len_;
                    op06_len_ = 0;
                } else {
                    op06_len_ = params_[0];
                    in_index_ = 0;
                    in_count_ = op06_len_;
                    if (op06_len_) waiting_ = false;
                }
                break;
            case 0x09: {
                const uint32_t p =
                    static_cast<uint32_t>(params_[0] | (params_[1] << 8)) *
                    static_cast<uint32_t>(params_[2] | (params_[3] << 8));
                out_[0] = static_cast<uint8_t>(p);
                out_[1] = static_cast<uint8_t>(p >> 8);
                out_[2] = static_cast<uint8_t>(p >> 16);
                out_[3] = static_cast<uint8_t>(p >> 24);
                out_count_ = 4;
                break;
            }
            case 0x0D:
                if (op0d_has_len_) {
                    op0d();
                    out_count_ = op0d_out_len_;
                    op0d_has_len_ = false;
                } else {
                    op0d_in_len_ = params_[0];
                    op0d_out_len_ = params_[1];
                    in_index_ = 0;
                    in_count_ = (op0d_in_len_ + 1) >> 1;
                    op0d_has_len_ = true;
                    if (in_count_) waiting_ = false;
                }
                break;
            default: break;
        }
    }

    template <class S>
    void serialize(S& s) {
        s.io(waiting_); s.io(cmd_); s.io(in_count_); s.io(in_index_);
        s.io(out_count_); s.io(out_index_);
        s.io(op05_len_); s.io(op05_transparent_); s.io(op06_len_);
        s.io(op0d_in_len_); s.io(op0d_out_len_); s.io(op0d_has_len_);
        s.io(params_); s.io(out_);
    }

private:
    bool waiting_;
    uint8_t cmd_;
    int in_count_, in_index_, out_count_, out_index_;
    int op05_len_, op06_len_;
    uint8_t op05_transparent_;
    int op0d_in_len_, op0d_out_len_;
    bool op0d_has_len_;
    uint8_t params_[512];
    uint8_t out_[512];

    void op01() {  // 4bpp packed-pixel column to bitplane conversion
        const uint8_t* p1 = params_;
        uint8_t* p2a = out_;
        uint8_t* p2b = out_ + 16;
        for (int j = 0; j < 8; ++j) {
            const uint8_t c0 = *p1++, c1 = *p1++, c2 = *p1++, c3 = *p1++;
            *p2a++ = static_cast<uint8_t>(
                (c0 & 0x10) << 3 | (c0 & 0x01) << 6 | (c1 & 0x10) << 1 |
                (c1 & 0x01) << 4 | (c2 & 0x10) >> 1 | (c2 & 0x01) << 2 |
                (c3 & 0x10) >> 3 | (c3 & 0x01));
            *p2a++ = static_cast<uint8_t>(
                (c0 & 0x20) << 2 | (c0 & 0x02) << 5 | (c1 & 0x20) |
                (c1 & 0x02) << 3 | (c2 & 0x20) >> 2 | (c2 & 0x02) << 1 |
                (c3 & 0x20) >> 4 | (c3 & 0x02) >> 1);
            *p2b++ = static_cast<uint8_t>(
                (c0 & 0x40) << 1 | (c0 & 0x04) << 4 | (c1 & 0x40) >> 1 |
                (c1 & 0x04) << 2 | (c2 & 0x40) >> 3 | (c2 & 0x04) |
                (c3 & 0x40) >> 5 | (c3 & 0x04) >> 2);
            *p2b++ = static_cast<uint8_t>(
                (c0 & 0x80) | (c0 & 0x08) << 3 | (c1 & 0x80) >> 2 |
                (c1 & 0x08) << 1 | (c2 & 0x80) >> 4 | (c2 & 0x08) >> 1 |
                (c3 & 0x80) >> 6 | (c3 & 0x08) >> 3);
        }
    }

    void op05() {  // overlay bitmap 2 over bitmap 1 with transparent colour
        const uint8_t color = op05_transparent_ & 0x0F;
        const uint8_t* p1 = params_;
        const uint8_t* p2 = params_ + op05_len_;
        uint8_t* p3 = out_;
        for (int n = 0; n < op05_len_; ++n) {
            const uint8_t c1 = *p1++, c2 = *p2++;
            *p3++ = static_cast<uint8_t>(
                (((c2 >> 4) == color) ? (c1 & 0xF0) : (c2 & 0xF0)) |
                (((c2 & 0x0F) == color) ? (c1 & 0x0F) : (c2 & 0x0F)));
        }
    }

    void op06() {  // reverse bitmap, nibble-swapped
        for (int i = 0, j = op06_len_ - 1; i < op06_len_; ++i, --j)
            out_[j] = static_cast<uint8_t>((params_[i] << 4) | (params_[i] >> 4));
    }

    void op0d() {  // scale bitmap with the hardware's fixed-point step
        uint32_t multiplier;
        if (op0d_in_len_ <= op0d_out_len_)
            multiplier = 0x10000;
        else
            multiplier = (static_cast<uint32_t>(op0d_in_len_) << 17) /
                         ((static_cast<uint32_t>(op0d_out_len_) << 1) + 1);
        uint32_t pixloc = 0;
        uint8_t pix[512];
        for (int i = 0; i < op0d_out_len_ * 2; ++i) {
            const int j = static_cast<int>(pixloc >> 16);
            pix[i] = (j & 1) ? (params_[j >> 1] & 0x0F)
                             : static_cast<uint8_t>((params_[j >> 1] & 0xF0) >> 4);
            pixloc += multiplier;
        }
        for (int i = 0; i < op0d_out_len_; ++i)
            out_[i] = static_cast<uint8_t>((pix[i << 1] << 4) | pix[(i << 1) + 1]);
    }
};

}  // namespace famemu::snes
