// ember32/bus.hpp — Ember 32 memory bus: flat main RAM + an MMIO register block
// onto the GPU. REFERENCE MODEL. A cartridge lays its data (palette, tiles,
// tilemaps, sprite pixels) into RAM and configures the scene by CONSOLE ADDRESS
// through these registers; on a write to RENDER the bus points the compositor's
// fields at those RAM addresses and composites a frame.
//
// Register map (all offsets from MMIO base 0x0400_0000; words are 32-bit):
//   0x000 BACKDROP     0x00RRGGBB
//   0x004 RENDER       write nonzero → apply the registers + composite a frame
//   0x008 PALETTE_ADDR RAM addr of 256 RGB words
//   0x00C TILESET_ADDR RAM addr of tile pixels (8bpp, 64 bytes/tile)
//   0x010 TILESET_CNT
//   0x014 FRAME        read-only, ++ each render (lets a cart animate)
//   Layer L (0x040 + L*0x40, L=0..5):
//     +00 ENABLE  +04 MAP_ADDR  +08 MAP_W  +0C MAP_H  +10 SCROLL_X  +14 SCROLL_Y
//     +18 ANGLE(16.16 rad)  +1C SCALE(16.16, 1.0=0x10000)  +20 PRIORITY  +24 BLEND
//   Sprite S (0x400 + S*0x20, S=0..1023):
//     +00 X  +04 Y  +08 ENABLE  +0C TILE_ADDR  +10 W  +14 H
//     +18 ANGLE(16.16)  +1C SCALE(16.16)
#pragma once
#include <cstdint>
#include <cstring>
#include "compositor.hpp"

namespace ember32 {

struct Bus {
    static const uint32_t RAM_SIZE = 4u * 1024 * 1024;   // 4 MB
    static const uint32_t MMIO = 0x04000000;
    static const uint32_t REG_BYTES = 0x8400;            // covers all layer + sprite regs
    uint8_t* ram;
    uint32_t reg[REG_BYTES / 4] = {};
    Compositor gpu;
    bool rendered = false;
    uint32_t frame = 0;
    uint32_t input = 0;                                  // pad buttons (read at MMIO+0x18)

    Bus() { ram = new uint8_t[RAM_SIZE]; std::memset(ram, 0, RAM_SIZE); }
    ~Bus() { delete[] ram; }
    Bus(const Bus&) = delete; Bus& operator=(const Bus&) = delete;

    uint8_t  r8(uint32_t a)  const { return a < RAM_SIZE ? ram[a] : 0; }
    uint16_t r16(uint32_t a) const { return a + 1 < RAM_SIZE ? uint16_t(ram[a] | ram[a+1] << 8) : 0; }
    uint32_t r32(uint32_t a) const {
        if (a + 3 < RAM_SIZE) return ram[a] | ram[a+1]<<8 | ram[a+2]<<16 | uint32_t(ram[a+3])<<24;
        if (a - MMIO == 0x14) return frame;              // FRAME counter
        if (a - MMIO == 0x18) return input;              // pad buttons
        return 0;
    }
    void w8(uint32_t a, uint8_t v)  { if (a < RAM_SIZE) ram[a] = v; else mmio(a, v); }
    void w16(uint32_t a, uint16_t v) { if (a + 1 < RAM_SIZE) { ram[a]=v; ram[a+1]=v>>8; } }
    void w32(uint32_t a, uint32_t v) {
        if (a + 3 < RAM_SIZE) { ram[a]=v; ram[a+1]=v>>8; ram[a+2]=v>>16; ram[a+3]=v>>24; }
        else mmio(a, v);
    }

    // set a register from the host (a cart's initial config / data section)
    void reg_set(uint32_t off, uint32_t v) { if (off < REG_BYTES) reg[off/4] = v; }

private:
    uint32_t rg(uint32_t off) const { return off < REG_BYTES ? reg[off/4] : 0; }

    void mmio(uint32_t a, uint32_t v) {
        uint32_t off = a - MMIO;
        if (off < REG_BYTES) reg[off/4] = v;
        if (off == 0x04 && v) apply();
    }

    void apply() {
        gpu = Compositor();                              // fresh scene each render
        uint32_t bd = rg(0x00);
        gpu.backdrop = {uint8_t(bd>>16), uint8_t(bd>>8), uint8_t(bd)};

        uint32_t pal = rg(0x08);
        for (int i = 0; i < 256; i++) { uint32_t w = r32(pal + i*4);
            gpu.palette[i] = {uint8_t(w>>16), uint8_t(w>>8), uint8_t(w)}; }
        gpu.tileset = { &ram[rg(0x0C)], int(rg(0x10)) };

        for (int L = 0; L < 6; L++) {
            uint32_t b = 0x40 + L*0x40; if (!rg(b)) continue;
            Layer& ly = gpu.layers[L];
            ly.enabled = true;
            ly.map = reinterpret_cast<const uint16_t*>(&ram[rg(b+0x04)]);
            ly.map_w = rg(b+0x08); ly.map_h = rg(b+0x0C);
            ly.ox = float(int32_t(rg(b+0x10))); ly.oy = float(int32_t(rg(b+0x14)));
            float scl = rg(b+0x1C)/65536.0f; if (scl <= 0) scl = 1;
            Compositor::make_affine(int32_t(rg(b+0x18))/65536.0f, scl, ly.a, ly.b, ly.c, ly.d);
            ly.priority = int(rg(b+0x20)); ly.wrap = true;
            ly.blend = BlendMode(rg(b+0x24));
        }
        int maxsp = 0;
        for (int S = 0; S < 1024; S++) {
            uint32_t b = 0x400 + S*0x20; if (!rg(b+0x08)) continue;
            Sprite& sp = gpu.sprites[S]; maxsp = S + 1;
            sp.enabled = true;
            sp.x = float(int32_t(rg(b+0x00))); sp.y = float(int32_t(rg(b+0x04)));
            sp.pixels = &ram[rg(b+0x0C)]; sp.w = rg(b+0x10); sp.h = rg(b+0x14);
            float scl = rg(b+0x1C)/65536.0f; if (scl <= 0) scl = 1;
            Compositor::make_affine(int32_t(rg(b+0x18))/65536.0f, scl, sp.a, sp.b, sp.c, sp.d);
            sp.priority = 2; sp.blend = BLEND_ALPHA; sp.alpha = 255;
        }
        gpu.sprite_count = maxsp;
        gpu.render();
        rendered = true; frame++;
    }
};

} // namespace ember32
