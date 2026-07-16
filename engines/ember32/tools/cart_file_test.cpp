// ember32/tools/cart_file_test.cpp — load a real .e32 FILE from disk through the
// FamemuCoreAPI facade, exactly as the app's engine.cpp does (fread the file →
// load_rom(bytes,size) → run_frame → video_rgb). Proves the flat-image cart
// (program at 0, data at 0x1000+) renders through the file path, independent of
// Metal / the Swift app.
//   c++ -std=c++17 -O2 -I.. tools/cart_file_test.cpp famemu_ember32_core.cpp -o /tmp/e32cf && /tmp/e32cf demo.e32
#include "../../include/famemu_core.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv){
    const char* path = argc>1?argv[1]:"demo.e32";
    std::FILE* fp = std::fopen(path,"rb");
    if(!fp){ std::fprintf(stderr,"!! cannot open %s\n",path); return 2; }
    std::fseek(fp,0,SEEK_END); long n=std::ftell(fp); std::fseek(fp,0,SEEK_SET);
    std::vector<uint8_t> rom(n);
    if(std::fread(rom.data(),1,n,fp)!=(size_t)n){ std::fclose(fp); return 2; } std::fclose(fp);

    const FamemuCoreAPI* api = famemu_ember32_core();
    if(!api->load_rom(rom.data(), rom.size())){ std::printf("!! load_rom rejected %ld bytes\n",n); return 1; }
    api->run_frame();
    int w=0,h=0; const uint8_t* fb = api->video_rgb(&w,&h);
    if(!fb||w<=0||h<=0){ std::printf("!! no frame\n"); return 1; }

    // A rendered scene must NOT be a flat backdrop: count distinct-ish pixels and
    // find the brightest (the sprite's core is near-white 0xFFF0A0).
    long lit=0; int maxsum=0,mx=0,my=0;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){ const uint8_t* p=fb+(y*w+x)*3;
        int s=p[0]+p[1]+p[2]; if(s>0x30*3) lit++;
        if(s>maxsum){ maxsum=s; mx=x; my=y; } }
    const uint8_t* bp=fb+((my*w+mx))*3;
    std::printf("video %dx%d | lit(non-backdrop) pixels=%ld | brightest (%d,%d)=(%d,%d,%d)\n",
                w,h,lit,mx,my,bp[0],bp[1],bp[2]);
    bool ok = (w==320 && h==240 && lit>2000 && maxsum>0x280);   // real content, bright sprite present
    std::printf(ok ? "CART-FILE OK\n" : "!! CART-FILE FAIL (looks blank)\n");
    api->unload_rom();
    return ok?0:1;
}
