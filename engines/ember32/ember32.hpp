// ember32/ember32.hpp — top-level Ember 32 system (REFERENCE MODEL skeleton).
//
// Holds the reference compositor and gives the CPU / RAM / MMIO / audio / quad
// unit a home and the C-ABI shape (a framebuffer accessor) that matches
// engines/nes/system.hpp and engines/snes. Those pieces are the rest of phase 2
// (see README.md) — this file is the seam they plug into so the compositor,
// which is done and verified, doesn't have to move when they land.
#pragma once
#include "compositor.hpp"

namespace ember32 {

struct System {
    Compositor gpu;                 // DONE + bring-up-verified (tools/bringup.cpp)

    // --- remaining phase-2 pieces (stubs for now; see README.md §"What's next") ---
    // TODO: ARM7TDMI-class CPU core + 4 MB main RAM + MMIO that maps the layer /
    //       sprite / palette / VRAM registers onto `gpu`'s fields.
    // TODO: per-scanline register tables (HDMA-class) — render_frame() will apply
    //       them line-by-line instead of once per frame.
    // TODO: 32-voice PCM/ADPCM audio + streamed channels.
    // TODO: the optional textured-quad unit (reuses the sprite texture path).

    // 320x240, 24-bit — the app's RF/NTSC encoder consumes this exactly as it
    // does the Ember 8/16 framebuffers.
    const RGB* framebuffer() const { return gpu.fb; }
    static int width()  { return Compositor::W; }
    static int height() { return Compositor::H; }

    // Later: run the CPU for a frame, apply per-scanline tables, then composite.
    // Today: composite the current scene state directly.
    void render_frame() { gpu.render(); }
};

} // namespace ember32

// The C-ABI facade `famemu_ember32_core()` (cf. engines/nes/famemu_nes_core.cpp)
// is added when Ember 32 is wired into the app — after the CPU lands and a real
// cartridge can drive it (phase 3+).
