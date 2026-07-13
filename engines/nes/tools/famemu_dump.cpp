// famemu NES engine — headless frame dumper for verification (gate 5).
//
//   famemu_dump <rom.nes> <out_prefix> [script] [--every N] [--wav out.wav]
//
// `script` uses the SAME grammar as game/dump_ppm ("HEXMASK:FRAMES;..." with
// libretro joypad bit positions: B=01 SELECT=04 START=08 UP=10 DOWN=20
// LEFT=40 RIGHT=80 A=100), so one input script drives both this core and the
// Nestopia reference for pixel comparison. Writes out_prefix_NNNN.ppm for the
// LAST frame of each script segment (or every Nth frame with --every).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../palette.hpp"
#include "../system.hpp"

using namespace famemu::nes;

namespace {

// libretro mask -> NES controller byte (A,B,Sel,Start,U,D,L,R = bits 0..7).
uint8_t libretro_to_pad(unsigned m) {
    uint8_t p = 0;
    if (m & 0x100) p |= 0x01;  // A
    if (m & 0x001) p |= 0x02;  // B
    if (m & 0x004) p |= 0x04;  // SELECT
    if (m & 0x008) p |= 0x08;  // START
    if (m & 0x010) p |= 0x10;  // UP
    if (m & 0x020) p |= 0x20;  // DOWN
    if (m & 0x040) p |= 0x40;  // LEFT
    if (m & 0x080) p |= 0x80;  // RIGHT
    return p;
}

void write_ppm(const uint8_t* idx, const char* path) {
    std::FILE* fp = std::fopen(path, "wb");
    if (!fp) { std::fprintf(stderr, "cannot open %s\n", path); return; }
    std::fprintf(fp, "P6\n%d %d\n255\n", Ppu::kWidth, Ppu::kHeight);
    for (int i = 0; i < Ppu::kWidth * Ppu::kHeight; ++i) {
        uint32_t c = kPaletteRgb[idx[i] & 0x3F];
        uint8_t rgb[3] = {static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8),
                          static_cast<uint8_t>(c)};
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <rom.nes> <out_prefix> [script] [--every N]\n"
                     "  script: HEXMASK:FRAMES;...  e.g. \"0:60;08:2;0:120\"\n",
                     argv[0]);
        return 2;
    }
    const char* rom_path = argv[1];
    const std::string prefix = argv[2];
    std::string script = (argc > 3 && argv[3][0] != '-') ? argv[3] : "0:240";
    int every = 0;
    const char* wav_path = nullptr;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--every") && i + 1 < argc) every = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
    }

    // Load ROM.
    std::FILE* fp = std::fopen(rom_path, "rb");
    if (!fp) { std::fprintf(stderr, "cannot open ROM %s\n", rom_path); return 1; }
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(fp); return 1; }
    std::vector<uint8_t> rom(static_cast<size_t>(sz));
    if (std::fread(rom.data(), 1, rom.size(), fp) != rom.size()) { std::fclose(fp); return 1; }
    std::fclose(fp);

    NesSystem sys;
    if (!sys.load_rom(rom.data(), rom.size())) {
        std::fprintf(stderr, "unsupported ROM (mapper?)\n");
        return 1;
    }

    int frame = 0, dumped = 0;
    char path[512];
    std::vector<int16_t> pcm;  // stereo frames when --wav
    int16_t chunk[4096];
    for (size_t pos = 0; pos < script.size();) {
        size_t seg_end = script.find(';', pos);
        if (seg_end == std::string::npos) seg_end = script.size();
        unsigned mask = 0;
        int frames = 0;
        std::sscanf(script.substr(pos, seg_end - pos).c_str(), "%x:%d", &mask, &frames);
        sys.set_buttons(0, libretro_to_pad(mask));
        for (int i = 0; i < frames; ++i) {
            sys.run_frame();
            ++frame;
            if (wav_path) {
                size_t got;
                while ((got = sys.apu().read_samples(chunk, 2048)) > 0)
                    pcm.insert(pcm.end(), chunk, chunk + got * 2);
            }
            if (every > 0 && frame % every == 0) {
                std::snprintf(path, sizeof path, "%s_%04d.ppm", prefix.c_str(), frame);
                write_ppm(sys.framebuffer(), path);
                ++dumped;
            }
        }
        if (every == 0 && frames > 0) {  // last frame of each segment
            std::snprintf(path, sizeof path, "%s_%04d.ppm", prefix.c_str(), frame);
            write_ppm(sys.framebuffer(), path);
            ++dumped;
        }
        pos = seg_end + 1;
    }
    if (wav_path && !pcm.empty()) {  // minimal 16-bit stereo WAV
        std::FILE* wf = std::fopen(wav_path, "wb");
        if (wf) {
            const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
            const uint32_t rate = Apu::kSampleRate;
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
    std::printf("famemu_dump: %d frames run, %d dumped\n", frame, dumped);
    return dumped > 0 ? 0 : 1;
}
