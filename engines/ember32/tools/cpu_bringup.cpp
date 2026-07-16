// ember32/tools/cpu_bringup.cpp — CPU smoke test on the register-mapped bus. The
// cart's data (palette, tiles, tilemap, sprite pixels) is laid into RAM and its
// static registers pre-set; then a small ARM program LOOPS computing 6 sprite
// positions and writes them via MMIO before triggering render. The frame's sprite
// layout is therefore produced by executing ARM (MOV/ADD/MOV-LSL/CMP/BLT).
//   c++ -std=c++17 -O2 -I.. tools/cpu_bringup.cpp -o /tmp/e32cpu && /tmp/e32cpu out.ppm
#include "../cpu_arm7.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace ember32;

static bool arm_imm(uint32_t c, uint32_t& e){ for(int r=0;r<16;r++){int k=2*r;uint32_t v=k?((c<<k)|(c>>(32-k))):c; if(v<=0xFF){e=(uint32_t(r)<<8)|v;return true;}} return false; }
static uint32_t mov_imm(int rd,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE3A00000u|(rd<<12)|e;}
static uint32_t mvn_imm(int rd,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE3E00000u|(rd<<12)|e;}
static uint32_t add_imm(int rd,int rn,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE2800000u|(rn<<16)|(rd<<12)|e;}
static uint32_t cmp_imm(int rn,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE3500000u|(rn<<16)|e;}
static uint32_t add_reg(int rd,int rn,int rm){return 0xE0800000u|(rn<<16)|(rd<<12)|rm;}
static uint32_t mov_lsl(int rd,int rm,int a){return 0xE1A00000u|(rd<<12)|(a<<7)|rm;}
static uint32_t str_off(int rt,int rn,int off){return 0xE5800000u|(rn<<16)|(rt<<12)|(off&0xFFF);}
static uint32_t b_lt(int from,int to){int off=(to-(from+8))>>2;return 0xBA000000u|(off&0xFFFFFF);}

int main(int argc, char** argv){
    const char* out = argc>1?argv[1]:"e32_cpu.ppm";
    static Bus bus;
    const uint32_t PAL=0x1000, TILES=0x1400, MAP=0x1800, ORB=0x2000;   // cart data section

    // palette
    auto pal=[&](int i,uint32_t rgb){ bus.w32(PAL+i*4, rgb); };
    pal(1,0x1A1E36); pal(2,0x283054); pal(4,0xFF7828); pal(5,0xFFDC5A); pal(6,0x9646E6);
    // 2 checker floor tiles + orb sprite
    for(int p=0;p<64;p++) bus.w8(TILES + p, ((p%8/4)^(p/8/4))?1:2);
    for(int p=0;p<64;p++) bus.w8(TILES+64+p, ((p%8/4)^(p/8/4))?2:1);
    for(int i=0;i<16*16;i++) bus.w32(MAP+i*4, (i&1));                  // alternating tiles
    const int SZ=40;
    for(int y=0;y<SZ;y++)for(int x=0;x<SZ;x++){ float dx=x-SZ/2.0f+.5f,dy=y-SZ/2.0f+.5f,r=std::sqrt(dx*dx+dy*dy)/(SZ/2.0f);
        bus.w8(ORB+y*SZ+x, r>1?0:(r>.72f?6:(r>.38f?4:5))); }

    // static registers (the cart's initial config): palette, tileset, layer 0, 6 sprites
    bus.reg_set(0x08,PAL); bus.reg_set(0x0C,TILES); bus.reg_set(0x10,2);
    bus.reg_set(0x40,1); bus.reg_set(0x44,MAP); bus.reg_set(0x48,16); bus.reg_set(0x4C,16);
    bus.reg_set(0x5C,3<<16); bus.reg_set(0x60,0);                     // layer0 scale 3x, prio 0
    for(int s=0;s<6;s++){ uint32_t b=0x400+s*0x20;
        bus.reg_set(b+0x08,1); bus.reg_set(b+0x0C,ORB); bus.reg_set(b+0x10,SZ);
        bus.reg_set(b+0x14,SZ); bus.reg_set(b+0x1C,1<<16); }           // enable, tile, w,h, scale 1x

    // ARM program: write backdrop + a loop of 6 sprite positions, then render + halt.
    std::vector<uint32_t> prog = {
        mov_imm(0,0x04000000), mov_imm(1,0x2C), str_off(1,0,0x00),     // r0=MMIO, backdrop
        mov_imm(2,0), mov_imm(3,40), mov_imm(4,120),                   // i, x, y
        /* loop@0x18 */ mov_lsl(5,2,5), add_imm(6,0,0x400), add_reg(6,6,5),  // r6 = MMIO+0x400+i*0x20
        str_off(3,6,0), str_off(4,6,4),                               // sprite[i].x/.y
        add_imm(3,3,48), add_imm(2,2,1), cmp_imm(2,6), b_lt(0x38,0x18),
        mov_imm(1,1), str_off(1,0,4), mvn_imm(15,0),                   // render, halt
    };
    for(size_t i=0;i<prog.size();i++) bus.w32(uint32_t(i*4), prog[i]);

    CPU cpu; cpu.bus=&bus; cpu.reset(0); cpu.run(10000);
    std::printf("CPU halted=%d after %llu instrs; rendered=%d\n", cpu.halted,(unsigned long long)cpu.cycles,bus.rendered);
    if(!bus.rendered){ std::printf("!! render never triggered\n"); return 1; }

    FILE* f=std::fopen(out,"wb"); std::fprintf(f,"P6\n%d %d\n255\n",Compositor::W,Compositor::H);
    for(int i=0;i<Compositor::W*Compositor::H;i++) std::fputc(bus.gpu.fb[i].r,f),std::fputc(bus.gpu.fb[i].g,f),std::fputc(bus.gpu.fb[i].b,f);
    std::fclose(f); std::printf("wrote %s\n",out); return 0;
}
