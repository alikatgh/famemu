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

struct Compositor {
    static const int W = 320, H = 240;
    RGB palette[256] = {};
    TileSet tileset;
    Layer layers[6] = {};
    Sprite sprites[1024] = {};
    int sprite_count = 0;
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

        // Priority-ordered draw list (layers + sprites), back to front.
        struct D { int prio, type, idx; };
        D list[6 + 1024];
        int n = 0;
        for (int i = 0; i < 6; i++)
            if (layers[i].enabled) list[n++] = {layers[i].priority, 0, i};
        for (int i = 0; i < sprite_count; i++)
            if (sprites[i].enabled) list[n++] = {sprites[i].priority, 1, i};
        // stable insertion sort (small n; reference clarity over speed)
        for (int i = 1; i < n; i++) {
            D k = list[i]; int j = i - 1;
            while (j >= 0 && list[j].prio > k.prio) { list[j + 1] = list[j]; j--; }
            list[j + 1] = k;
        }
        for (int i = 0; i < n; i++) {
            if (list[i].type == 0) draw_layer(layers[list[i].idx]);
            else                   draw_sprite(sprites[list[i].idx]);
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
};

} // namespace ember32
