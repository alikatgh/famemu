// Adversarial SNES CPU fuzz: mutate a real ROM (header + code) and run frames.
// A loaded, mutated ROM runs garbage 65816 code — exercising the CPU / bus /
// SPC700 / PPU on inputs the well-formed golden ROMs never reach. Built to catch
// OOB and integer UB on the hostile path. It found (and now guards) a shift-UB in
// the LoROM SRAM-size decode: snes_system_impl.hpp did `1024u << sram_kb_log`
// where sram_kb_log is a raw header byte (0-255) → shift-by->=32 UB on garbage.
// Fixed by treating an implausible size (> 12) as "no SRAM". Real ROMs unchanged
// (golden CRCs identical). Does NOT prove functional correctness — only memory /
// UB safety of the load+run path under adversarial input.
//
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I. -I.. \
//     tests/snes_cpu_fuzz.cpp cpu65816.cpp sdsp.cpp spc700.cpp sppu.cpp -o /tmp/f
//   ASAN_OPTIONS=detect_leaks=0 /tmp/f tests/data/<any>.sfc 1500
#include "../snes_system.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using famemu::snes::SnesSystem;
int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s rom.sfc [iters]\n", argv[0]); return 1; }
    const int N = argc > 2 ? std::atoi(argv[2]) : 3000;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("cannot open %s\n", argv[1]); return 1; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> base; base.resize(size_t(sz));
    if (std::fread(base.data(), 1, size_t(sz), f) != size_t(sz)) { std::fclose(f); return 1; }
    std::fclose(f);

    std::mt19937 rng(0x5EEDu);
    long loaded = 0;
    for (int it = 0; it < N; ++it) {
        std::vector<uint8_t> rom = base;
        const int flips = 1 + int(rng() % 200);
        for (int k = 0; k < flips; ++k) rom[rng() % rom.size()] ^= uint8_t(rng() & 0xFF);
        SnesSystem sys;
        if (sys.load_rom(rom.data(), rom.size())) {
            ++loaded;
            for (int fr = 0; fr < 3; ++fr) sys.run_frame();
        }
    }
    std::printf("snes adversarial ROM fuzz: %d ROMs (%ld loaded+run garbage code) — no OOB, PASS\n", N, loaded);
    return 0;
}
