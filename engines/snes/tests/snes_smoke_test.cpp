// SNES engine smoke test: boot a ROM, assert the screen lights up and the
// SPC700 driver is running with audible DSP output. The ROM is NOT a repo
// fixture (game content stays out of the public repo): copy one to
// engines/snes/tests/data/smoke.sfc locally (KORA is the canonical choice)
// and the ctest target registers itself.
#include <cstdio>
#include <vector>

#include "../snes_system.hpp"

using namespace famemu::snes;

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <rom.sfc>\n", argv[0]); return 2; }
    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> rom(sz > 0 ? static_cast<size_t>(sz) : 0);
    if (rom.empty() || std::fread(rom.data(), 1, rom.size(), fp) != rom.size()) {
        std::fclose(fp);
        return 2;
    }
    std::fclose(fp);

    SnesSystem sys;
    if (!sys.load_rom(rom.data(), rom.size())) { std::fprintf(stderr, "bad ROM\n"); return 1; }

    for (int i = 0; i < 120; ++i) sys.run_frame();

    // Video: some non-black pixels.
    const uint8_t* fb = sys.framebuffer();
    long lit = 0;
    for (int i = 0; i < SPpu::kWidth * SPpu::kHeight; ++i)
        if (fb[i * 3] + fb[i * 3 + 1] + fb[i * 3 + 2] > 24) ++lit;
    if (lit < 500) {
        std::fprintf(stderr, "screen stayed dark (%ld lit px)\n", lit);
        return 1;
    }

    // Audio: drain and require signal.
    std::vector<int16_t> pcm(32000 * 2);
    size_t got = sys.read_audio(pcm.data(), 32000);
    long energy = 0;
    for (size_t i = 0; i < got * 2; ++i) energy += pcm[i] > 0 ? pcm[i] : -pcm[i];
    const long avg = got ? energy / static_cast<long>(got * 2) : 0;
    if (got < 8000 || avg < 50) {
        std::fprintf(stderr, "audio missing/quiet (frames=%zu avg=%ld)\n", got, avg);
        return 1;
    }
    std::printf("snes smoke: %ld lit px, %zu audio frames (avg |s|=%ld) — PASS\n",
                lit, got, avg);
    return 0;
}
