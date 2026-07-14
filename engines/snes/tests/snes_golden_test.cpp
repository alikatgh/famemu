// SNES golden-screen regression: run each ROM listed in a manifest for N
// frames and compare the framebuffer CRC32 against the recorded golden.
// Catches any CPU/PPU behavior change that alters a verified screen (the
// peterlemon CPU test ROMs stall on their first failing case, so a changed
// screen means a changed verdict). ROMs + manifest live in tests/data/
// (gitignored: game/test ROM content stays out of the public repo).
//
//   snes_golden_test <manifest>       — verify all entries
//   snes_golden_test <manifest> --gen — print a fresh manifest to stdout
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../snes_system.hpp"

using namespace famemu::snes;

static uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <manifest> [--gen]\n", argv[0]);
        return 2;
    }
    const bool gen = argc > 2 && !std::strcmp(argv[2], "--gen");
    std::FILE* mf = std::fopen(argv[1], "rb");
    if (!mf) { std::fprintf(stderr, "cannot open manifest %s\n", argv[1]); return 2; }

    // manifest dir = prefix for relative ROM names
    std::string dir = argv[1];
    auto slash = dir.find_last_of('/');
    dir = (slash == std::string::npos) ? "" : dir.substr(0, slash + 1);

    char line[512];
    int checked = 0, failed = 0;
    while (std::fgets(line, sizeof line, mf)) {
        char rom[256];
        int frames = 0;
        unsigned expect = 0;
        if (line[0] == '#' || std::sscanf(line, "%255s %d %x", rom, &frames, &expect) < 2)
            continue;
        const std::string path = dir + rom;
        std::FILE* rf = std::fopen(path.c_str(), "rb");
        if (!rf) { std::fprintf(stderr, "missing ROM %s (skipped)\n", path.c_str()); continue; }
        std::fseek(rf, 0, SEEK_END);
        long sz = std::ftell(rf);
        std::fseek(rf, 0, SEEK_SET);
        std::vector<uint8_t> data(sz > 0 ? static_cast<size_t>(sz) : 0);
        if (data.empty() || std::fread(data.data(), 1, data.size(), rf) != data.size()) {
            std::fclose(rf);
            continue;
        }
        std::fclose(rf);

        SnesSystem sys;
        if (!sys.load_rom(data.data(), data.size())) continue;
        for (int i = 0; i < frames; ++i) sys.run_frame();
        const uint32_t got = crc32(sys.framebuffer(),
                                   static_cast<size_t>(sys.ppu().width()) * SPpu::kHeight * 3);
        ++checked;
        if (gen) {
            std::printf("%s %d %08X\n", rom, frames, got);
        } else if (got != expect) {
            std::fprintf(stderr, "GOLDEN MISMATCH %s: got %08X expect %08X\n",
                         rom, got, expect);
            ++failed;
        }
    }
    std::fclose(mf);
    if (!gen) std::printf("golden screens: %d checked, %d mismatched\n", checked, failed);
    return failed ? 1 : 0;
}
