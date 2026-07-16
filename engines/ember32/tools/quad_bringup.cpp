// ember32/tools/quad_bringup.cpp — the optional textured-quad unit. Draws a
// receding perspective floor (a textured trapezoid) and a rotated textured
// banner, showing the "3-D as a distorted sprite" affine mapping the spec calls
// for. Layers/sprites still compose around it by priority.
//   c++ -std=c++17 -O2 -I.. tools/quad_bringup.cpp -o /tmp/e32q && /tmp/e32q out.ppm
#include "../compositor.hpp"
#include <cstdio>
#include <cmath>
using namespace ember32;

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "e32_quad.ppm";
    static Compositor c;
    c.backdrop = {30, 24, 56};                        // dusk sky

    // palette + a bordered checker texture (so the affine warp is legible)
    c.palette[1]={70,80,150}; c.palette[2]={230,236,255}; c.palette[3]={250,180,60};
    c.palette[4]={40,200,160}; c.palette[5]={20,24,40};
    const int T=64; static uint8_t checker[T*T];
    for(int y=0;y<T;y++) for(int x=0;x<T;x++){
        bool b=((x/8)^(y/8))&1; checker[y*T+x] = (x<2||y<2||x>=T-2||y>=T-2)?3:(b?2:1);
    }
    static uint8_t band[T*T];                          // banner texture
    for(int y=0;y<T;y++) for(int x=0;x<T;x++) band[y*T+x] = ((x/6+y/6)&1)?4:5;

    // a faint sky layer so the scene isn't flat
    static uint8_t sky[64]; for(int p=0;p<64;p++) sky[p]=(p/8<2)?2:0;   // top stripe only
    static uint16_t skymap[16*16]={};
    c.tileset={sky,1};
    c.layers[0].enabled=true; c.layers[0].map=skymap; c.layers[0].map_w=16; c.layers[0].map_h=16;
    Compositor::make_affine(0,4.0f,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    c.layers[0].priority=0; c.layers[0].wrap=true;

    // Quad 0 — a receding floor: narrow at the horizon, wide at the bottom.
    Quad& fl=c.quads[c.quad_count++];
    fl.enabled=true; fl.tex=checker; fl.tw=T; fl.th=T; fl.priority=1; fl.blend=BLEND_NONE;
    fl.x[0]=120; fl.y[0]=132;  fl.x[1]=200; fl.y[1]=132;      // top (horizon)
    fl.x[2]=310; fl.y[2]=238;  fl.x[3]=10;  fl.y[3]=238;      // bottom (wide)
    fl.u[0]=0;   fl.v[0]=0;     fl.u[1]=T;   fl.v[1]=0;
    fl.u[2]=T;   fl.v[2]=T;     fl.u[3]=0;   fl.v[3]=T;

    // Quad 1 — a rotated textured banner (arbitrary 4 corners), alpha over the floor.
    Quad& bn=c.quads[c.quad_count++];
    bn.enabled=true; bn.tex=band; bn.tw=T; bn.th=T; bn.priority=2; bn.blend=BLEND_ALPHA; bn.alpha=220;
    float cx=160,cy=96,rw=92,rh=30,ang=-0.18f,s=std::sin(ang),co=std::cos(ang);
    float corners[4][2]={{-rw,-rh},{rw,-rh},{rw,rh},{-rw,rh}};
    for(int i=0;i<4;i++){ bn.x[i]=cx+corners[i][0]*co-corners[i][1]*s;
                          bn.y[i]=cy+corners[i][0]*s +corners[i][1]*co; }
    bn.u[0]=0; bn.v[0]=0; bn.u[1]=T; bn.v[1]=0; bn.u[2]=T; bn.v[2]=T; bn.u[3]=0; bn.v[3]=T;

    c.render();

    FILE* f=std::fopen(out,"wb");
    std::fprintf(f,"P6\n%d %d\n255\n",Compositor::W,Compositor::H);
    for(int i=0;i<Compositor::W*Compositor::H;i++)
        std::fputc(c.fb[i].r,f),std::fputc(c.fb[i].g,f),std::fputc(c.fb[i].b,f);
    std::fclose(f);
    std::printf("wrote %s (%d quads: perspective floor + rotated banner)\n",out,c.quad_count);
    return 0;
}
