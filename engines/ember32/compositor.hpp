// ember32/compositor.hpp — the Ember 32 2D compositor, REFERENCE MODEL.
//
// This is phase 2 of EMBER32.md: "the portable C model — correctness over speed,
// the spec made executable." It is the authority the fast core will later be
// verified pixel-for-pixel against, so every line here favours obvious
// correctness over cleverness.
//
// What it models (from EMBER32.md §"Video"):
//   * a 24-bit direct-colour framebuffer (320x240 base)
//   * up to 6 tile background layers, EACH with a full 2x3 affine transform
//     (scale / rotate / shear), independent scroll, priority and a blend mode
//   * up to 1024 affine sprites (scale/rotate), per-sprite alpha + priority
//   * true per-pixel priority compositing with normal / alpha / additive /
//     subtractive blend and colour-key (index 0) transparency
//
// NOT modelled yet (later in phase 2 — see README): the CPU, per-scanline
// register tables (HDMA-class), windows/clip, the optional quad unit, audio.
// The interface is deliberately register-free for now: you build a scene struct
// and call render(). The CPU will later drive these same fields via MMIO.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ember32 {

struct RGB { uint8_t r, g, b; };

enum BlendMode { BLEND_NONE, BLEND_ALPHA, BLEND_ADD, BLEND_SUB };

// 8x8 tiles, 8bpp palette indices (index 0 == transparent, the colour key).
struct TileSet { const uint8_t* pixels = nullptr; int tile_count = 0; };

struct Layer {
    bool enabled = false;
    const uint16_t* map = nullptr;   // map_w*map_h tile indices
    int map_w = 0, map_h = 0;        // tilemap size, in tiles
    // dest->source affine about the screen centre: (sx,sy) = M*(x-cx, y-cy)+(ox,oy)
    float a = 1, b = 0, c = 0, d = 1;
    float ox = 0, oy = 0;            // source-space scroll/offset
    int priority = 0;               // lower = further back
    BlendMode blend = BLEND_NONE;
    uint8_t alpha = 255;
    bool wrap = true;               // wrap the tilemap (else clip to its bounds)
    // Optional per-OUTPUT-scanline source offsets — the HDMA-class raster trick.
    // One entry per screen line (H of them). Bends the layer per line for water
    // ripple, parallax bands, wobble, perspective floors, etc.
    const int16_t* line_sx = nullptr;
    const int16_t* line_sy = nullptr;
};

struct Sprite {
    bool enabled = false;
    float x = 0, y = 0;             // screen position of the sprite's CENTRE
    float a = 1, b = 0, c = 0, d = 1;   // dest->source affine (scale/rotate)
    int w = 0, h = 0;              // sprite size in source pixels
    const uint8_t* pixels = nullptr;    // w*h 8bpp indices (0 = transparent)
    int priority = 0;
    BlendMode blend = BLEND_ALPHA;
    uint8_t alpha = 255;
};

// Optional textured/Gouraud QUAD — the "3-D as a distorted sprite" unit. Four
// screen corners + four texture coords; the picture is affine-mapped across it
// (not perspective-correct — the era's look). Reuses the paletted texture path.
struct Quad {
    bool enabled = false;
    float x[4] = {}, y[4] = {};      // screen corners (0,1,2,3 around the perimeter)
    float u[4] = {}, v[4] = {};      // matching texture coords, in texels
    const uint8_t* tex = nullptr;    // tw*th 8bpp indices (0 = transparent)
    int tw = 0, th = 0;
    int priority = 0;
    BlendMode blend = BLEND_NONE;
    uint8_t alpha = 255;
};

struct Compositor {
    static const int W = 320, H = 240;
    RGB palette[256] = {};
    TileSet tileset;
    Layer layers[6] = {};
    Sprite sprites[1024] = {};
    int sprite_count = 0;
    Quad quads[64] = {};
    int quad_count = 0;
    RGB backdrop = {0, 0, 0};
    RGB fb[W * H] = {};

    // Build a dest->source affine for a layer/sprite shown with the given screen
    // rotation (radians, CW) and uniform scale. Because we sample dest->source,
    // this is the INVERSE: rotate by -angle and divide by scale.
    static void make_affine(float angle, float scale,
                            float& a, float& b, float& c, float& d) {
        float s = std::sin(-angle), co = std::cos(-angle), inv = 1.0f / scale;
        a = co * inv;  b = -s * inv;
        c = s * inv;   d = co * inv;
    }

    void render() {
        for (int i = 0; i < W * H; i++) fb[i] = backdrop;

        // Priority-ordered draw list (layers + sprites + quads), back to front.
        struct D { int prio, type, idx; };
        D list[6 + 1024 + 64];
        int n = 0;
        for (int i = 0; i < 6; i++)
            if (layers[i].enabled) list[n++] = {layers[i].priority, 0, i};
        for (int i = 0; i < sprite_count; i++)
            if (sprites[i].enabled) list[n++] = {sprites[i].priority, 1, i};
        for (int i = 0; i < quad_count; i++)
            if (quads[i].enabled) list[n++] = {quads[i].priority, 2, i};
        // stable insertion sort (small n; reference clarity over speed)
        for (int i = 1; i < n; i++) {
            D k = list[i]; int j = i - 1;
            while (j >= 0 && list[j].prio > k.prio) { list[j + 1] = list[j]; j--; }
            list[j + 1] = k;
        }
        for (int i = 0; i < n; i++) {
            if (list[i].type == 0)      draw_layer(layers[list[i].idx]);
            else if (list[i].type == 1) draw_sprite(sprites[list[i].idx]);
            else                        draw_quad(quads[list[i].idx]);
        }
    }

private:
    static uint8_t mix8(uint8_t d, uint8_t s, int a) { return uint8_t((s * a + d * (255 - a)) / 255); }
    static uint8_t addc(uint8_t d, int v) { int r = d + v; return uint8_t(r > 255 ? 255 : r); }
    static uint8_t subc(uint8_t d, int v) { int r = d - v; return uint8_t(r < 0 ? 0 : r); }

    void blend_px(int idx, RGB src, BlendMode m, int a) {
        RGB d = fb[idx], o;
        switch (m) {
            case BLEND_NONE:  o = a >= 255 ? src : RGB{mix8(d.r,src.r,a),mix8(d.g,src.g,a),mix8(d.b,src.b,a)}; break;
            case BLEND_ALPHA: o = {mix8(d.r,src.r,a), mix8(d.g,src.g,a), mix8(d.b,src.b,a)}; break;
            case BLEND_ADD:   o = {addc(d.r,src.r*a/255), addc(d.g,src.g*a/255), addc(d.b,src.b*a/255)}; break;
            case BLEND_SUB:   o = {subc(d.r,src.r*a/255), subc(d.g,src.g*a/255), subc(d.b,src.b*a/255)}; break;
            default:          o = src; break;
        }
        fb[idx] = o;
    }

    // Returns true + colour if the layer covers (x,y); false if transparent/clipped.
    bool sample_layer(const Layer& L, int x, int y, RGB& out) const {
        float dx = x - W * 0.5f, dy = y - H * 0.5f;
        float fx = L.a * dx + L.b * dy + L.ox;
        float fy = L.c * dx + L.d * dy + L.oy;
        if (L.line_sx) fx += L.line_sx[y];      // per-scanline raster offsets (HDMA-class)
        if (L.line_sy) fy += L.line_sy[y];
        int px = (int)std::floor(fx), py = (int)std::floor(fy);
        int tw = L.map_w * 8, th = L.map_h * 8;
        if (L.wrap) { px = ((px % tw) + tw) % tw; py = ((py % th) + th) % th; }
        else if (px < 0 || py < 0 || px >= tw || py >= th) return false;
        int tile = L.map[(py / 8) * L.map_w + (px / 8)];
        uint8_t ix = tileset.pixels[tile * 64 + (py % 8) * 8 + (px % 8)];
        if (ix == 0) return false;                 // colour-key transparent
        out = palette[ix];
        return true;
    }

    void draw_layer(const Layer& L) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                RGB c;
                if (sample_layer(L, x, y, c)) blend_px(y * W + x, c, L.blend, L.alpha);
            }
    }

    void draw_sprite(const Sprite& S) {
        // Screen AABB: transform the four corners of the [0,w]x[0,h] source box
        // through the INVERSE of the dest->source matrix (i.e. source->dest).
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
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float dx = x - S.x, dy = y - S.y;
                float fx = S.a * dx + S.b * dy + S.w * 0.5f;
                float fy = S.c * dx + S.d * dy + S.h * 0.5f;
                int px = (int)std::floor(fx), py = (int)std::floor(fy);
                if (px < 0 || py < 0 || px >= S.w || py >= S.h) continue;
                uint8_t ix = S.pixels[py * S.w + px];
                if (ix == 0) continue;
                blend_px(y * W + x, palette[ix], S.blend, S.alpha);
            }
    }

    static float edge(float ax, float ay, float bx, float by, float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    }
    // affine-textured triangle; barycentric weights are divided by the SIGNED
    // area, so a point inside is w>=0 for either winding.
    void draw_tri(const Quad& Q, int i0, int i1, int i2) {
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
                blend_px(y*W+x, palette[ix], Q.blend, Q.alpha);
            }
    }
    void draw_quad(const Quad& Q) { draw_tri(Q,0,1,2); draw_tri(Q,0,2,3); }
};

} // namespace ember32
