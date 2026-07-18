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

// Apply the cart's voice registers (bus.hpp map: 0x200 + V*0x20) to the audio
// unit. Runs once per frame after the render; KEY_ON/KEY_OFF are edge commands
// the host consumes (write-then-clear), everything else is level state.
static void apply_audio_regs() {
    uint32_t on  = g->bus.reg[0x1F0 / 4];
    uint32_t off = g->bus.reg[0x1F4 / 4];
    for (int v = 0; v < 8; v++) {
        uint32_t b = (0x200 + v * 0x20) / 4;
        Voice& vc = g->audio.voices[v];
        uint32_t src = g->bus.reg[b + 0];
        uint32_t len = g->bus.reg[b + 1];
        if (on & (1u << v)) {
            if (src + len * 2 <= Bus::RAM_SIZE && len > 0) {
                vc.sample = reinterpret_cast<const int16_t*>(&g->bus.ram[src]);
                vc.len = int(len);
                uint32_t rate = g->bus.reg[b + 2];
                vc.rate = (rate ? rate : 0x10000u) / 65536.0;
                vc.vol = g->bus.reg[b + 3] / 256.0f;
                vc.pan = g->bus.reg[b + 4] / 256.0f;
                uint32_t lp = g->bus.reg[b + 5];
                vc.loop = lp != 0;
                vc.loop_start = lp ? int(lp - 1) : 0;
                vc.attack = 16; vc.decay = 1; vc.sustain = 1.0f; vc.release = 512;
                vc.keyOn();
            }
        } else if (vc.active) {
            // live tweaks (vibrato/fades) without retrigger
            uint32_t rate = g->bus.reg[b + 2];
            if (rate) vc.rate = rate / 65536.0;
            vc.vol = g->bus.reg[b + 3] / 256.0f;
        }
        if (off & (1u << v)) vc.keyOff();
    }
    g->bus.reg[0x1F0 / 4] = 0;
    g->bus.reg[0x1F4 / 4] = 0;
}

void run_frame() {
    if (!g) return;
    g->bus.rendered = false;
    // step until the cart signals a frame (RENDER), it halts, or a safety cap.
    for (int i = 0; i < 4000000 && !g->bus.rendered && !g->cpu.halted; i++) g->cpu.step();
    apply_audio_regs();
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
    if (!g) return;
    if (port == 0) g->bus.input = buttons;
    else if (port == 1) g->bus.input2 = buttons;   // 2-player carts (MMIO 0x1C)
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
