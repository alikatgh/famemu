// ember32/compositor_fast.hpp — the OPTIMISED Ember 32 compositor.
//
// This is the fast core. It renders into the SAME Compositor fields as the
// reference (compositor.hpp) and is verified PIXEL-FOR-PIXEL identical to it on
// the feature-cart suite (tools/fastcore_test.cpp) — the contract from the README.
//
// It is faster than the reference without changing a single output bit, by:
//   * The separable fast path — a layer with no rotation/shear and no per-line
//     offsets (b==0 && c==0) maps x->sourceX and y->sourceY independently, so the
//     source tile coords are precomputed once per column / per row and the inner
//     loop is pure integer (no per-pixel float, floor or modulo). This is the
//     common case (plain scale + scroll) and the whole-screen layer cost is where
//     the reference spends most of its time.
//   * Hoisting the row-constant affine terms (b*dy, d*dy) out of the inner loop
//     for rotated/sheared layers and every sprite. These are the SAME float
//     values the reference computes per pixel, just computed once per row — so the
//     sum association is unchanged and the result is bit-identical.
//   * Inlining the sampler + blend (no per-pixel call through sample_layer()).
//
// The blend math (mix8/addc/subc/blend order, the back-to-front draw list, the AABB
// culling) is copied verbatim from the reference so overlapping pixels resolve
// identically. Quads keep the reference's exact per-pixel path (rare, small, and
// the per-pixel divide can't be reassociated bit-identically).
#pragma once
#include "compositor.hpp"

namespace ember32 {

namespace fastcore {

inline uint8_t mix8(uint8_t d, uint8_t s, int a) { return uint8_t((s * a + d * (255 - a)) / 255); }
inline uint8_t addc(uint8_t d, int v) { int r = d + v; return uint8_t(r > 255 ? 255 : r); }
inline uint8_t subc(uint8_t d, int v) { int r = d - v; return uint8_t(r < 0 ? 0 : r); }

inline void blend_px(Compositor& C, int idx, RGB src, BlendMode m, int a) {
    RGB d = C.fb[idx], o;
    switch (m) {
        case BLEND_NONE:  o = a >= 255 ? src : RGB{mix8(d.r,src.r,a),mix8(d.g,src.g,a),mix8(d.b,src.b,a)}; break;
        case BLEND_ALPHA: o = {mix8(d.r,src.r,a), mix8(d.g,src.g,a), mix8(d.b,src.b,a)}; break;
        case BLEND_ADD:   o = {addc(d.r,src.r*a/255), addc(d.g,src.g*a/255), addc(d.b,src.b*a/255)}; break;
        case BLEND_SUB:   o = {subc(d.r,src.r*a/255), subc(d.g,src.g*a/255), subc(d.b,src.b*a/255)}; break;
        default:          o = src; break;
    }
    C.fb[idx] = o;
}

inline void draw_layer(Compositor& C, const Layer& L) {
    const int W = Compositor::W, H = Compositor::H;
    const int tw = L.map_w * 8, th = L.map_h * 8;
    const uint8_t* tp = C.tileset.pixels;

    // Separable: no rotation/shear and no per-line offsets -> source coords depend
    // on x alone / y alone. b*dy and c*dx are ±0 (b==c==0), which is floor-neutral,
    // so the precomputed px/py equal the reference's per-pixel floor exactly.
    if (L.b == 0.0f && L.c == 0.0f && !L.line_sx && !L.line_sy) {
        int colpx[Compositor::W], rowpy[Compositor::H];
        for (int x = 0; x < W; x++) {
            float dx = x - W * 0.5f;
            float fx = L.a * dx + L.b * (0 - H * 0.5f) + L.ox;   // + (±0) b*dy term
            int px = (int)std::floor(fx);
            if (L.wrap) px = ((px % tw) + tw) % tw;
            else if (px < 0 || px >= tw) px = -1;                // whole column transparent
            colpx[x] = px;
        }
        for (int y = 0; y < H; y++) {
            float dy = y - H * 0.5f;
            float fy = L.c * (0 - W * 0.5f) + L.d * dy + L.oy;   // + (±0) c*dx term
            int py = (int)std::floor(fy);
            if (L.wrap) py = ((py % th) + th) % th;
            else if (py < 0 || py >= th) py = -1;
            rowpy[y] = py;
        }
        for (int y = 0; y < H; y++) {
            int py = rowpy[y]; if (py < 0) continue;
            const int rowbase = (py / 8) * L.map_w, pym8 = (py % 8) * 8, dst = y * W;
            for (int x = 0; x < W; x++) {
                int px = colpx[x]; if (px < 0) continue;
                int tile = L.map[rowbase + (px / 8)];
                uint8_t ix = tp[tile * 64 + pym8 + (px % 8)];
                if (ix == 0) continue;
                blend_px(C, dst + x, C.palette[ix], L.blend, L.alpha);
            }
        }
        return;
    }

    // Rotated / sheared / line-offset: keep the per-pixel affine, but hoist the
    // row-constant terms b*dy and d*dy (identical values -> bit-identical result).
    for (int y = 0; y < H; y++) {
        float dy = y - H * 0.5f;
        float bdy = L.b * dy, ddy = L.d * dy;
        const int dst = y * W;
        for (int x = 0; x < W; x++) {
            float dx = x - W * 0.5f;
            float fx = L.a * dx + bdy + L.ox;
            float fy = L.c * dx + ddy + L.oy;
            if (L.line_sx) fx += L.line_sx[y];
            if (L.line_sy) fy += L.line_sy[y];
            int px = (int)std::floor(fx), py = (int)std::floor(fy);
            if (L.wrap) { px = ((px % tw) + tw) % tw; py = ((py % th) + th) % th; }
            else if (px < 0 || py < 0 || px >= tw || py >= th) continue;
            int tile = L.map[(py / 8) * L.map_w + (px / 8)];
            uint8_t ix = tp[tile * 64 + (py % 8) * 8 + (px % 8)];
            if (ix == 0) continue;
            blend_px(C, dst + x, C.palette[ix], L.blend, L.alpha);
        }
    }
}

inline void draw_sprite(Compositor& C, const Sprite& S) {
    const int W = Compositor::W, H = Compositor::H;
    float det = S.a * S.d - S.b * S.c;
    if (det == 0) return;
    float ia = S.d / det, ib = -S.b / det, ic = -S.c / det, id = S.a / det;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int cy = 0; cy <= 1; cy++)
        for (int cx = 0; cx <= 1; cx++) {
            float lx = cx * S.w - S.w * 0.5f, ly = cy * S.h - S.h * 0.5f;
            float sx = ia * lx + ib * ly + S.x, sy = ic * lx + id * ly + S.y;
            minx = std::fmin(minx, sx); maxx = std::fmax(maxx, sx);
            miny = std::fmin(miny, sy); maxy = std::fmax(maxy, sy);
        }
    int x0 = std::max(0, (int)std::floor(minx)), x1 = std::min(W - 1, (int)std::ceil(maxx));
    int y0 = std::max(0, (int)std::floor(miny)), y1 = std::min(H - 1, (int)std::ceil(maxy));
    const float hw = S.w * 0.5f, hh = S.h * 0.5f;
    for (int y = y0; y <= y1; y++) {
        float dy = y - S.y;
        float bdy = S.b * dy, ddy = S.d * dy;   // hoisted row constants (bit-identical)
        const int dst = y * W;
        for (int x = x0; x <= x1; x++) {
            float dx = x - S.x;
            float fx = S.a * dx + bdy + hw;
            float fy = S.c * dx + ddy + hh;
            int px = (int)std::floor(fx), py = (int)std::floor(fy);
            if (px < 0 || py < 0 || px >= S.w || py >= S.h) continue;
            uint8_t ix = S.pixels[py * S.w + px];
            if (ix == 0) continue;
            blend_px(C, dst + x, C.palette[ix], S.blend, S.alpha);
        }
    }
}

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
inline void draw_tri(Compositor& C, const Quad& Q, int i0, int i1, int i2) {
    const int W = Compositor::W, H = Compositor::H;
    float ax=Q.x[i0],ay=Q.y[i0], bx=Q.x[i1],by=Q.y[i1], cx=Q.x[i2],cy=Q.y[i2];
    float area = edge(ax,ay,bx,by,cx,cy);
    if (std::fabs(area) < 1e-6f) return;
    int x0=std::max(0,(int)std::floor(std::min({ax,bx,cx})));
    int x1=std::min(W-1,(int)std::ceil(std::max({ax,bx,cx})));
    int y0=std::max(0,(int)std::floor(std::min({ay,by,cy})));
    int y1=std::min(H-1,(int)std::ceil(std::max({ay,by,cy})));
    for (int y=y0;y<=y1;y++)
        for (int x=x0;x<=x1;x++) {
            float px=x+0.5f, py=y+0.5f;
            float w0=edge(bx,by,cx,cy,px,py)/area;
            float w1=edge(cx,cy,ax,ay,px,py)/area;
            float w2=edge(ax,ay,bx,by,px,py)/area;
            if (w0<0||w1<0||w2<0) continue;
            int tu=(int)(w0*Q.u[i0]+w1*Q.u[i1]+w2*Q.u[i2]);
            int tv=(int)(w0*Q.v[i0]+w1*Q.v[i1]+w2*Q.v[i2]);
            if (tu<0||tv<0||tu>=Q.tw||tv>=Q.th) continue;
            uint8_t ix=Q.tex[tv*Q.tw+tu];
            if (ix==0) continue;
            blend_px(C, y*W+x, C.palette[ix], Q.blend, Q.alpha);
        }
}

} // namespace fastcore

// Optimised render: pixel-for-pixel identical to Compositor::render().
inline void render_fast(Compositor& C) {
    const int W = Compositor::W, H = Compositor::H;
    for (int i = 0; i < W * H; i++) C.fb[i] = C.backdrop;

    struct D { int prio, type, idx; };
    D list[6 + 1024 + 64];
    int n = 0;
    for (int i = 0; i < 6; i++)
        if (C.layers[i].enabled) list[n++] = {C.layers[i].priority, 0, i};
    for (int i = 0; i < C.sprite_count; i++)
        if (C.sprites[i].enabled) list[n++] = {C.sprites[i].priority, 1, i};
    for (int i = 0; i < C.quad_count; i++)
        if (C.quads[i].enabled) list[n++] = {C.quads[i].priority, 2, i};
    for (int i = 1; i < n; i++) {           // same stable insertion sort as the reference
        D k = list[i]; int j = i - 1;
        while (j >= 0 && list[j].prio > k.prio) { list[j + 1] = list[j]; j--; }
        list[j + 1] = k;
    }
    for (int i = 0; i < n; i++) {
        if (list[i].type == 0)      fastcore::draw_layer(C, C.layers[list[i].idx]);
        else if (list[i].type == 1) fastcore::draw_sprite(C, C.sprites[list[i].idx]);
        else { const Quad& Q = C.quads[list[i].idx]; fastcore::draw_tri(C,Q,0,1,2); fastcore::draw_tri(C,Q,0,2,3); }
    }
}

} // namespace ember32
