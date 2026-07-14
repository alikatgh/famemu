// famemu SNES engine — headless frame dumper (KORA boot gate).
//   snes_dump <rom.sfc> <out_prefix> [script]
// Script grammar matches famemu_dump/dump_ppm: HEXMASK:FRAMES;... with
// libretro bit positions (B=01 Y=02 SELECT=04 START=08 UP=10 DOWN=20 LEFT=40
// RIGHT=80 A=100 X=200 L=400 R=800). Dumps the last frame of each segment.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../snes_system.hpp"

using namespace famemu::snes;

namespace {
uint32_t core_buttons(unsigned m) {  // libretro positions -> FamemuCoreButton
    uint32_t b = 0;
    if (m & 0x100) b |= 1u << 0;   // A
    if (m & 0x001) b |= 1u << 1;   // B
    if (m & 0x004) b |= 1u << 2;   // Select
    if (m & 0x008) b |= 1u << 3;   // Start
    if (m & 0x010) b |= 1u << 4;   // Up
    if (m & 0x020) b |= 1u << 5;   // Down
    if (m & 0x040) b |= 1u << 6;   // Left
    if (m & 0x080) b |= 1u << 7;   // Right
    if (m & 0x200) b |= 1u << 8;   // X
    if (m & 0x002) b |= 1u << 9;   // Y
    if (m & 0x400) b |= 1u << 10;  // L
    if (m & 0x800) b |= 1u << 11;  // R
    return b;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <rom.sfc> <out_prefix> [script]\n", argv[0]);
        return 2;
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

    SnesSystem sys;
    if (!sys.load_rom(rom.data(), rom.size())) {
        std::fprintf(stderr, "bad ROM\n");
        return 2;
    }

    const std::string prefix = argv[2];
    std::string script = argc > 3 && argv[3][0] != '-' ? argv[3] : "0:300";
    const char* wav_path = nullptr;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
    std::vector<int16_t> pcm;
    int16_t chunk[4096];
    int frame = 0, dumped = 0;
    char path[512];
    for (size_t pos = 0; pos < script.size();) {
        size_t end = script.find(';', pos);
        if (end == std::string::npos) end = script.size();
        unsigned mask = 0;
        int frames = 0;
        std::sscanf(script.substr(pos, end - pos).c_str(), "%x:%d", &mask, &frames);
        sys.set_buttons(core_buttons(mask));
        for (int i = 0; i < frames; ++i) {
            sys.run_frame();
            ++frame;
            if (wav_path) {
                size_t got;
                while ((got = sys.read_audio(chunk, 2048)) > 0)
                    pcm.insert(pcm.end(), chunk, chunk + got * 2);
            }
            if (getenv("SNES_DEBUG_SLOTS")) {
                std::fprintf(stderr, "f=%d slots:", frame);
                for (int zi = 0; zi < 12; ++zi)
                    std::fprintf(stderr, " %04X",
                                 sys.wram_byte(2 + zi * 2) |
                                 (sys.wram_byte(3 + zi * 2) << 8));
                std::fprintf(stderr, "\n");
            }
            if (getenv("SNES_DEBUG_ZP"))
                { uint8_t cm[8]; sys.ppu().dbg_colormath(cm);
                std::fprintf(stderr, "f=%d mode=%d gstate=%d pos=%d,%d pc=%02X:%04X song=%d cgadsub=%02X coldata=%d,%d,%d ts=%02X cgwsel=%02X bg2sc=%02X bg12nba=%02X\n",
                             frame, sys.wram_byte(58), sys.wram_byte(57),
                             sys.wram_byte(4) | (sys.wram_byte(5) << 8),
                             sys.wram_byte(6) | (sys.wram_byte(7) << 8),
                             sys.cpu().pbr, sys.cpu().pc, sys.spc_song(),
                             cm[0], cm[1], cm[2], cm[3], cm[4], cm[5], cm[6], cm[7]); }
        }
        if (frames > 0) {
            std::snprintf(path, sizeof path, "%s_%04d.ppm", prefix.c_str(), frame);
            if (std::FILE* out = std::fopen(path, "wb")) {
                const int fw = sys.ppu().width();
                std::fprintf(out, "P6\n%d %d\n255\n", fw, SPpu::kHeight);
                std::fwrite(sys.framebuffer(), 1,
                            static_cast<size_t>(fw) * SPpu::kHeight * 3, out);
                std::fclose(out);
                ++dumped;
            }
        }
        pos = end + 1;
    }
    if (wav_path && !pcm.empty()) {
        std::FILE* wf = std::fopen(wav_path, "wb");
        if (wf) {
            const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
            const uint32_t rate = SDsp::kSampleRate;
            uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
                             16,0,0,0, 1,0, 2,0, 0,0,0,0, 0,0,0,0, 4,0, 16,0,
                             'd','a','t','a',0,0,0,0};
            auto put32 = [&](int off, uint32_t v) {
                h[off] = v & 0xFF; h[off+1] = (v >> 8) & 0xFF;
                h[off+2] = (v >> 16) & 0xFF; h[off+3] = (v >> 24) & 0xFF;
            };
            put32(4, 36 + data_bytes);
            put32(24, rate);
            put32(28, rate * 4);
            put32(40, data_bytes);
            std::fwrite(h, 1, 44, wf);
            std::fwrite(pcm.data(), 2, pcm.size(), wf);
            std::fclose(wf);
            std::printf("wav: %zu stereo frames -> %s\n", pcm.size() / 2, wav_path);
        }
    }
    std::printf("snes_dump: %d frames run, %d dumped (cpu pc=%02X:%04X)\n",
                frame, dumped, sys.cpu().pbr, sys.cpu().pc);
    return dumped > 0 ? 0 : 1;
}
