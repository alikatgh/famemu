// Run a blargg test ROM on the famemu NES core (gates 2-4).
//
//   blargg_harness <rom.nes> [max_frames] [--screen out.ppm]
//
// Two reporting styles, both supported:
//  - $6000 protocol (instr_test-v5, ppu_vbl_nmi, apu_test): $6000 = 0x80 while
//    running, final status when done (0 = pass), text at $6004. We exit with
//    the ROM's status code.
//  - screen-only (the 2005 sprite-hit suite): run max_frames, write the
//    framebuffer as PPM for inspection, exit 0 (caller judges the image).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../palette.hpp"
#include "../system.hpp"

using namespace famemu::nes;

static void write_ppm(const uint8_t* idx, const char* path) {
    std::FILE* fp = std::fopen(path, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", Ppu::kWidth, Ppu::kHeight);
    for (int i = 0; i < Ppu::kWidth * Ppu::kHeight; ++i) {
        uint32_t c = kPaletteRgb[idx[i] & 0x3F];
        uint8_t rgb[3] = {static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8),
                          static_cast<uint8_t>(c)};
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <rom.nes> [max_frames] [--screen out.ppm]\n", argv[0]);
        return 2;
    }
    int max_frames = 1200;
    const char* screen_out = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--screen") && i + 1 < argc) screen_out = argv[++i];
        else max_frames = std::atoi(argv[i]);
    }

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

    NesSystem sys;
    if (!sys.load_rom(rom.data(), rom.size())) {
        std::fprintf(stderr, "unsupported ROM (mapper)\n");
        return 2;
    }

    bool armed = false;  // saw $6000 == 0x80 (test officially running)
    for (int f = 0; f < max_frames; ++f) {
        sys.run_frame();
        uint8_t st = sys.read(0x6000);
        if (!armed && st == 0x80) armed = true;
        if (armed && st != 0x80) {
            if (st == 0x81) {  // needs reset: press it and continue
                // (blargg convention: wait >=100ms then reset)
                for (int k = 0; k < 8; ++k) sys.run_frame();
                sys.cpu().reset();
                armed = false;
                continue;
            }
            std::string text;
            for (uint16_t a = 0x6004; a < 0x6200; ++a) {
                uint8_t c = sys.read(a);
                if (!c) break;
                text += static_cast<char>(c);
            }
            std::printf("status=%02X\n%s\n", st, text.c_str());
            if (screen_out) write_ppm(sys.framebuffer(), screen_out);
            return st == 0 ? 0 : 1;
        }
    }
    if (screen_out) {
        write_ppm(sys.framebuffer(), screen_out);
        std::printf("ran %d frames, screen written to %s\n", max_frames, screen_out);
        return 0;
    }
    std::fprintf(stderr, "timeout: no $6000 result after %d frames\n", max_frames);
    return 3;
}
