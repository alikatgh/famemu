// ember32/tools/make_cart.cpp — emit a real .e32 cartridge FILE (a flat ROM
// image the app's load_rom() copies to RAM at 0). This is the same scene as
// cart_bringup.cpp (scrolling scaled layer + scaled/rotated sprite), but laid
// into one flat byte image — program at offset 0, cart data at 0x1000+ — so it
// can be fed through the real app route:
//   famemu --crtsnap builtin:ember32 demo.e32 out.png
// Build:
//   c++ -std=c++17 -O2 tools/make_cart.cpp -o /tmp/e32mk && /tmp/e32mk demo.e32
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

// ARM immediate encoders (pure; mirror cpu_bringup/cart_bringup).
static bool arm_imm(uint32_t c, uint32_t& e){ e=0; for(int r=0;r<16;r++){int k=2*r;uint32_t v=k?((c<<k)|(c>>(32-k))):c; if(v<=0xFF){e=(uint32_t(r)<<8)|v;return true;}} return false; }
static uint32_t mov_imm(int rd,uint32_t c){uint32_t e; if(!arm_imm(c,e)) std::fprintf(stderr,"!! imm %x needs >1 instr\n",c); return 0xE3A00000u|(rd<<12)|e;}
static uint32_t mvn_imm(int rd,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE3E00000u|(rd<<12)|e;}
static uint32_t str_off(int rt,int rn,int off){return 0xE5800000u|(rn<<16)|(rt<<12)|(off&0xFFF);}

int main(int argc, char** argv){
    const char* out = argc>1?argv[1]:"demo.e32";
    std::vector<uint8_t> rom;
    auto w8 =[&](uint32_t a,uint8_t v){ if(a+1>rom.size()) rom.resize(a+1,0); rom[a]=v; };
    auto w32=[&](uint32_t a,uint32_t v){ if(a+4>rom.size()) rom.resize(a+4,0); rom[a]=v; rom[a+1]=v>>8; rom[a+2]=v>>16; rom[a+3]=v>>24; };

    const uint32_t PAL=0x1000, TILES=0x1400, MAP=0x1800, SPR=0x2000;   // cart data section
    auto pal=[&](int i,uint32_t rgb){ w32(PAL+i*4,rgb); };
    pal(1,0x20304F); pal(2,0x3A5478); pal(3,0x0E1524); pal(4,0x8FA8C8);   // hull panels
    pal(5,0xFFB030); pal(6,0xFF5828); pal(7,0xFFF0A0); pal(8,0x40D0C0);   // sprite
    for(int p=0;p<64;p++){ int tx=p%8,ty=p/8;                            // tile 0: panel + rivets
        w8(TILES+p, (tx==0||ty==0)?3:((tx==7||ty==7)?4:(((tx/2)^(ty/2))&1?1:2))); }
    for(int p=0;p<64;p++){ int tx=p%8,ty=p/8;                            // tile 1: darker panel
        w8(TILES+64+p, (tx==0||ty==0)?3:2); }
    for(int p=0;p<64;p++){ int tx=p%8;                                   // tile 2: bright grate
        w8(TILES+128+p, (tx%2==0)?3:4); }
    for(int i=0;i<16*16;i++){ int c=i%16,r=i/16;                         // tilemap
        w32(MAP+i*4, (r==0||r==15||c==0||c==15)?2 : (((c+r)&3)==0?1:0)); }
    const int SZ=48;                                                      // a chunky "ship" sprite
    for(int y=0;y<SZ;y++)for(int x=0;x<SZ;x++){ float dx=(x-SZ/2.0f+.5f)/(SZ/2.0f),dy=(y-SZ/2.0f+.5f)/(SZ/2.0f);
        float r=std::sqrt(dx*dx+dy*dy); uint8_t v=0;
        if(r<0.95f){ v = r<0.35f?7 : (r<0.6f?5 : 6); if(std::fabs(dx)<0.12f) v=7; }
        w8(SPR+y*SZ+x, v); }

    // the cart's ARM program: configure the scene via MMIO, then render + halt.
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
    for(size_t i=0;i<prog.size();i++) w32(uint32_t(i*4), prog[i]);

    FILE* f=std::fopen(out,"wb");
    if(!f){ std::fprintf(stderr,"!! cannot open %s\n",out); return 1; }
    std::fwrite(rom.data(),1,rom.size(),f); std::fclose(f);
    std::printf("wrote %s (%zu bytes): %zu-instr program + cart data\n", out, rom.size(), prog.size());
    return 0;
}
