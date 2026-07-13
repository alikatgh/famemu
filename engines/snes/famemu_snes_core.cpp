// famemu SNES engine — FamemuCoreAPI facade (see engines/include/famemu_core.h).
#include <memory>

#include "famemu_core.h"
#include "snes_system.hpp"

namespace {

using famemu::snes::SnesSystem;
using famemu::snes::SPpu;

std::unique_ptr<SnesSystem> g_sys;

int snes_load_rom(const uint8_t* data, size_t len) {
    auto sys = std::make_unique<SnesSystem>();
    if (!sys->load_rom(data, len)) return 0;
    g_sys = std::move(sys);
    return 1;
}

void snes_unload_rom(void) { g_sys.reset(); }
void snes_reset(void) { if (g_sys) g_sys->power_on(); }
void snes_run_frame(void) { if (g_sys) g_sys->run_frame(); }

const uint8_t* snes_video_rgb(int* w, int* h) {
    if (w) *w = SPpu::kWidth;
    if (h) *h = SPpu::kHeight;
    return g_sys ? g_sys->framebuffer() : nullptr;  // already RGB888
}

size_t snes_audio_read(int16_t* out, size_t max_frames) {
    return g_sys ? g_sys->read_audio(out, max_frames) : 0;
}
int snes_sample_rate(void) { return famemu::snes::SDsp::kSampleRate; }

void snes_set_input(int port, uint32_t buttons) {
    if (g_sys && port == 0) g_sys->set_buttons(buttons);
}

size_t snes_state_size(void) { return 0; }   // after the fidelity pass
int snes_state_save(uint8_t*, size_t) { return 0; }
int snes_state_load(const uint8_t*, size_t) { return 0; }

const FamemuCoreAPI kSnesApi = {
    "snes",
    snes_load_rom, snes_unload_rom, snes_reset, snes_run_frame,
    snes_video_rgb, snes_audio_read, snes_sample_rate, snes_set_input,
    snes_state_size, snes_state_save, snes_state_load,
};

}  // namespace

extern "C" const FamemuCoreAPI* famemu_snes_core(void) { return &kSnesApi; }
