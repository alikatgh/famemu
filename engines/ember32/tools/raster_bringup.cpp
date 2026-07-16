// ember32/tools/raster_bringup.cpp — per-scanline (HDMA-class) raster tables. A
// single background layer is bent by a per-output-line X-offset table: the top
// half is still, the bottom half gets a sinusoidal offset whose amplitude grows
// with depth — the classic "wavy water reflection" raster trick.
//   c++ -std=c++17 -O2 -I.. tools/raster_bringup.cpp -o /tmp/e32r && /tmp/e32r out.ppm
#include "../compositor.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace ember32;

int main(int argc, char** argv){
    const char* out = argc>1?argv[1]:"e32_raster.ppm";
    static Compositor c;
    c.backdrop = {8, 10, 24};
    c.palette[1]={70,90,150}; c.palette[2]={235,240,255}; c.palette[3]={250,190,70};
    c.palette[4]={40,180,200};

    // Build a 64x64 "banner" image, then slice it into 64 proper 8x8 tiles with
    // an 8x8 tilemap (tile t at grid cell t) — the compositor works in 8x8 tiles.
    const int T=64; static uint8_t img[T*T];
    for(int y=0;y<T;y++)for(int x=0;x<T;x++){
        int idx = (((x/16)^(y/16))&1)?1:2;
        float dx=x-46, dy=y-18; if(dx*dx+dy*dy < 120) idx=3;         // a "sun"
        if((y%16)==0||(x%16)==0) idx=4;                             // grid lines
        img[y*T+x]=idx;
    }
    static uint8_t tiles[64*64];                                    // 64 tiles of 8x8
    for(int t=0;t<64;t++)for(int ry=0;ry<8;ry++)for(int rx=0;rx<8;rx++)
        tiles[t*64 + ry*8+rx] = img[((t/8)*8+ry)*T + (t%8)*8+rx];
    c.tileset={tiles,64};
    static uint16_t map[8*8]; for(int i=0;i<64;i++) map[i]=i;       // the 8x8 image
    c.layers[0].enabled=true; c.layers[0].map=map; c.layers[0].map_w=8; c.layers[0].map_h=8;
    Compositor::make_affine(0,3.0f,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    c.layers[0].wrap=true; c.layers[0].priority=0;

    // per-line X offset table: still up top, rippling below the "waterline"
    static int16_t linex[Compositor::H];
    const int waterline = Compositor::H/2;
    for(int y=0;y<Compositor::H;y++){
        if(y<waterline) linex[y]=0;
        else { float depth=(y-waterline)/float(Compositor::H-waterline);   // 0..1
               linex[y]=int16_t(depth*10.0f*std::sin(y*0.35f)); }
    }
    c.layers[0].line_sx = linex;

    c.render();
    FILE* f=std::fopen(out,"wb"); std::fprintf(f,"P6\n%d %d\n255\n",Compositor::W,Compositor::H);
    for(int i=0;i<Compositor::W*Compositor::H;i++) std::fputc(c.fb[i].r,f),std::fputc(c.fb[i].g,f),std::fputc(c.fb[i].b,f);
    std::fclose(f); std::printf("wrote %s (per-scanline line-scroll: still top, rippling bottom)\n",out);
    return 0;
}
