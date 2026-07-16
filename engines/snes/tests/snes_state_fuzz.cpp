// SNES save-state fuzz: snapshot a running system, then bit-flip / truncate the
// bytes and load them back + run frames. A corrupt or hostile save-state must not
// crash the SNES core (OOB) — the same class as the NES PPU-counter OOB and the
// SPC7110 index. 20k corrupt states came back clean (bounded reads + invalid ones
// rejected by state_load). Memory/UB safety only, not correctness.
//
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I. -I.. \
//     tests/snes_state_fuzz.cpp cpu65816.cpp sdsp.cpp spc700.cpp sppu.cpp -o /tmp/f
//   ASAN_OPTIONS=detect_leaks=0 /tmp/f tests/data/<any>.sfc 20000
#include "../snes_system.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace famemu::snes;
int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s rom.sfc [iters]\n", argv[0]); return 1; }
    const int N = argc > 2 ? std::atoi(argv[2]) : 20000;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("cannot open %s\n", argv[1]); return 1; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom; rom.resize(size_t(sz));
    if (std::fread(rom.data(), 1, size_t(sz), f) != size_t(sz)) { std::fclose(f); return 1; }
    std::fclose(f);

    SnesSystem seed;
    if (!seed.load_rom(rom.data(), rom.size())) { std::printf("rom not loadable\n"); return 1; }
    for (int i = 0; i < 120; ++i) seed.run_frame();
    std::vector<uint8_t> base(seed.state_size());
    if (!seed.state_save(base.data(), base.size())) { std::printf("save failed\n"); return 1; }

    std::mt19937 rng(0x57A7Eu);
    long loaded = 0;
    for (int it = 0; it < N; ++it) {
        std::vector<uint8_t> st = base;
        const int flips = 1 + int(rng() % 64);
        for (int k = 0; k < flips; ++k) st[rng() % st.size()] ^= uint8_t(1u << (rng() & 7u));
        if ((rng() & 3u) == 0 && st.size() > 16) st.resize(rng() % st.size());  // truncation
        SnesSystem sys;
        sys.load_rom(rom.data(), rom.size());
        if (sys.state_load(st.data(), st.size())) {
            ++loaded;
            for (int fr = 0; fr < 3; ++fr) sys.run_frame();
        }
    }
    std::printf("snes save-state fuzz: %d corrupt states (%ld loaded+run) — no OOB, PASS\n", N, loaded);
    return 0;
}
