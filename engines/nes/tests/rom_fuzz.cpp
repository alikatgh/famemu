// ROM-loading fuzz for the clean-room NES core. Hammers load_rom with malformed
// iNES headers across all mapper numbers, size mismatches, trainer edges, and
// truncation; runs frames on any that load (garbage content exercises banking +
// the PPU). Deterministic (fixed seed). A crash / sanitizer error = a real bug.
//
// Build + run (direct binary lets ASAN/UBSan link normally, unlike a
// DYLD-inserted test bundle):
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I. \
//     tests/rom_fuzz.cpp ppu.cpp cpu.cpp apu.cpp -o /tmp/rom_fuzz
//   ASAN_OPTIONS=detect_leaks=0 /tmp/rom_fuzz [iterations]
#include "../system.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using famemu::nes::NesSystem;

int main(int argc, char** argv) {
    const int N = argc > 1 ? std::atoi(argv[1]) : 100000;
    std::mt19937 rng(0xC0FFEEu);
    long loaded = 0, rejected = 0, ran = 0;
    for (int it = 0; it < N; ++it) {
        std::vector<uint8_t> rom;
        switch (rng() % 5) {
        case 0:  // fully random, random length
            rom.resize(rng() % 4096);
            for (auto& b : rom) b = rng() & 0xFF;
            break;
        case 1:  // valid magic + random header + body
            rom.resize(16 + (rng() % 4096));
            for (auto& b : rom) b = rng() & 0xFF;
            std::memcpy(rom.data(), "NES\x1A", 4);
            break;
        case 2: {  // consistent sizes so it CAN load — random mapper + content
            const int prg = 1 + int(rng() % 4), chr = int(rng() % 3);
            const bool trainer = rng() & 1;
            rom.resize(16 + (trainer ? 512 : 0) + size_t(prg) * 16384 + size_t(chr) * 8192);
            for (auto& b : rom) b = rng() & 0xFF;
            std::memcpy(rom.data(), "NES\x1A", 4);
            rom[4] = uint8_t(prg);
            rom[5] = uint8_t(chr);
            rom[6] = (rng() & 0x0F) | (trainer ? 0x04 : 0);
            rom[7] = rng() & 0xF0;
            break;
        }
        case 3:  // trainer bit but truncated
            rom.resize(16 + (rng() % 512));
            for (auto& b : rom) b = rng() & 0xFF;
            std::memcpy(rom.data(), "NES\x1A", 4);
            rom[6] |= 0x04;
            break;
        default:  // tiny (0..16 bytes) header-boundary cases
            rom.resize(rng() % 17);
            for (auto& b : rom) b = rng() & 0xFF;
            if (rom.size() >= 4) std::memcpy(rom.data(), "NES\x1A", 4);
            break;
        }
        NesSystem sys;
        static const uint8_t empty = 0;
        if (sys.load_rom(rom.empty() ? &empty : rom.data(), rom.size())) {
            ++loaded;
            if (ran < 3000) { sys.run_frame(); sys.run_frame(); ++ran; }
        } else {
            ++rejected;
        }
    }
    std::printf("nes rom fuzz: %ld loaded, %ld rejected, ran %ld — NO CRASH, PASS\n",
                loaded, rejected, ran);
    return 0;
}
