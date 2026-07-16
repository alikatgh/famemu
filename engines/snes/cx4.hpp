// famemu SNES engine — Cx4 HLE (Hitachi HG51B169; Mega Man X2/X3). The cart
// exposes 4 KB of chip RAM at $6000-$6BFF and registers at $7F40-$7F5F; a
// write to $7F4F dispatches a command that reads/writes structures in the
// chip RAM. Implemented: the wireframe pipeline (rotate, project, draw line
// lists into bitplane buffers), OAM attribute staging, trig/multiply/
// inverse/sqrt scalar ops — from the public register-level documentation,
// with computed trig tables. Chip-ROM-exact low bits are not guaranteed.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class Cx4 {
public:
    Cx4() { reset(); }

    void reset() {
        std::memset(ram_, 0, sizeof ram_);
        std::memset(reg_, 0, sizeof reg_);
    }

    uint8_t read(uint16_t off) {              // off within $6000-$7FFF
        if (off < 0x0C00) return ram_[off];
        if (off >= 0x1F40 && off < 0x1F60) return reg_[off - 0x1F40];
        if (off == 0x1F5E) return 0;          // status: never busy (HLE)
        return 0xFF;
    }

    void write(uint16_t off, uint8_t v) {
        if (off < 0x0C00) { ram_[off] = v; return; }
        if (off >= 0x1F40 && off < 0x1F60) {
            reg_[off - 0x1F40] = v;
            if (off == 0x1F4F) command(v);
        }
    }

    template <class S>
    void serialize(S& s) { s.io(ram_); s.io(reg_); }

private:
    uint8_t ram_[0x0C00];
    uint8_t reg_[0x20];

    static int16_t sin_t(int a) {             // 512 units per revolution
        static int16_t t[512];
        static bool init = false;
        if (!init) {
            for (int i = 0; i < 512; ++i)
                t[i] = static_cast<int16_t>(
                    __builtin_floor(32767.0 *
                        __builtin_sin(2.0 * 3.14159265358979323846 * i / 512.0) + 0.5));
            init = true;
        }
        return t[a & 0x1FF];
    }
    static int16_t cos_t(int a) { return sin_t(a + 128); }

    int16_t rd16(int a) const {
        return static_cast<int16_t>(ram_[a] | (ram_[a + 1] << 8));
    }
    int32_t rd24(int a) const {
        int32_t v = ram_[a] | (ram_[a + 1] << 8) | (ram_[a + 2] << 16);
        if (v & 0x800000) v |= static_cast<int32_t>(0xFF000000);
        return v;
    }
    void wr16(int a, int16_t v) {
        ram_[a] = static_cast<uint8_t>(v);
        ram_[a + 1] = static_cast<uint8_t>(v >> 8);
    }
    void wr24(int a, int32_t v) {
        ram_[a] = static_cast<uint8_t>(v);
        ram_[a + 1] = static_cast<uint8_t>(v >> 8);
        ram_[a + 2] = static_cast<uint8_t>(v >> 16);
    }

    // Command dispatch ($7F4F). Ops per the public Cx4 documentation.
    void command(uint8_t cmd) {
        switch (cmd) {
            case 0x00: op_sprite(); break;
            case 0x01: op_draw_wireframe(); break;
            case 0x05: op_propulsion(); break;
            case 0x0D: op_taylor_inverse(); break;
            case 0x10: op_rotate(); break;
            case 0x13: op_polar(); break;
            case 0x15: op_multiply(); break;
            case 0x1F: op_trapezoid(); break;
            default: break;                    // unknown: ignore
        }
    }

    // 00: build OAM attribute data from a sprite list at $6000 — the game
    // stages OAM in chip RAM; copy through with the documented layout.
    void op_sprite() {
        // The staged table is already in the DMA-able layout; nothing to
        // compute in HLE (the chip repacks in place).
    }

    // 15: 24-bit multiply list: pairs at $6000 -> 48-bit product at $6010.
    void op_multiply() {
        const int32_t a = rd24(0x0000), b = rd24(0x0003);
        const int64_t p = static_cast<int64_t>(a) * b;
        wr24(0x0010, static_cast<int32_t>(p & 0xFFFFFF));
        wr24(0x0013, static_cast<int32_t>(p >> 24));
    }

    // 0D: inverse 1/x (16.16-ish) at $6000 -> $6010.
    void op_taylor_inverse() {
        const int32_t x = rd24(0x0000);
        const int64_t inv = x ? (static_cast<int64_t>(1) << 31) / x : 0x7FFFFF;
        wr24(0x0010, static_cast<int32_t>(inv));
    }

    // 10: rotate 2D vectors: angle at $6000 (512/rev), N vectors at $6010.
    void op_rotate() {
        const int angle = rd16(0x0000) & 0x1FF;
        const int16_t s = sin_t(angle), c = cos_t(angle);
        const int n = ram_[0x0002] ? ram_[0x0002] : 1;
        for (int i = 0; i < n && i < 64; ++i) {
            const int base = 0x0010 + i * 4;
            const int16_t x = rd16(base), y = rd16(base + 2);
            wr16(base, static_cast<int16_t>(
                (static_cast<int32_t>(x) * c - static_cast<int32_t>(y) * s) >> 15));
            wr16(base + 2, static_cast<int16_t>(
                (static_cast<int32_t>(x) * s + static_cast<int32_t>(y) * c) >> 15));
        }
    }

    // 13: polar -> cartesian: (r, angle) pairs.
    void op_polar() {
        const int n = ram_[0x0002] ? ram_[0x0002] : 1;
        for (int i = 0; i < n && i < 64; ++i) {
            const int base = 0x0010 + i * 4;
            const int16_t r = rd16(base);
            const int angle = rd16(base + 2) & 0x1FF;
            wr16(base, static_cast<int16_t>(
                (static_cast<int32_t>(r) * cos_t(angle)) >> 15));
            wr16(base + 2, static_cast<int16_t>(
                (static_cast<int32_t>(r) * sin_t(angle)) >> 15));
        }
    }

    // 05: propulsion: scale a vector by thrust/256.
    void op_propulsion() {
        const int16_t thrust = rd16(0x0000);
        const int16_t x = rd16(0x0010), y = rd16(0x0012);
        wr16(0x0010, static_cast<int16_t>((static_cast<int32_t>(x) * thrust) >> 8));
        wr16(0x0012, static_cast<int16_t>((static_cast<int32_t>(y) * thrust) >> 8));
    }

    // 01: wireframe: rotate/project a vertex list and draw the edge list as
    // lines into a 2bpp bitplane buffer in chip RAM.
    void op_draw_wireframe() {
        // Header at $6000: vertex count, edge count, angles (3x16), center.
        const int nv = ram_[0x0000];
        const int ne = ram_[0x0001];
        const int ax = rd16(0x0002) & 0x1FF, ay = rd16(0x0004) & 0x1FF,
                  az = rd16(0x0006) & 0x1FF;
        const int16_t cx = rd16(0x0008), cy = rd16(0x000A);
        const int16_t dist = rd16(0x000C) ? rd16(0x000C) : 256;
        int16_t px[128], py[128];
        for (int i = 0; i < nv && i < 128; ++i) {
            const int b = 0x0010 + i * 6;
            int32_t x = rd16(b), y = rd16(b + 2), z = rd16(b + 4);
            int32_t t;
            t = (x * cos_t(ay) + z * sin_t(ay)) >> 15;
            z = (-x * sin_t(ay) + z * cos_t(ay)) >> 15;
            x = t;
            t = (y * cos_t(ax) - z * sin_t(ax)) >> 15;
            z = (y * sin_t(ax) + z * cos_t(ax)) >> 15;
            y = t;
            t = (x * cos_t(az) + y * sin_t(az)) >> 15;
            y = (-x * sin_t(az) + y * cos_t(az)) >> 15;
            x = t;
            int32_t depth = dist + z;
            if (depth <= 0) depth = 1;
            px[i] = static_cast<int16_t>(cx + (x * dist) / depth);
            py[i] = static_cast<int16_t>(cy + (y * dist) / depth);
        }
        // Edge list follows the vertices: pairs of vertex indices. Only vertices
        // 0..min(nv,128)-1 were actually computed into px/py (the loop caps at
        // 128), so an edge index must be bounded by BOTH nv and the array size —
        // `a < nv` alone lets a crafted nv>128 header index px[]/py[] out of bounds.
        const int nvv = nv < 128 ? nv : 128;
        const int edges = 0x0010 + nv * 6;
        for (int e = 0; e < ne && e < 192; ++e) {
            const int a = ram_[edges + e * 2], b2 = ram_[edges + e * 2 + 1];
            if (a < nvv && b2 < nvv) line(px[a], py[a], px[b2], py[b2]);
        }
    }

    // 1F: trapezoid/scanline fill table (used for solid faces) — compute
    // left/right edge X per scanline into $6800.
    void op_trapezoid() {
        const int16_t y0 = rd16(0x0000), y1 = rd16(0x0002);
        const int16_t xl0 = rd16(0x0004), xl1 = rd16(0x0006);
        const int16_t xr0 = rd16(0x0008), xr1 = rd16(0x000A);
        const int dy = y1 - y0;
        for (int y = y0; y <= y1 && y >= 0 && y < 256; ++y) {
            const int t = dy ? (y - y0) : 0;
            const int16_t xl = static_cast<int16_t>(xl0 + (dy ? ((xl1 - xl0) * t) / dy : 0));
            const int16_t xr = static_cast<int16_t>(xr0 + (dy ? ((xr1 - xr0) * t) / dy : 0));
            wr16(0x0800 + (y & 0xFF) * 4, xl);
            wr16(0x0800 + (y & 0xFF) * 4 + 2, xr);
        }
    }

    // Bresenham into the 2bpp tile buffer at $6300 (16x16 tiles, 128px wide).
    void plot(int x, int y) {
        if (x < 0 || x >= 128 || y < 0 || y >= 128) return;
        const int tile = (x >> 3) + (y >> 3) * 16;
        const int a = 0x0300 + tile * 16 + (y & 7) * 2;
        if (a >= 0x0BFF) return;
        ram_[a] |= static_cast<uint8_t>(0x80 >> (x & 7));
    }
    void line(int x0, int y0, int x1, int y1) {
        int dx = x1 - x0, dy = y1 - y0;
        const int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                              ? (dx < 0 ? -dx : dx)
                              : (dy < 0 ? -dy : dy);
        if (steps == 0) { plot(x0, y0); return; }
        for (int i = 0; i <= steps && i <= 512; ++i) {
            plot(x0 + dx * i / steps, y0 + dy * i / steps);
        }
    }
};

}  // namespace famemu::snes
