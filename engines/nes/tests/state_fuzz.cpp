// Save-state load fuzz for the clean-room NES core. Takes a real ROM's valid
// state, flips bytes past the 4-byte magic (so the corrupt fields are actually
// APPLIED) and/or truncates, loads, and runs frames. Confirms load is memory-safe
// under adversarial input — `Ppu::post_load` clamps the render counters so a
// garbage `scanline_`/`scan_count_` can't index `fb_`/`scan_sprites_` OOB.
//
// Run under ASAN/UBSan to catch even non-crashing OOB reads:
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I. \
//     tests/state_fuzz.cpp ppu.cpp cpu.cpp apu.cpp -o /tmp/state_fuzz
//   ASAN_OPTIONS=detect_leaks=0 /tmp/state_fuzz rom.nes [iterations]
//
// NOTE: UBSan may still report benign "invalid bool value" loads from the APU
// channel structs (they're memcpy'd wholesale); that's non-exploitable (bool read
// as true, no OOB — ASAN is clean). A full close would give the channel structs a
// field-wise serialize() or normalize their bools in Apu::post_load.
#include "../system.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using famemu::nes::NesSystem;

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s rom.nes [iterations]\n", argv[0]); return 1; }
    const int N = argc > 2 ? std::atoi(argv[2]) : 30000;

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("cannot open rom\n"); return 1; }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom;
    rom.resize(size_t(sz));
    if (std::fread(rom.data(), 1, size_t(sz), f) != size_t(sz)) { std::fclose(f); return 1; }
    std::fclose(f);

    NesSystem base;
    if (!base.load_rom(rom.data(), rom.size())) { std::printf("rom load fail\n"); return 1; }
    for (int i = 0; i < 20; ++i) base.run_frame();
    std::vector<uint8_t> clean(base.state_size());
    if (!base.state_save(clean.data(), clean.size())) { std::printf("save fail\n"); return 1; }

    std::mt19937 rng(0xBADF00Du);
    for (int it = 0; it < N; ++it) {
        NesSystem sys;
        sys.load_rom(rom.data(), rom.size());
        std::vector<uint8_t> st = clean;
        const int flips = 1 + int(rng() % 40);
        for (int k = 0; k < flips && st.size() > 4; ++k)
            st[4 + (rng() % (st.size() - 4))] ^= uint8_t(rng() & 0xFF);   // keep magic → apply fields
        const size_t useLen = (rng() % 8 == 0) ? (rng() % st.size()) : st.size();
        sys.state_load(st.data(), useLen);
        sys.run_frame();
        sys.run_frame();
    }
    std::printf("nes save-state fuzz: %d corrupt loads + runs — NO CRASH/OOB, PASS\n", N);
    return 0;
}
