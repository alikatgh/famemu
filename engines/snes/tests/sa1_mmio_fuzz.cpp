// SA-1 command-port fuzz: hammer the SA-1 MMIO registers (0x2200-0x23FF) with
// random writes, interleaving reads and run_line() steps (each runs the SA-1's
// own 65816 core + DMA / arithmetic units over whatever state the MMIO set up).
// Built to catch OOB / UB reachable purely through a hostile driver poking the
// command ports — the golden ROMs drive SA-1 only through legitimate sequences.
// 300k ops came back clean (no OOB, no UB). Memory/UB safety only, not behavior.
//
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I. -I.. \
//     tests/sa1_mmio_fuzz.cpp cpu65816.cpp sdsp.cpp spc700.cpp sppu.cpp -o /tmp/f
//   ASAN_OPTIONS=detect_leaks=0 /tmp/f tests/data/<any>.sfc 300000
#include "../snes_system.hpp"
#include "../sa1.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace famemu::snes;
int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s host.sfc [ops]\n", argv[0]); return 1; }
    const int N = argc > 2 ? std::atoi(argv[2]) : 300000;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("cannot open %s\n", argv[1]); return 1; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom; rom.resize(size_t(sz));
    if (std::fread(rom.data(), 1, size_t(sz), f) != size_t(sz)) { std::fclose(f); return 1; }
    std::fclose(f);

    SnesSystem sys;
    sys.load_rom(rom.data(), rom.size());  // host system for the Sa1 ctor / bus
    std::mt19937 rng(0xA1u);
    Sa1 sa1(sys);
    sa1.reset();
    for (int i = 0; i < N; ++i) {
        sa1.write(0x2200u + (rng() % 0x200u), uint8_t(rng() & 0xFF));
        if ((rng() & 7u) == 0) (void)sa1.read(0x2300u + (rng() % 0x100u));
        if ((rng() & 15u) == 0) sa1.run_line();
    }
    std::printf("sa1 mmio fuzz: %d ops — no OOB, PASS\n", N);
    return 0;
}
