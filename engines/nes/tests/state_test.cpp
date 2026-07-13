// Save-state round-trip determinism: run N frames, snapshot, run K more and
// record the framebuffer + CPU; restore the snapshot, run K again — the
// replay must be bit-identical. Uses nestest.nes (committed fixture).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../system.hpp"

using namespace famemu::nes;

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <rom.nes>\n", argv[0]); return 2; }
    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp) return 2;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(sz));
    if (std::fread(rom.data(), 1, rom.size(), fp) != rom.size()) { std::fclose(fp); return 2; }
    std::fclose(fp);

    NesSystem sys;
    if (!sys.load_rom(rom.data(), rom.size())) return 2;

    for (int i = 0; i < 120; ++i) sys.run_frame();

    std::vector<uint8_t> snap(sys.state_size());
    if (!sys.state_save(snap.data(), snap.size())) {
        std::fprintf(stderr, "save failed\n");
        return 1;
    }

    // Branch A.
    sys.set_buttons(0, 0x08);  // hold START to exercise input state too
    for (int i = 0; i < 30; ++i) sys.run_frame();
    uint8_t fb_a[256 * 240];
    std::memcpy(fb_a, sys.framebuffer(), sizeof fb_a);
    const uint16_t pc_a = sys.cpu().pc;
    const uint64_t cyc_a = sys.cpu().cyc;

    // Restore, branch B (same inputs).
    if (!sys.state_load(snap.data(), snap.size())) {
        std::fprintf(stderr, "load failed\n");
        return 1;
    }
    sys.set_buttons(0, 0x08);
    for (int i = 0; i < 30; ++i) sys.run_frame();

    if (std::memcmp(fb_a, sys.framebuffer(), sizeof fb_a) != 0) {
        std::fprintf(stderr, "framebuffer diverged after state load\n");
        return 1;
    }
    if (sys.cpu().pc != pc_a || sys.cpu().cyc != cyc_a) {
        std::fprintf(stderr, "cpu diverged: pc %04X vs %04X, cyc %llu vs %llu\n",
                     sys.cpu().pc, pc_a,
                     static_cast<unsigned long long>(sys.cpu().cyc),
                     static_cast<unsigned long long>(cyc_a));
        return 1;
    }
    std::printf("state round-trip: %zu-byte snapshot, replay bit-identical — PASS\n",
                snap.size());
    return 0;
}
