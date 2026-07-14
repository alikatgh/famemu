// famemu SNES engine — ST010 HLE (Seta; F1 ROC II: Race of Champions). The
// cart exposes 4 KB of shared RAM at banks $68-$6F; the game writes inputs,
// a command byte at $0020, and polls $0021 for completion. Implemented from
// the public command documentation with computed trig tables: 01 sort cars
// by score, 02 direction (atan2), 04 scale vector, 06 rotate vector. The
// exotic driving-AI op ($08) is approximated by leaving its input state
// unchanged (logged).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace famemu::snes {

class St010 {
public:
    St010() { reset(); }
    void reset() { std::memset(ram_, 0, sizeof ram_); }

    uint8_t read(uint16_t off) const { return ram_[off & 0xFFF]; }
    void write(uint16_t off, uint8_t v) {
        off &= 0xFFF;
        ram_[off] = v;
        if (off == 0x0020 && v) execute(v);
    }

    template <class S>
    void serialize(S& s) { s.io(ram_); }

private:
    uint8_t ram_[0x1000];

    int16_t rd16(int a) const {
        return static_cast<int16_t>(ram_[a] | (ram_[a + 1] << 8));
    }
    void wr16(int a, int16_t v) {
        ram_[a] = static_cast<uint8_t>(v);
        ram_[a + 1] = static_cast<uint8_t>(v >> 8);
    }

    static int16_t sin_t(int a) {          // 65536 units per revolution
        static int16_t t[1024];
        static bool init = false;
        if (!init) {
            for (int i = 0; i < 1024; ++i)
                t[i] = static_cast<int16_t>(
                    __builtin_floor(32767.0 *
                        __builtin_sin(2.0 * 3.14159265358979323846 * i / 1024.0) + 0.5));
            init = true;
        }
        return t[(a >> 6) & 0x3FF];
    }

    void execute(uint8_t op) {
        switch (op) {
            case 0x01: {  // sort up to 16 (score, index) pairs descending
                struct E { uint16_t score; uint16_t idx; } e[16];
                for (int i = 0; i < 16; ++i) {
                    e[i].score = static_cast<uint16_t>(rd16(0x0040 + i * 2));
                    e[i].idx = static_cast<uint16_t>(rd16(0x0060 + i * 2));
                }
                for (int i = 0; i < 16; ++i)
                    for (int j = i + 1; j < 16; ++j)
                        if (e[j].score > e[i].score) { E t = e[i]; e[i] = e[j]; e[j] = t; }
                for (int i = 0; i < 16; ++i) {
                    wr16(0x0040 + i * 2, static_cast<int16_t>(e[i].score));
                    wr16(0x0060 + i * 2, static_cast<int16_t>(e[i].idx));
                }
                break;
            }
            case 0x02: {  // direction: atan2(y, x) -> angle (65536/rev)
                const double y = rd16(0x0000), x = rd16(0x0002);
                const double a = __builtin_atan2(y, x);
                wr16(0x0010, static_cast<int16_t>(
                    a * 65536.0 / (2.0 * 3.14159265358979323846)));
                break;
            }
            case 0x04: {  // scale vector by magnitude/256
                const int16_t m = rd16(0x0000);
                const int16_t x = rd16(0x0002), y = rd16(0x0004);
                wr16(0x0010, static_cast<int16_t>((static_cast<int32_t>(x) * m) >> 8));
                wr16(0x0012, static_cast<int16_t>((static_cast<int32_t>(y) * m) >> 8));
                break;
            }
            case 0x06: {  // rotate vector by angle
                const int angle = rd16(0x0000);
                const int16_t x = rd16(0x0002), y = rd16(0x0004);
                const int16_t s = sin_t(angle),
                              c = sin_t(angle + 0x4000);
                wr16(0x0010, static_cast<int16_t>(
                    (static_cast<int32_t>(x) * c - static_cast<int32_t>(y) * s) >> 15));
                wr16(0x0012, static_cast<int16_t>(
                    (static_cast<int32_t>(x) * s + static_cast<int32_t>(y) * c) >> 15));
                break;
            }
            default: {
                static int n = 0;
                if (n < 4) { std::fprintf(stderr, "st010: unimplemented op %02X\n", op); ++n; }
                break;
            }
        }
        ram_[0x0020] = 0;     // command consumed
        ram_[0x0021] = 0x80;  // done flag
    }
};

}  // namespace famemu::snes
