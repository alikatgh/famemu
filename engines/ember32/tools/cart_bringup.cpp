// ember32/tools/cart_bringup.cpp — PHASE 3: an Ember 32 bring-up cartridge, end
// to end. The cart's data (palette, tiles, tilemap, sprite) is laid into RAM; an
// ARM program configures the whole scene through MMIO — a scrolling, scaled
// background layer and a scaled + rotated sprite — and triggers render. The
// resulting framebuffer is then put THROUGH the composite signal path
// (rf_composite.hpp), the platform's signature. Writes the clean frame and the
// composite ("out the RF path") frame.
//   c++ -std=c++17 -O2 -I.. tools/cart_bringup.cpp -o /tmp/e32cart && /tmp/e32cart clean.ppm rf.ppm
#include "../cpu_arm7.hpp"
#include "../rf_composite.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace ember32;

static bool arm_imm(uint32_t c, uint32_t& e){ e=0; for(int r=0;r<16;r++){int k=2*r;uint32_t v=k?((c<<k)|(c>>(32-k))):c; if(v<=0xFF){e=(uint32_t(r)<<8)|v;return true;}} return false; }
static uint32_t mov_imm(int rd,uint32_t c){uint32_t e; if(!arm_imm(c,e)){std::fprintf(stderr,"!! imm %x needs >1 instr\n",c);} return 0xE3A00000u|(rd<<12)|e;}
static uint32_t mvn_imm(int rd,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE3E00000u|(rd<<12)|e;}
static uint32_t str_off(int rt,int rn,int off){return 0xE5800000u|(rn<<16)|(rt<<12)|(off&0xFFF);}

static void writePPM(const char* p, const RGB* fb, int W, int H){
    FILE* f=std::fopen(p,"wb"); std::fprintf(f,"P6\n%d %d\n255\n",W,H);
    for(int i=0;i<W*H;i++) std::fputc(fb[i].r,f),std::fputc(fb[i].g,f),std::fputc(fb[i].b,f);
    std::fclose(f);
}

int main(int argc, char** argv){
    const char* clean = argc>1?argv[1]:"e32_clean.ppm";
    const char* rf    = argc>2?argv[2]:"e32_rf.ppm";
    static Bus bus;
    const uint32_t PAL=0x1000, TILES=0x1400, MAP=0x1800, SPR=0x2000;   // cart data section

    // ---- cart data ----
    auto pal=[&](int i,uint32_t rgb){ bus.w32(PAL+i*4,rgb); };
    pal(1,0x20304F); pal(2,0x3A5478); pal(3,0x0E1524); pal(4,0x8FA8C8);   // hull panels
    pal(5,0xFFB030); pal(6,0xFF5828); pal(7,0xFFF0A0); pal(8,0x40D0C0);   // sprite
    for(int p=0;p<64;p++){ int tx=p%8,ty=p/8;                            // tile 0: panel + rivets
        bus.w8(TILES+p, (tx==0||ty==0)?3:((tx==7||ty==7)?4:(((tx/2)^(ty/2))&1?1:2))); }
    for(int p=0;p<64;p++){ int tx=p%8,ty=p/8;                            // tile 1: darker panel
        bus.w8(TILES+64+p, (tx==0||ty==0)?3:2); }
    for(int p=0;p<64;p++){ int tx=p%8,ty=p/8;                            // tile 2: bright grate
        bus.w8(TILES+128+p, (tx%2==0)?3:4); }
    for(int i=0;i<16*16;i++){ int c=i%16,r=i/16;                         // tilemap
        bus.w32(MAP+i*4, (r==0||r==15||c==0||c==15)?2 : (((c+r)&3)==0?1:0)); }
    const int SZ=48;                                                      // a chunky "ship" sprite
    for(int y=0;y<SZ;y++)for(int x=0;x<SZ;x++){ float dx=(x-SZ/2.0f+.5f)/(SZ/2.0f),dy=(y-SZ/2.0f+.5f)/(SZ/2.0f);
        float r=std::sqrt(dx*dx+dy*dy); uint8_t v=0;
        if(r<0.95f){ v = r<0.35f?7 : (r<0.6f?5 : 6); if(std::fabs(dx)<0.12f) v=7; }
        bus.w8(SPR+y*SZ+x, v); }

    // ---- the cart's ARM program: configure the scene via MMIO, then render ----
    struct RV{uint32_t reg,val;};
    RV cfg[] = {
        {0x000,0x14},                                       // backdrop (dark, fits an imm)
        {0x008,PAL},{0x00C,TILES},{0x010,3},                // palette + tileset
        {0x040,1},{0x044,MAP},{0x048,16},{0x04C,16},        // layer0 enable/map/size
        {0x050,40},{0x054,24},{0x05C,0x30000},{0x060,0},    // scroll (40,24), scale 3x, prio 0
        {0x400,160},{0x404,116},{0x408,1},{0x40C,SPR},      // sprite0 pos/enable/tile
        {0x410,SZ},{0x414,SZ},{0x418,0x8000},{0x41C,0x20000}, // w/h, angle 0.5rad, scale 2x
    };
    std::vector<uint32_t> prog;
    prog.push_back(mov_imm(0,0x04000000));                  // r0 = MMIO base
    for(auto& c : cfg){ prog.push_back(mov_imm(1,c.val)); prog.push_back(str_off(1,0,c.reg)); }
    prog.push_back(mov_imm(1,1)); prog.push_back(str_off(1,0,0x004)); // RENDER
    prog.push_back(mvn_imm(15,0));                          // halt
    for(size_t i=0;i<prog.size();i++) bus.w32(uint32_t(i*4), prog[i]);

    CPU cpu; cpu.bus=&bus; cpu.reset(0); cpu.run(100000);
    std::printf("cart ran %llu instrs, halted=%d, rendered=%d\n",
                (unsigned long long)cpu.cycles, cpu.halted, bus.rendered);
    if(!bus.rendered){ std::printf("!! never rendered\n"); return 1; }

    writePPM(clean, bus.gpu.fb, Compositor::W, Compositor::H);
    static RGB comp[Compositor::W*Compositor::H];
    composite_ntsc(bus.gpu.fb, comp, Compositor::W, Compositor::H);       // out the RF path
    writePPM(rf, comp, Compositor::W, Compositor::H);
    std::printf("wrote %s (clean) + %s (composite/RF)\n", clean, rf);
    return 0;
}
