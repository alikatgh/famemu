// ember32/tools/test_features.cpp — the feature/verification gate (phase 4).
//
// Each case exercises one feature of the reference model (compositor or CPU),
// hashes the result (framebuffer or register state), and checks it against a
// frozen GOLDEN hash. This is the contract the eventual FAST core is verified
// pixel-for-pixel against, and it belongs in CI: any change that alters output
// flips a hash. Run with `--bless` to print the current hashes (to update
// GOLDEN after an intentional change).
//   c++ -std=c++17 -O2 -I.. tools/test_features.cpp -o /tmp/e32test && /tmp/e32test
#include "../cpu_arm7.hpp"
#include "../compositor.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
using namespace ember32;

static uint64_t fnv(const void* p, size_t n){
    const uint8_t* b=(const uint8_t*)p; uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<n;i++){ h^=b[i]; h*=1099511628211ull; } return h;
}
static uint64_t hashFB(const Compositor& c){ return fnv(c.fb, sizeof(c.fb)); }

// -- scene builders (each returns a hash of what it produced) -----------------
static void checkerTiles(Compositor& c){
    static uint8_t t[64]; for(int p=0;p<64;p++) t[p]=((p%8/4)^(p/8/4))?1:2; c.tileset={t,1};
    c.palette[1]={40,60,120}; c.palette[2]={220,225,240}; c.palette[3]={255,160,40};
    c.palette[4]={60,200,180}; c.palette[5]={255,90,60};
}
static uint8_t* orb(){ static uint8_t o[32*32]; for(int y=0;y<32;y++)for(int x=0;x<32;x++){
    float dx=x-16+.5f,dy=y-16+.5f,r=std::sqrt(dx*dx+dy*dy)/16; o[y*32+x]=r>1?0:(r>.6f?5:3);} return o; }
static uint16_t* map1(){ static uint16_t m[16*16]={}; return m; }

static uint64_t c_backdrop(){ Compositor c; c.backdrop={30,40,70}; checkerTiles(c);
    c.layers[0]={}; c.layers[0].enabled=true; c.layers[0].map=map1(); c.layers[0].map_w=16; c.layers[0].map_h=16;
    c.render(); return hashFB(c); }
static uint64_t c_affine(){ Compositor c; checkerTiles(c); c.layers[0].enabled=true; c.layers[0].map=map1();
    c.layers[0].map_w=16; c.layers[0].map_h=16; Compositor::make_affine(0.4f,2.0f,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    c.render(); return hashFB(c); }
static uint64_t c_alpha(){ Compositor c; checkerTiles(c); c.layers[0].enabled=true; c.layers[0].map=map1();
    c.layers[0].map_w=16; c.layers[0].map_h=16; c.sprites[0]={}; c.sprites[0].enabled=true; c.sprites[0].x=160;
    c.sprites[0].y=120; c.sprites[0].w=32; c.sprites[0].h=32; c.sprites[0].pixels=orb(); c.sprites[0].blend=BLEND_ALPHA;
    c.sprites[0].alpha=140; c.sprites[0].priority=1; Compositor::make_affine(0,2,c.sprites[0].a,c.sprites[0].b,c.sprites[0].c,c.sprites[0].d);
    c.sprite_count=1; c.render(); return hashFB(c); }
static uint64_t c_additive(){ Compositor c; checkerTiles(c); c.sprites[0]={}; c.sprites[0].enabled=true;
    c.sprites[0].x=160; c.sprites[0].y=120; c.sprites[0].w=32; c.sprites[0].h=32; c.sprites[0].pixels=orb();
    c.sprites[0].blend=BLEND_ADD; c.sprites[0].alpha=200; Compositor::make_affine(0.2f,3,c.sprites[0].a,c.sprites[0].b,c.sprites[0].c,c.sprites[0].d);
    c.sprite_count=1; c.render(); return hashFB(c); }
static uint64_t c_priority(){ Compositor c; c.backdrop={20,20,44};
    static uint8_t t[64]; for(int p=0;p<64;p++) t[p]=((p%8/4)^(p/8/4))?1:0;   // half transparent (index 0)
    c.palette[1]={60,80,150}; c.palette[3]={255,160,40}; c.palette[5]={255,90,60}; c.tileset={t,1};
    c.layers[0].enabled=true; c.layers[0].map=map1(); c.layers[0].map_w=16; c.layers[0].map_h=16; c.layers[0].priority=1;
    Compositor::make_affine(0,2,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    c.sprites[0]={}; c.sprites[0].enabled=true; c.sprites[0].x=160; c.sprites[0].y=120; c.sprites[0].w=32; c.sprites[0].h=32;
    c.sprites[0].pixels=orb(); c.sprites[0].priority=0;                       // BEHIND — peeks through the gaps
    Compositor::make_affine(0,2,c.sprites[0].a,c.sprites[0].b,c.sprites[0].c,c.sprites[0].d);
    c.sprite_count=1; c.render(); return hashFB(c); }
static uint64_t c_quad(){ Compositor c; checkerTiles(c); c.quads[0]={}; Quad& q=c.quads[0]; q.enabled=true;
    q.tex=orb(); q.tw=32; q.th=32; q.x[0]=40;q.y[0]=40;q.x[1]=280;q.y[1]=60;q.x[2]=300;q.y[2]=200;q.x[3]=20;q.y[3]=210;
    q.u[0]=0;q.v[0]=0;q.u[1]=32;q.v[1]=0;q.u[2]=32;q.v[2]=32;q.u[3]=0;q.v[3]=32; c.quad_count=1; c.render(); return hashFB(c); }
static uint64_t c_linescroll(){ Compositor c; checkerTiles(c); c.layers[0].enabled=true; c.layers[0].map=map1();
    c.layers[0].map_w=16; c.layers[0].map_h=16; Compositor::make_affine(0,2,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    static int16_t lx[Compositor::H]; for(int y=0;y<Compositor::H;y++) lx[y]=int16_t(8*std::sin(y*0.3f)); c.layers[0].line_sx=lx;
    c.render(); return hashFB(c); }
static uint64_t c_arm(){ Bus b;   // r0=7; r0=r0*6; r0+=5 → 47; store to 0x400; halt
    b.w32(0,0xE3A00007); b.w32(4,0xE3A01006); b.w32(8,0xE0000091);   // r0=7, r1=6, r0=r1*r0=42
    b.w32(12,0xE2800005); b.w32(16,0xE3A03B01); b.w32(20,0xE5830000); b.w32(24,0xE3E0F000); // +5=47, r3=0x400, str, halt
    CPU cpu; cpu.bus=&b; cpu.reset(0); cpu.run(1000); uint32_t v=b.r32(0x400); return fnv(&v,4)^(cpu.halted?1:0); }
static uint64_t c_thumb(){ Bus b; b.w32(0,0xE3A00011); b.w32(4,0xE12FFF10);
    uint16_t t[]={0x2104,0x2205,0x4351,0x3103,0x2340,0x009B,0x6019,0x2030,0x4700};
    for(size_t i=0;i<9;i++) b.w16(0x10+i*2,t[i]); b.w32(0x30,0xE3E0F000);
    CPU cpu; cpu.bus=&b; cpu.reset(0); cpu.run(1000); uint32_t v=b.r32(0x100); return fnv(&v,4)^(cpu.halted?1:0); }

int main(int argc, char** argv){
    bool bless = argc>1 && !std::strcmp(argv[1],"--bless");
    struct Case { const char* name; std::function<uint64_t()> run; uint64_t golden; };
    Case cases[] = {
        {"backdrop+layer", c_backdrop,   0x366F59098B111D83ull},
        {"affine layer",   c_affine,     0x886ABD33F4717F75ull},
        {"alpha sprite",   c_alpha,      0xCF1EB12ED73CEE8Bull},
        {"additive blend", c_additive,   0x74F03EB7E5EF329Full},
        {"priority",       c_priority,   0xA450FE10918D5CFBull},
        {"textured quad",  c_quad,       0xEB803E5642AC0D2Eull},
        {"line-scroll",    c_linescroll, 0x1598508A4F100383ull},
        {"cpu ARM alu",    c_arm,        0xD079D3F6553D646Dull},
        {"cpu Thumb",      c_thumb,      0xD0F9B32E4033B6D5ull},
    };
    int pass=0, n=sizeof(cases)/sizeof(cases[0]);
    for(auto& c : cases){
        uint64_t h=c.run();
        if(bless) std::printf("        {\"%s\", ...  0x%016llXull},\n", c.name, (unsigned long long)h);
        else { bool ok=(h==c.golden); pass+=ok; std::printf("  [%s] %-16s %016llX\n", ok?"PASS":"FAIL", c.name, (unsigned long long)h); }
    }
    if(!bless) std::printf("%d/%d passed\n", pass, n);
    return bless?0:(pass==n?0:1);
}
