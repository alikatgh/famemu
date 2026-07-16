// ember32/tools/bringup.cpp — a standalone bring-up for the Ember 32 reference
// compositor. Builds a scene entirely from procedural assets (no data files),
// renders one frame, and writes a PPM. This is the "does the spec produce a
// picture?" smoke test for phase 2 — it exercises affine background layers,
// affine sprites, alpha + additive blend, and priority interleaving.
//
//   c++ -std=c++17 -O2 -I.. tools/bringup.cpp -o /tmp/bringup && /tmp/bringup out.ppm
#include "../compositor.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace ember32;

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "ember32_bringup.ppm";
    static Compositor c;

    // ---- palette (index 0 == transparent colour-key; 1.. are real colours) ----
    c.palette[1] = {28, 34, 60};    c.palette[2] = {40, 52, 92};    // floor checks
    c.palette[3] = {90, 210, 255};                                  // grid glow (cyan)
    c.palette[4] = {255, 120, 40};                                  // orb hot
    c.palette[5] = {255, 220, 90};                                  // orb core
    c.palette[6] = {180, 60, 220};                                  // orb rim (violet)
    c.backdrop = {10, 12, 22};

    // ---- tiles (8x8, 8bpp indices) --------------------------------------------
    static std::vector<uint8_t> tiles(3 * 64, 0);
    for (int p = 0; p < 64; p++) {
        int tx = p % 8, ty = p / 8;
        tiles[0 * 64 + p] = ((tx / 4) ^ (ty / 4)) ? 1 : 2;          // 0: checker floor
        tiles[1 * 64 + p] = (tx == 0 || ty == 0) ? 3 : 0;          // 1: grid line (else key)
        tiles[2 * 64 + p] = 0;                                      // 2: empty (key)
    }
    c.tileset = {tiles.data(), 3};

    // ---- layer 0: a big checker floor, scaled + rotated (spinning floor) -------
    static std::vector<uint16_t> floorMap(32 * 32, 0);             // all tile 0
    c.layers[0] = {};
    c.layers[0].enabled = true; c.layers[0].map = floorMap.data();
    c.layers[0].map_w = 32; c.layers[0].map_h = 32;
    Compositor::make_affine(0.35f, 2.2f, c.layers[0].a, c.layers[0].b, c.layers[0].c, c.layers[0].d);
    c.layers[0].ox = 128; c.layers[0].oy = 128;
    c.layers[0].priority = 0; c.layers[0].blend = BLEND_NONE; c.layers[0].wrap = true;

    // ---- layer 1: a glowing grid, rotated the other way, ADDITIVE --------------
    static std::vector<uint16_t> gridMap(32 * 32, 1);             // all tile 1 (grid)
    c.layers[1] = {};
    c.layers[1].enabled = true; c.layers[1].map = gridMap.data();
    c.layers[1].map_w = 32; c.layers[1].map_h = 32;
    Compositor::make_affine(-0.20f, 1.4f, c.layers[1].a, c.layers[1].b, c.layers[1].c, c.layers[1].d);
    c.layers[1].ox = 60; c.layers[1].oy = 60;
    c.layers[1].priority = 2; c.layers[1].blend = BLEND_ADD; c.layers[1].alpha = 180;

    // ---- an "orb" sprite: a radial gradient disc (rim/hot/core) ----------------
    const int SZ = 48;
    static std::vector<uint8_t> orb(SZ * SZ, 0);
    for (int y = 0; y < SZ; y++)
        for (int x = 0; x < SZ; x++) {
            float dx = x - SZ / 2.0f + 0.5f, dy = y - SZ / 2.0f + 0.5f;
            float r = std::sqrt(dx * dx + dy * dy) / (SZ / 2.0f);
            orb[y * SZ + x] = r > 1.0f ? 0 : (r > 0.75f ? 6 : (r > 0.4f ? 4 : 5));
        }

    // ---- sprites: varied scale / rotation / alpha / blend / priority -----------
    struct Place { float x, y, ang, scale; int prio; BlendMode bl; uint8_t a; };
    Place places[] = {
        {160, 120, 0.0f, 1.8f, 1, BLEND_ALPHA, 255},   // big centre orb, BEHIND the grid (prio 1 < 2)
        { 70,  70, 0.6f, 0.9f, 3, BLEND_ALPHA, 230},   // in front of grid
        {250,  80, 1.2f, 0.7f, 3, BLEND_ADD,   200},   // additive, rotated
        { 80, 180, 2.0f, 1.1f, 3, BLEND_ALPHA, 150},   // half-transparent
        {245, 175, 2.6f, 0.6f, 3, BLEND_ADD,   255},
        {160, 120, 0.8f, 3.0f, 4, BLEND_ADD,    70},   // huge faint additive bloom, frontmost
    };
    for (auto& p : places) {
        Sprite& s = c.sprites[c.sprite_count++];
        s.enabled = true; s.x = p.x; s.y = p.y; s.w = SZ; s.h = SZ;
        s.pixels = orb.data(); s.priority = p.prio; s.blend = p.bl; s.alpha = p.a;
        Compositor::make_affine(p.ang, p.scale, s.a, s.b, s.c, s.d);
    }

    c.render();

    FILE* f = std::fopen(out, "wb");
    if (!f) { std::perror("open"); return 1; }
    std::fprintf(f, "P6\n%d %d\n255\n", Compositor::W, Compositor::H);
    for (int i = 0; i < Compositor::W * Compositor::H; i++)
        std::fputc(c.fb[i].r, f), std::fputc(c.fb[i].g, f), std::fputc(c.fb[i].b, f);
    std::fclose(f);
    std::printf("wrote %s (%dx%d, %d sprites, 2 affine layers)\n",
                out, Compositor::W, Compositor::H, c.sprite_count);
    return 0;
}
