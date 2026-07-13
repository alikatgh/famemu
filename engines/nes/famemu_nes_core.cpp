// famemu NES engine — FamemuCoreAPI facade (see engines/include/famemu_core.h).
#include <cstring>
#include <memory>

#include "famemu_core.h"
#include "palette.hpp"
#include "system.hpp"

namespace {

using famemu::nes::NesSystem;
using famemu::nes::Ppu;

std::unique_ptr<NesSystem> g_sys;
uint8_t g_rgb[Ppu::kWidth * Ppu::kHeight * 3];

int nes_load_rom(const uint8_t* data, size_t len) {
    auto sys = std::make_unique<NesSystem>();
    if (!sys->load_rom(data, len)) return 0;
    g_sys = std::move(sys);
    return 1;
}

void nes_unload_rom(void) { g_sys.reset(); }
void nes_reset(void) { if (g_sys) g_sys->power_on(); }
void nes_run_frame(void) { if (g_sys) g_sys->run_frame(); }

const uint8_t* nes_video_rgb(int* w, int* h) {
    if (w) *w = Ppu::kWidth;
    if (h) *h = Ppu::kHeight;
    if (!g_sys) return nullptr;
    const uint8_t* idx = g_sys->framebuffer();
    for (int i = 0; i < Ppu::kWidth * Ppu::kHeight; ++i) {
        const uint32_t c = famemu::nes::kPaletteRgb[idx[i] & 0x3F];
        g_rgb[i * 3 + 0] = (c >> 16) & 0xFF;
        g_rgb[i * 3 + 1] = (c >> 8) & 0xFF;
        g_rgb[i * 3 + 2] = c & 0xFF;
    }
    return g_rgb;
}

size_t nes_audio_read(int16_t* out, size_t max_frames) {
    return g_sys ? g_sys->apu().read_samples(out, max_frames) : 0;
}
int nes_sample_rate(void) { return famemu::nes::Apu::kSampleRate; }

void nes_set_input(int port, uint32_t buttons) {
    // FamemuButton bit order matches the NES controller shift order (A first).
    if (g_sys) g_sys->set_buttons(port, static_cast<uint8_t>(buttons & 0xFF));
}

size_t nes_state_size(void) { return g_sys ? g_sys->state_size() : 0; }
int nes_state_save(uint8_t* buf, size_t len) {
    return (g_sys && g_sys->state_save(buf, len)) ? 1 : 0;
}
int nes_state_load(const uint8_t* buf, size_t len) {
    return (g_sys && g_sys->state_load(buf, len)) ? 1 : 0;
}

const FamemuCoreAPI kNesApi = {
    "nes",
    nes_load_rom, nes_unload_rom, nes_reset, nes_run_frame,
    nes_video_rgb, nes_audio_read, nes_sample_rate, nes_set_input,
    nes_state_size, nes_state_save, nes_state_load,
};

}  // namespace

extern "C" const FamemuCoreAPI* famemu_nes_core(void) { return &kNesApi; }
