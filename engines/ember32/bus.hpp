// ember32/bus.hpp — Ember 32 memory bus: flat main RAM + an MMIO window onto the
// GPU (compositor). REFERENCE MODEL. This is the seam the CPU drives the picture
// through: an ARM program stores to MMIO addresses and the compositor's state
// changes, then a write to the render trigger produces a frame.
//
// A first, deliberately small register map — enough to prove the CPU→MMIO→GPU
// path. The full map (every layer/sprite/palette/VRAM field) is a phase-2
// follow-on; the CPU and compositor don't move when it grows, only this file.
//
//   0x0400_0000  BACKDROP   word 0x00RRGGBB
//   0x0400_0004  RENDER     write nonzero → composite a frame into gpu.fb
//   0x0400_0100  SPRITES    +i*8: [+0]=sprite i screen-x, [+4]=screen-y (int words)
#pragma once
#include <cstdint>
#include <cstring>
#include "compositor.hpp"

namespace ember32 {

struct Bus {
    static const uint32_t RAM_SIZE = 4u * 1024 * 1024;   // 4 MB main RAM
    static const uint32_t MMIO = 0x04000000;
    uint8_t* ram;
    Compositor gpu;
    bool rendered = false;

    Bus() { ram = new uint8_t[RAM_SIZE]; std::memset(ram, 0, RAM_SIZE); }
    ~Bus() { delete[] ram; }
    Bus(const Bus&) = delete; Bus& operator=(const Bus&) = delete;

    uint8_t  r8(uint32_t a)  const { return a < RAM_SIZE ? ram[a] : 0; }
    uint32_t r32(uint32_t a) const {
        if (a + 3 < RAM_SIZE)
            return ram[a] | ram[a+1] << 8 | ram[a+2] << 16 | uint32_t(ram[a+3]) << 24;
        return 0;
    }
    void w8(uint32_t a, uint8_t v)  { if (a < RAM_SIZE) ram[a] = v; else mmio(a, v); }
    void w32(uint32_t a, uint32_t v) {
        if (a + 3 < RAM_SIZE) { ram[a]=v; ram[a+1]=v>>8; ram[a+2]=v>>16; ram[a+3]=v>>24; }
        else mmio(a, v);
    }

private:
    void mmio(uint32_t a, uint32_t v) {
        if (a == MMIO + 0x00) {
            gpu.backdrop = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
        } else if (a == MMIO + 0x04) {
            if (v) { gpu.render(); rendered = true; }
        } else if (a >= MMIO + 0x100) {
            uint32_t o = a - (MMIO + 0x100), i = o / 8, f = o % 8;
            if (i < 1024) {
                if (f == 0) gpu.sprites[i].x = float(int32_t(v));
                else        gpu.sprites[i].y = float(int32_t(v));
            }
        }
    }
};

} // namespace ember32
