// Software loopback demo: synthesize an animated "game-like" 256x240 RGB
// scene, encode it as an NTSC-J RF signal with NtscModulator, then decode it
// with the real NtscDecoder chain -- the same DSP the live HackRF pipeline
// runs. Proves the "played through a real analog TV" look on arbitrary content
// with no HackRF and no external dependencies. Writes decoded frames as BMP.
//
// Build (from repo root, nothing to install):
//   clang++ -std=c++20 -O2 -I src tools/famitv_loopback.cpp \
//       src/dsp/ntsc_decoder.cpp -o build_tools/famitv_loopback
//   ./build_tools/famitv_loopback loopback_out
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "config.hpp"
#include "dsp/am_detector.hpp"
#include "dsp/dc_blocker.hpp"
#include "dsp/fir.hpp"
#include "dsp/frame.hpp"
#include "dsp/nco.hpp"
#include "dsp/ntsc_decoder.hpp"
#include "dsp/ntsc_modulator.hpp"
#include "util/bmp.hpp"

using namespace famidec;

namespace {

constexpr int kW = 256, kH = 240;
struct RGB { uint8_t r, g, b; };

void put(std::vector<uint8_t>& fb, int x, int y, RGB c) {
    if (x < 0 || x >= kW || y < 0 || y >= kH) return;
    uint8_t* p = fb.data() + (static_cast<size_t>(y) * kW + x) * 3;
    p[0] = c.r; p[1] = c.g; p[2] = c.b;
}
void rect(std::vector<uint8_t>& fb, int x0, int y0, int w, int h, RGB c) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) put(fb, x, y, c);
}

// One animated frame; `t` advances the scene so successive frames differ.
void draw_scene(std::vector<uint8_t>& fb, int t) {
    // sky gradient
    for (int y = 0; y < kH; ++y) {
        uint8_t b = static_cast<uint8_t>(150 + y * 45 / kH);
        for (int x = 0; x < kW; ++x) put(fb, x, y, {92, 148, b});
    }
    // top: 7 SMPTE-ish color bars -- color fidelity reference
    const RGB bars[7] = {{191,191,191},{191,191,0},{0,191,191},{0,191,0},
                         {191,0,191},{191,0,0},{0,0,191}};
    for (int i = 0; i < 7; ++i) rect(fb, i * kW / 7, 0, kW / 7 + 1, 32, bars[i]);
    // fine 2px vertical stripes -- dot-crawl / rainbow (cross-color) demonstrator
    for (int x = 0; x < kW; ++x)
        rect(fb, x, 34, 1, 18, (x & 1) ? RGB{235,235,235} : RGB{16,16,16});
    // clouds
    rect(fb, 30 + (t % kW), 70, 34, 12, {245,245,245});
    rect(fb, 150 - ((t / 2) % 80), 60, 40, 14, {245,245,245});
    // three "?" blocks with black outline (saturated edges -> chroma bleed)
    for (int i = 0; i < 3; ++i) {
        int bx = 60 + i * 52, by = 140;
        rect(fb, bx, by, 22, 22, {224,148,32});
        rect(fb, bx, by, 22, 2, {0,0,0}); rect(fb, bx, by + 20, 22, 2, {0,0,0});
        rect(fb, bx, by, 2, 22, {0,0,0}); rect(fb, bx + 20, by, 2, 22, {0,0,0});
        rect(fb, bx + 9, by + 6, 4, 7, {0,0,0}); put(fb, bx + 10, by + 16, {0,0,0});
    }
    // moving red character
    int hx = 20 + (t * 3) % (kW - 40);
    rect(fb, hx, 178, 16, 22, {200,32,24});
    rect(fb, hx + 3, 172, 10, 7, {224,160,120});  // head
    // ground with brick pattern (fine detail -> Y/C crosstalk)
    rect(fb, 0, 208, kW, 32, {150,90,44});
    for (int y = 208; y < 240; y += 8) rect(fb, 0, y, kW, 1, {66,36,14});
    for (int x = 0; x < kW; x += 16) {
        int off = ((x / 16) & 1) ? 8 : 0;
        for (int y = 208; y < 240; y += 8) put(fb, x + off, y, {66,36,14});
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outdir = (argc > 1) ? argv[1] : "loopback_out";

    Config cfg;
    cfg.sample_rate = 10e6;
    cfg.offset_hz = 2.0e6;
    cfg.mode = Config::Mode::Color;

    TripleBuffer tb;
    NtscDecoder dec(cfg, tb);
    NtscModulator mod(cfg.sample_rate, cfg.offset_hz);
    DcBlocker dcb;
    Nco mixer(cfg.offset_hz, cfg.sample_rate);
    FirFilterC lpf(design_lowpass(4.3e6, cfg.sample_rate, 63));
    EnvelopeDetector env;

    std::error_code ec;
    std::filesystem::create_directories(outdir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create outdir %s: %s\n", outdir.c_str(),
                     ec.message().c_str());
    }

    std::vector<uint8_t> fb(static_cast<size_t>(kW) * kH * 3);
    std::vector<uint8_t> iq;
    std::vector<std::complex<float>> cbuf;
    std::vector<float> comp;

    const int fields = 48;
    int saved = 0;
    uint64_t last_seq = 0;
    for (int fld = 0; fld < fields; ++fld) {
        draw_scene(fb, fld);
        iq.clear();
        mod.modulate_field(fb.data(), kW, kH, iq);
        const size_t ns = iq.size() / 2;
        cbuf.resize(ns);
        comp.resize(ns);
        for (size_t i = 0; i < ns; ++i) {
            std::complex<float> c(static_cast<int8_t>(iq[2*i]) / 128.0f,
                                  static_cast<int8_t>(iq[2*i+1]) / 128.0f);
            cbuf[i] = dcb.process(c) * mixer.next();
        }
        lpf.process(cbuf.data(), cbuf.data(), ns);
        env.process(cbuf.data(), comp.data(), ns);
        dec.process(comp.data(), ns);

        const Frame* f = tb.acquire();
        if (f && f->seq != last_seq) {
            last_seq = f->seq;
            if (fld >= fields - 6 && saved < 3) {  // a few locked frames
                char p[512];
                std::snprintf(p, sizeof(p), "%s/frame_%02d.bmp", outdir.c_str(),
                              saved);
                if (write_bmp(*f, p)) {
                    ++saved;
                } else {
                    std::fprintf(stderr, "failed to write %s\n", p);
                }
            }
        }
    }
    std::printf("decoded frames=%llu lines=%llu coasted=%llu saved=%d\n",
                static_cast<unsigned long long>(dec.stats().frames.load()),
                static_cast<unsigned long long>(dec.stats().lines.load()),
                static_cast<unsigned long long>(dec.stats().lines_coasted.load()),
                saved);
    return saved > 0 ? 0 : 1;
}
