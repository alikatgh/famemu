// ember32/tools/cpu_bringup.cpp — phase-2 CPU bring-up: an ARM program drives the
// compositor. We emit a small ARM machine-code program (encoding helpers below,
// so it's real ARM, not a fake), load it into RAM, run it on the reference CPU,
// and render. The program computes 6 sprite positions in a LOOP (MOV/ADD/CMP/
// conditional-branch) and writes them + the backdrop through MMIO, so the frame
// is genuinely produced by executing ARM instructions.
//
//   c++ -std=c++17 -O2 -I.. tools/cpu_bringup.cpp -o /tmp/e32cpu && /tmp/e32cpu out.ppm
#include "../cpu_arm7.hpp"
#include <cstdio>
#include <vector>
using namespace ember32;

// ---- tiny ARM encoders (just the forms this program uses) ------------------
static bool arm_imm(uint32_t c, uint32_t& enc) {          // find imm8 ROR 2r == c
    for (int r = 0; r < 16; r++) {
        int k = 2 * r; uint32_t imm = k ? ((c << k) | (c >> (32 - k))) : c;
        if (imm <= 0xFF) { enc = (uint32_t(r) << 8) | imm; return true; }
    }
    return false;
}
static uint32_t mov_imm(int rd, uint32_t c){ uint32_t e; arm_imm(c,e); return 0xE3A00000u|(rd<<12)|e; }
static uint32_t mvn_imm(int rd, uint32_t c){ uint32_t e; arm_imm(c,e); return 0xE3E00000u|(rd<<12)|e; }
static uint32_t add_imm(int rd,int rn,uint32_t c){ uint32_t e; arm_imm(c,e); return 0xE2800000u|(rn<<16)|(rd<<12)|e; }
static uint32_t cmp_imm(int rn,uint32_t c){ uint32_t e; arm_imm(c,e); return 0xE3500000u|(rn<<16)|e; }
static uint32_t add_reg(int rd,int rn,int rm){ return 0xE0800000u|(rn<<16)|(rd<<12)|rm; }
static uint32_t mov_lsl(int rd,int rm,int amt){ return 0xE1A00000u|(rd<<12)|(amt<<7)|rm; }
static uint32_t str_off(int rt,int rn,int off){ return 0xE5800000u|(rn<<16)|(rt<<12)|(off&0xFFF); }
static uint32_t b_lt(int from,int to){ int off=(to-(from+8))>>2; return 0xBA000000u|(off&0xFFFFFF); }

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "e32_cpu.ppm";
    static Bus bus;

    // ---- scene the CPU will position sprites into (non-CPU-driven bits in C) ---
    bus.gpu.palette[1] = {26, 30, 54};  bus.gpu.palette[2] = {40, 48, 84};
    bus.gpu.palette[4] = {255, 120, 40}; bus.gpu.palette[5] = {255, 220, 90};
    bus.gpu.palette[6] = {150, 70, 230};
    static uint8_t tiles[2 * 64];
    for (int p = 0; p < 64; p++) tiles[p] = ((p % 8 / 4) ^ (p / 8 / 4)) ? 1 : 2;   // dim checker floor
    bus.gpu.tileset = {tiles, 2};
    static uint16_t map[16 * 16] = {};                    // all tile 0 (checker)
    bus.gpu.layers[0].enabled = true; bus.gpu.layers[0].map = map;
    bus.gpu.layers[0].map_w = 16; bus.gpu.layers[0].map_h = 16;
    Compositor::make_affine(0.0f, 3.0f, bus.gpu.layers[0].a, bus.gpu.layers[0].b,
                            bus.gpu.layers[0].c, bus.gpu.layers[0].d);
    bus.gpu.layers[0].priority = 0;

    const int SZ = 40; static uint8_t orb[SZ * SZ];
    for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
        float dx = x - SZ/2.0f + 0.5f, dy = y - SZ/2.0f + 0.5f, r = std::sqrt(dx*dx+dy*dy)/(SZ/2.0f);
        orb[y*SZ+x] = r > 1 ? 0 : (r > 0.72f ? 6 : (r > 0.38f ? 4 : 5));
    }
    for (int i = 0; i < 6; i++) {                         // 6 sprites, x/y set BY THE CPU
        Sprite& s = bus.gpu.sprites[i];
        s.enabled = true; s.w = SZ; s.h = SZ; s.pixels = orb; s.priority = 2;
        s.blend = BLEND_ALPHA; s.alpha = 255;
        Compositor::make_affine(0.0f, 1.0f, s.a, s.b, s.c, s.d);
    }
    bus.gpu.sprite_count = 6;

    // ---- the ARM program ------------------------------------------------------
    std::vector<uint32_t> prog = {
        /* 00 */ mov_imm(0, 0x04000000),   // r0 = MMIO base
        /* 04 */ mov_imm(1, 0x2C),         // r1 = backdrop (dark blue; fits a rotated imm8)
        /* 08 */ str_off(1, 0, 0x00),      // BACKDROP = r1
        /* 0C */ mov_imm(2, 0),            // r2 = i
        /* 10 */ mov_imm(3, 40),           // r3 = x
        /* 14 */ mov_imm(4, 120),          // r4 = y
        // loop @ 0x18:
        /* 18 */ mov_lsl(5, 2, 3),         // r5 = i << 3   (i*8)
        /* 1C */ add_imm(6, 0, 0x100),     // r6 = base + 0x100
        /* 20 */ add_reg(6, 6, 5),         // r6 += i*8
        /* 24 */ str_off(3, 6, 0),         // sprite[i].x = r3
        /* 28 */ str_off(4, 6, 4),         // sprite[i].y = r4
        /* 2C */ add_imm(3, 3, 48),        // x += 48
        /* 30 */ add_imm(2, 2, 1),         // i++
        /* 34 */ cmp_imm(2, 6),            // i - 6
        /* 38 */ b_lt(0x38, 0x18),         // if i < 6 → loop
        /* 3C */ mov_imm(1, 1),
        /* 40 */ str_off(1, 0, 4),         // RENDER
        /* 44 */ mvn_imm(15, 0),           // PC = 0xFFFFFFFF → halt
    };
    for (size_t i = 0; i < prog.size(); i++) bus.w32(uint32_t(i * 4), prog[i]);

    CPU cpu; cpu.bus = &bus; cpu.reset(0);
    cpu.run(10000);

    std::printf("CPU halted=%d after %llu instrs; rendered=%d\n",
                cpu.halted, (unsigned long long)cpu.cycles, bus.rendered);
    if (!bus.rendered) { std::printf("!! render trigger never reached — CPU bug\n"); return 1; }

    FILE* f = std::fopen(out, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", Compositor::W, Compositor::H);
    for (int i = 0; i < Compositor::W * Compositor::H; i++)
        std::fputc(bus.gpu.fb[i].r, f), std::fputc(bus.gpu.fb[i].g, f), std::fputc(bus.gpu.fb[i].b, f);
    std::fclose(f);
    std::printf("wrote %s\n", out);
    return 0;
}
