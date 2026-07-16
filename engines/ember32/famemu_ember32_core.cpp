// famemu Ember 32 engine — FamemuCoreAPI facade (see engines/include/famemu_core.h).
//
// The seam that lets an Ember 32 cartridge run in the app exactly like Ember 8/16:
// one narrow C ABI, so the app + analog-TV chain + store never know which system
// is running. Wires the reference CPU + bus + compositor + audio behind it.
//
// Frame model: a cart's per-frame code ends by writing the RENDER register (its
// vblank); run_frame steps the CPU until that render lands. A single-shot bring-up
// cart renders once; a looping cart renders once per frame.
//
// MIT — clean-room, original design; no copyleft emulator source.
#include "../include/famemu_core.h"
#include "cpu_arm7.hpp"   // pulls in bus.hpp + compositor.hpp
#include "audio.hpp"
#include <cstring>
#include <vector>

using namespace ember32;

namespace {

struct Machine {
    Bus bus;
    CPU cpu;
    Audio audio;
    std::vector<int16_t> abuf;   // audio not yet MMIO-wired → silent for now
    Machine() { cpu.bus = &bus; }
};
Machine* g = nullptr;

// map the shared button bits to the pad word a cart reads at MMIO+0x18
int load_rom(const uint8_t* data, size_t len) {
    if (!g) g = new Machine();
    std::memset(g->bus.ram, 0, Bus::RAM_SIZE);
    std::memset(g->bus.reg, 0, sizeof(g->bus.reg));
    for (size_t i = 0; i < len && i < Bus::RAM_SIZE; i++) g->bus.ram[i] = data[i];
    g->cpu.reset(0);                 // cart code runs from address 0
    return 1;
}
void unload_rom() { delete g; g = nullptr; }
void reset() { if (g) { g->cpu.reset(0); g->bus.frame = 0; } }

void run_frame() {
    if (!g) return;
    g->bus.rendered = false;
    // step until the cart signals a frame (RENDER), it halts, or a safety cap.
    for (int i = 0; i < 4000000 && !g->bus.rendered && !g->cpu.halted; i++) g->cpu.step();
}

const uint8_t* video_rgb(int* w, int* h) {
    if (w) *w = Compositor::W;
    if (h) *h = Compositor::H;
    // Compositor::fb is RGB{u8,u8,u8} — tightly packed RGB888, W*H*3 bytes.
    return g ? reinterpret_cast<const uint8_t*>(g->bus.gpu.fb) : nullptr;
}

size_t audio_read(int16_t* out, size_t max_frames) {
    if (!g || !out) return 0;
    g->audio.mix(out, int(max_frames));   // silent until voices are MMIO-driven
    return max_frames;
}
int sample_rate() { return Audio::SR; }

void set_input(int port, uint32_t buttons) {
    if (g && port == 0) g->bus.input = buttons;
}

// State = CPU regs + CPSR + the MMIO register file + main RAM.
size_t state_size() { return sizeof(CPU::r) + sizeof(uint32_t) + sizeof(Bus::reg) + Bus::RAM_SIZE; }
int state_save(uint8_t* buf, size_t len) {
    if (!g || len < state_size()) return 0;
    uint8_t* p = buf;
    std::memcpy(p, g->cpu.r, sizeof(g->cpu.r)); p += sizeof(g->cpu.r);
    std::memcpy(p, &g->cpu.cpsr, sizeof(uint32_t)); p += sizeof(uint32_t);
    std::memcpy(p, g->bus.reg, sizeof(g->bus.reg)); p += sizeof(g->bus.reg);
    std::memcpy(p, g->bus.ram, Bus::RAM_SIZE);
    return 1;
}
int state_load(const uint8_t* buf, size_t len) {
    if (!g || len < state_size()) return 0;
    const uint8_t* p = buf;
    std::memcpy(g->cpu.r, p, sizeof(g->cpu.r)); p += sizeof(g->cpu.r);
    std::memcpy(&g->cpu.cpsr, p, sizeof(uint32_t)); p += sizeof(uint32_t);
    std::memcpy(g->bus.reg, p, sizeof(g->bus.reg)); p += sizeof(g->bus.reg);
    std::memcpy(g->bus.ram, p, Bus::RAM_SIZE);
    return 1;
}

const FamemuCoreAPI kEmber32Api = {
    "ember32",
    load_rom, unload_rom, reset,
    run_frame,
    video_rgb,
    audio_read, sample_rate,
    set_input,
    state_size, state_save, state_load,
};

} // namespace

extern "C" const FamemuCoreAPI* famemu_ember32_core(void) { return &kEmber32Api; }
