// ember32/tools/facade_test.cpp — drive the Ember 32 core through the FamemuCoreAPI
// exactly as the app would: load a tiny ROM, run a frame, read the video, and
// round-trip a save-state.
//   c++ -std=c++17 -O2 -I.. tools/facade_test.cpp famemu_ember32_core.cpp -o /tmp/e32f && /tmp/e32f
#include "../../include/famemu_core.h"
#include <cstdio>
#include <vector>

static void w32(std::vector<uint8_t>& m, uint32_t a, uint32_t v){
    if(a+4>m.size()) m.resize(a+4); m[a]=v; m[a+1]=v>>8; m[a+2]=v>>16; m[a+3]=v>>24;
}

int main(){
    const FamemuCoreAPI* api = famemu_ember32_core();
    std::printf("system_id = %s | sample_rate = %d\n", api->system_id, api->sample_rate());

    // a tiny cartridge: set the backdrop blue (0x40), trigger render, halt.
    std::vector<uint8_t> rom;
    w32(rom, 0,  0xE3A00404);   // mov r0,#0x04000000  (MMIO base)
    w32(rom, 4,  0xE3A01040);   // mov r1,#0x40         (backdrop = blue 0x40)
    w32(rom, 8,  0xE5801000);   // str r1,[r0,#0]
    w32(rom, 12, 0xE3A01001);   // mov r1,#1
    w32(rom, 16, 0xE5801004);   // str r1,[r0,#4]       (RENDER)
    w32(rom, 20, 0xE3E0F000);   // mvn r15,#0           (halt)

    api->load_rom(rom.data(), rom.size());
    api->run_frame();
    int w=0, h=0; const uint8_t* fb = api->video_rgb(&w, &h);
    std::printf("video %dx%d, pixel0 = (%d,%d,%d)  expect (0,0,64)\n", w, h, fb[0], fb[1], fb[2]);

    // save-state round-trip: save, wipe with reset, reload, frame still valid
    size_t ss = api->state_size(); std::vector<uint8_t> st(ss);
    int saved = api->state_save(st.data(), ss);
    api->reset();
    int loaded = api->state_load(st.data(), ss);
    api->set_input(0, FAMEMU_CORE_BTN_A | FAMEMU_CORE_BTN_START);

    bool ok = w==320 && h==240 && fb[0]==0 && fb[1]==0 && fb[2]==0x40 && saved && loaded;
    std::printf("state: %zu bytes, save=%d load=%d\n", ss, saved, loaded);
    std::printf(ok ? "FACADE OK\n" : "!! FACADE FAIL\n");
    api->unload_rom();
    return ok ? 0 : 1;
}
