// famemu SNES engine — S-DD1 (Star Ocean, Street Fighter Alpha 2): MMC
// banking at $4804-4807 plus the entropy decompressor that feeds VRAM DMA
// from banks $C0-FF while armed via $4800/$4801.
//
// The decompressor implements Andreas Naive's public-domain analysis of the
// chip (exact probability-evolution and Golomb run tables) — with thanks to
// him and The Dumper, as the original notice asks.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class SnesSystem;

class Sdd1 {
public:
    explicit Sdd1(SnesSystem& sys) : sys_(sys) { reset(); }

    void reset() {
        for (int i = 0; i < 4; ++i) banks_[i] = static_cast<uint8_t>(i);
        dma_enable_ = dma_ready_ = 0;
    }

    // $4800-$4807 (CPU banks 00-3F/80-BF)
    uint8_t read_io(uint16_t off) const {
        if (off >= 0x4804 && off <= 0x4807) return banks_[off - 0x4804];
        if (off == 0x4800) return dma_enable_;
        if (off == 0x4801) return dma_ready_;
        return 0;
    }
    void write_io(uint16_t off, uint8_t v) {
        if (off == 0x4800) dma_enable_ = v;
        else if (off == 0x4801) dma_ready_ = v;
        else if (off >= 0x4804 && off <= 0x4807) banks_[off - 0x4804] = v & 0x0F;
    }

    // Banks $C0-$FF: 1 MB slices selected per $4804-7.
    uint32_t map(uint8_t bank, uint16_t off) const {
        const int slot = ((bank >> 4) - 0x0C) & 3;
        return (static_cast<uint32_t>(banks_[slot]) << 20) |
               (static_cast<uint32_t>(bank & 0x0F) << 16) | off;
    }

    bool dma_armed(int ch) const {
        return (dma_enable_ & dma_ready_ & (1 << ch)) != 0;
    }
    void dma_done(int ch) { dma_ready_ &= static_cast<uint8_t>(~(1 << ch)); }

    void decompress(uint32_t rom_addr, uint8_t* out, uint32_t len);

    template <class S>
    void serialize(S& s) { s.io(banks_); s.io(dma_enable_); s.io(dma_ready_); }

private:
    SnesSystem& sys_;
    uint8_t banks_[4];
    uint8_t dma_enable_, dma_ready_;

    uint8_t rom(uint32_t a) const;  // defined next to SnesSystem
};

// The decompressor proper (per-call state; the chip restarts per transfer).
inline void Sdd1::decompress(uint32_t rom_addr, uint8_t* out, uint32_t len) {
    static const struct { uint8_t code, mps, lps; } kEvo[33] = {
        {0,25,25},{0,2,1},{0,3,1},{0,4,2},{0,5,3},{1,6,4},{1,7,5},{1,8,6},
        {1,9,7},{2,10,8},{2,11,9},{2,12,10},{2,13,11},{3,14,12},{3,15,13},
        {3,16,14},{3,17,15},{4,18,16},{4,19,17},{5,20,18},{5,21,19},
        {6,22,20},{6,23,21},{7,24,22},{7,24,23},{0,26,1},{1,27,2},{2,28,4},
        {3,29,8},{4,30,12},{5,31,16},{6,32,18},{7,24,22},
    };
    static const uint8_t kRun[128] = {
        128,64,96,32,112,48,80,16,120,56,88,24,104,40,72,8,124,60,92,28,108,
        44,76,12,116,52,84,20,100,36,68,4,126,62,94,30,110,46,78,14,118,54,
        86,22,102,38,70,6,122,58,90,26,106,42,74,10,114,50,82,18,98,34,66,2,
        127,63,95,31,111,47,79,15,119,55,87,23,103,39,71,7,123,59,91,27,107,
        43,75,11,115,51,83,19,99,35,67,3,125,61,93,29,109,45,77,13,117,53,85,
        21,101,37,69,5,121,57,89,25,105,41,73,9,113,49,81,17,97,33,65,1,
    };

    if (len == 0) len = 0x10000;
    uint32_t in_addr = rom_addr;
    const uint8_t first = rom(in_addr);
    const uint8_t second = rom(in_addr + 1);
    in_addr += 2;

    const int bitplane_type = first >> 6;
    int high_ctx, low_ctx;
    switch (first & 0x30) {
        case 0x00: high_ctx = 0x01C0; low_ctx = 0x0001; break;
        case 0x10: high_ctx = 0x0180; low_ctx = 0x0001; break;
        case 0x20: high_ctx = 0x00C0; low_ctx = 0x0001; break;
        default:   high_ctx = 0x0180; low_ctx = 0x0003; break;
    }

    uint16_t in_stream = static_cast<uint16_t>((first << 11) | (second << 3));
    int valid_bits = 5;
    uint8_t bit_ctr[8] = {0};
    uint8_t ctx_state[32] = {0};
    int ctx_mps[32] = {0};
    int prev_bits[8] = {0};

    auto get_codeword = [&](int bits) -> uint8_t {
        if (!valid_bits) {
            in_stream |= rom(in_addr++);
            valid_bits = 8;
        }
        in_stream <<= 1;
        --valid_bits;
        in_stream ^= 0x8000;
        if (in_stream & 0x8000) return static_cast<uint8_t>(0x80 + (1 << bits));
        const uint8_t tmp = static_cast<uint8_t>((in_stream >> 8) | (0x7F >> bits));
        in_stream <<= bits;
        valid_bits -= bits;
        if (valid_bits < 0) {
            in_stream |= static_cast<uint16_t>(rom(in_addr++)) << (-valid_bits);
            valid_bits += 8;
        }
        return kRun[tmp];
    };
    auto golomb_bit = [&](int code) -> uint8_t {
        if (!bit_ctr[code]) bit_ctr[code] = get_codeword(code);
        --bit_ctr[code];
        if (bit_ctr[code] == 0x80) {
            bit_ctr[code] = 0;
            return 2;  // 'last zero' (ones are always last)
        }
        return (bit_ctr[code] == 0) ? 1 : 0;
    };
    auto prob_bit = [&](uint8_t ctx) -> int {
        const uint8_t state = ctx_state[ctx];
        const uint8_t bit = golomb_bit(kEvo[state].code);
        if (bit & 1) {
            ctx_state[ctx] = kEvo[state].lps;
            if (state < 2) {
                ctx_mps[ctx] ^= 1;
                return ctx_mps[ctx];
            }
            return ctx_mps[ctx] ^ 1;
        }
        if (bit) ctx_state[ctx] = kEvo[state].mps;
        return ctx_mps[ctx];
    };
    auto get_bit = [&](int plane) -> int {
        const int bit = prob_bit(static_cast<uint8_t>(
            ((plane & 1) << 4) | ((prev_bits[plane] & high_ctx) >> 5) |
            (prev_bits[plane] & low_ctx)));
        prev_bits[plane] <<= 1;
        prev_bits[plane] |= bit;
        return bit;
    };

    switch (bitplane_type) {
        case 0:
            for (;;) {
                uint8_t b1 = 0, b2 = 0;
                for (uint8_t bit = 0x80; bit; bit >>= 1) {
                    if (get_bit(0)) b1 |= bit;
                    if (get_bit(1)) b2 |= bit;
                }
                *out++ = b1;
                if (!--len) return;
                *out++ = b2;
                if (!--len) return;
            }
        case 1: {
            uint8_t i = 0, plane = 0;
            for (;;) {
                uint8_t b1 = 0, b2 = 0;
                for (uint8_t bit = 0x80; bit; bit >>= 1) {
                    if (get_bit(plane)) b1 |= bit;
                    if (get_bit(plane + 1)) b2 |= bit;
                }
                *out++ = b1;
                if (!--len) return;
                *out++ = b2;
                if (!--len) return;
                i = static_cast<uint8_t>(i + 32);
                if (!i) plane = (plane + 2) & 7;
            }
        }
        case 2: {
            uint8_t i = 0, plane = 0;
            for (;;) {
                uint8_t b1 = 0, b2 = 0;
                for (uint8_t bit = 0x80; bit; bit >>= 1) {
                    if (get_bit(plane)) b1 |= bit;
                    if (get_bit(plane + 1)) b2 |= bit;
                }
                *out++ = b1;
                if (!--len) return;
                *out++ = b2;
                if (!--len) return;
                i = static_cast<uint8_t>(i + 32);
                if (!i) plane ^= 2;
            }
        }
        default:
            do {
                uint8_t b1 = 0;
                int plane = 0;
                for (uint8_t bit = 1; bit; bit <<= 1, ++plane) {
                    if (get_bit(plane)) b1 |= bit;
                }
                *out++ = b1;
            } while (--len);
            return;
    }
}

}  // namespace famemu::snes
