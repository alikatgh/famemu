// ember32/tools/fastcore_test.cpp — verify the OPTIMISED compositor
// (compositor_fast.hpp) is PIXEL-FOR-PIXEL identical to the reference
// (compositor.hpp) on the feature-cart suite, and measure the speedup. This is
// the contract the fast core owes the reference (README "Verification").
//
// For each scene: build it once, render with BOTH cores, and require
//   hash(reference) == hash(fast) == the frozen golden from test_features.cpp.
//   c++ -std=c++17 -O2 -I.. tools/fastcore_test.cpp -o /tmp/e32fast && /tmp/e32fast
#include "../compositor.hpp"
#include "../compositor_fast.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <functional>
using namespace ember32;

static uint64_t fnv(const void* p, size_t n){
    const uint8_t* b=(const uint8_t*)p; uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<n;i++){ h^=b[i]; h*=1099511628211ull; } return h;
}
static uint64_t hashFB(const Compositor& c){ return fnv(c.fb, sizeof(c.fb)); }

// --- scene builders (identical to test_features.cpp, minus the render call) ---
static void checkerTiles(Compositor& c){
    static uint8_t t[64]; for(int p=0;p<64;p++) t[p]=((p%8/4)^(p/8/4))?1:2; c.tileset={t,1};
    c.palette[1]={40,60,120}; c.palette[2]={220,225,240}; c.palette[3]={255,160,40};
    c.palette[4]={60,200,180}; c.palette[5]={255,90,60};
}
static uint8_t* orb(){ static uint8_t o[32*32]; for(int y=0;y<32;y++)for(int x=0;x<32;x++){
    float dx=x-16+.5f,dy=y-16+.5f,r=std::sqrt(dx*dx+dy*dy)/16; o[y*32+x]=r>1?0:(r>.6f?5:3);} return o; }
static uint16_t* map1(){ static uint16_t m[16*16]={}; return m; }

static void b_backdrop(Compositor& c){ c.backdrop={30,40,70}; checkerTiles(c);
    c.layers[0]={}; c.layers[0].enabled=true; c.layers[0].map=map1(); c.layers[0].map_w=16; c.layers[0].map_h=16; }
static void b_affine(Compositor& c){ checkerTiles(c); c.layers[0].enabled=true; c.layers[0].map=map1();
    c.layers[0].map_w=16; c.layers[0].map_h=16; Compositor::make_affine(0.4f,2.0f,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d); }
static void b_alpha(Compositor& c){ checkerTiles(c); c.layers[0].enabled=true; c.layers[0].map=map1();
    c.layers[0].map_w=16; c.layers[0].map_h=16; c.sprites[0]={}; c.sprites[0].enabled=true; c.sprites[0].x=160;
    c.sprites[0].y=120; c.sprites[0].w=32; c.sprites[0].h=32; c.sprites[0].pixels=orb(); c.sprites[0].blend=BLEND_ALPHA;
    c.sprites[0].alpha=140; c.sprites[0].priority=1; Compositor::make_affine(0,2,c.sprites[0].a,c.sprites[0].b,c.sprites[0].c,c.sprites[0].d);
    c.sprite_count=1; }
static void b_additive(Compositor& c){ checkerTiles(c); c.sprites[0]={}; c.sprites[0].enabled=true;
    c.sprites[0].x=160; c.sprites[0].y=120; c.sprites[0].w=32; c.sprites[0].h=32; c.sprites[0].pixels=orb();
    c.sprites[0].blend=BLEND_ADD; c.sprites[0].alpha=200; Compositor::make_affine(0.2f,3,c.sprites[0].a,c.sprites[0].b,c.sprites[0].c,c.sprites[0].d);
    c.sprite_count=1; }
static void b_priority(Compositor& c){ c.backdrop={20,20,44};
    static uint8_t t[64]; for(int p=0;p<64;p++) t[p]=((p%8/4)^(p/8/4))?1:0;
    c.palette[1]={60,80,150}; c.palette[3]={255,160,40}; c.palette[5]={255,90,60}; c.tileset={t,1};
    c.layers[0].enabled=true; c.layers[0].map=map1(); c.layers[0].map_w=16; c.layers[0].map_h=16; c.layers[0].priority=1;
    Compositor::make_affine(0,2,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    c.sprites[0]={}; c.sprites[0].enabled=true; c.sprites[0].x=160; c.sprites[0].y=120; c.sprites[0].w=32; c.sprites[0].h=32;
    c.sprites[0].pixels=orb(); c.sprites[0].priority=0;
    Compositor::make_affine(0,2,c.sprites[0].a,c.sprites[0].b,c.sprites[0].c,c.sprites[0].d);
    c.sprite_count=1; }
static void b_quad(Compositor& c){ checkerTiles(c); c.quads[0]={}; Quad& q=c.quads[0]; q.enabled=true;
    q.tex=orb(); q.tw=32; q.th=32; q.x[0]=40;q.y[0]=40;q.x[1]=280;q.y[1]=60;q.x[2]=300;q.y[2]=200;q.x[3]=20;q.y[3]=210;
    q.u[0]=0;q.v[0]=0;q.u[1]=32;q.v[1]=0;q.u[2]=32;q.v[2]=32;q.u[3]=0;q.v[3]=32; c.quad_count=1; }
static int16_t* ripple(){ static int16_t lx[Compositor::H]; for(int y=0;y<Compositor::H;y++) lx[y]=int16_t(8*std::sin(y*0.3f)); return lx; }
static void b_linescroll(Compositor& c){ checkerTiles(c); c.layers[0].enabled=true; c.layers[0].map=map1();
    c.layers[0].map_w=16; c.layers[0].map_h=16; Compositor::make_affine(0,2,c.layers[0].a,c.layers[0].b,c.layers[0].c,c.layers[0].d);
    c.layers[0].line_sx=ripple(); }

int main(){
    struct Case { const char* name; std::function<void(Compositor&)> build; uint64_t golden; };
    Case cases[] = {
        {"backdrop+layer", b_backdrop,   0x366F59098B111D83ull},
        {"affine layer",   b_affine,     0x886ABD33F4717F75ull},
        {"alpha sprite",   b_alpha,      0xCF1EB12ED73CEE8Bull},
        {"additive blend", b_additive,   0x74F03EB7E5EF329Full},
        {"priority",       b_priority,   0xA450FE10918D5CFBull},
        {"textured quad",  b_quad,       0xEB803E5642AC0D2Eull},
        {"line-scroll",    b_linescroll, 0x1598508A4F100383ull},
    };
    int pass=0, n=sizeof(cases)/sizeof(cases[0]);
    for(auto& c : cases){
        static Compositor comp; comp = Compositor(); c.build(comp);
        comp.render();              uint64_t hRef = hashFB(comp);
        render_fast(comp);          uint64_t hFast = hashFB(comp);
        bool ok = hRef==c.golden && hFast==c.golden;
        pass += ok;
        std::printf("  [%s] %-16s ref=%016llX fast=%016llX %s\n",
            ok?"PASS":"FAIL", c.name, (unsigned long long)hRef, (unsigned long long)hFast,
            hFast==hRef ? "(identical)" : "!! DIVERGED");
    }
    std::printf("%d/%d pixel-identical\n", pass, n);

    // --- speed: render each scene REPS times with each core, aggregate wall time.
    const int REPS = 1500;
    static Compositor cs[7];
    for(int i=0;i<n;i++){ cs[i]=Compositor(); cases[i].build(cs[i]); }
    auto t0=std::chrono::steady_clock::now();
    for(int r=0;r<REPS;r++) for(int i=0;i<n;i++) cs[i].render();
    auto t1=std::chrono::steady_clock::now();
    for(int r=0;r<REPS;r++) for(int i=0;i<n;i++) render_fast(cs[i]);
    auto t2=std::chrono::steady_clock::now();
    double refms  = std::chrono::duration<double,std::milli>(t1-t0).count();
    double fastms = std::chrono::duration<double,std::milli>(t2-t1).count();
    std::printf("  timing over %d x %d renders: reference %.1f ms, fast %.1f ms  -> %.2fx\n",
        REPS, n, refms, fastms, refms/fastms);

    // Isolate the separable full-screen layer win (the common case).
    static Compositor big; big=Compositor(); b_backdrop(big);
    auto s0=std::chrono::steady_clock::now(); for(int r=0;r<REPS*4;r++) big.render();
    auto s1=std::chrono::steady_clock::now(); for(int r=0;r<REPS*4;r++) render_fast(big);
    auto s2=std::chrono::steady_clock::now();
    double br=std::chrono::duration<double,std::milli>(s1-s0).count();
    double bf=std::chrono::duration<double,std::milli>(s2-s1).count();
    std::printf("  separable full-screen layer: reference %.1f ms, fast %.1f ms  -> %.2fx\n", br, bf, br/bf);

    return pass==n ? 0 : 1;
}
