// SNES save-state round-trip: run, snapshot, diverge, restore, replay —
// framebuffer + CPU must be bit-identical. Same local-ROM gating as the
// smoke test (engines/snes/tests/data/smoke.sfc, gitignored).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../snes_system.hpp"

using namespace famemu::snes;

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <rom.sfc>\n", argv[0]); return 2; }
    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp) return 2;
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
    if (!sys.load_rom(rom.data(), rom.size())) return 2;
    for (int i = 0; i < 90; ++i) sys.run_frame();

    std::vector<uint8_t> snap(sys.state_size());
    if (!sys.state_save(snap.data(), snap.size())) {
        std::fprintf(stderr, "save failed (size=%zu)\n", snap.size());
        return 1;
    }

    sys.set_buttons(1u << 3);  // hold Start into the divergence
    for (int i = 0; i < 30; ++i) sys.run_frame();
    std::vector<uint8_t> fb_a(sys.framebuffer(),
                              sys.framebuffer() + SPpu::kWidth * SPpu::kHeight * 3);
    const uint16_t pc_a = sys.cpu().pc;

    if (!sys.state_load(snap.data(), snap.size())) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }
    sys.set_buttons(1u << 3);
    for (int i = 0; i < 30; ++i) sys.run_frame();

    if (std::memcmp(fb_a.data(), sys.framebuffer(), fb_a.size()) != 0) {
        std::fprintf(stderr, "framebuffer diverged after state load\n");
        return 1;
    }
    if (sys.cpu().pc != pc_a) {
        std::fprintf(stderr, "cpu diverged: %04X vs %04X\n", sys.cpu().pc, pc_a);
        return 1;
    }
    std::printf("snes state round-trip: %zu-byte snapshot, replay bit-identical — PASS\n",
                snap.size());
    return 0;
}
