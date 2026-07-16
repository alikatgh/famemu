# Ember 32 — reference model (phase 2, in progress)

The portable C++ **reference model** for the Ember 32 console — the authority the
fast core is later verified pixel-for-pixel against. Spec:
[../../docs/engines/EMBER32.md](../../docs/engines/EMBER32.md). Plan / phases:
[EMBER32_SCOPE.md §6](../../docs/engines/EMBER32_SCOPE.md).

Correctness over speed is the rule here — this file set *is* the spec, made
executable.

## Status

| Piece | State |
|-------|-------|
| **2D compositor** (`compositor.hpp`) | **done + bring-up-verified** — 6 affine layers, 1024 affine sprites, 24-bit direct colour, normal/alpha/additive/subtractive blend, per-pixel priority, colour-key transparency |
| System skeleton (`ember32.hpp`) | seam in place; holds the compositor + framebuffer accessor |
| Bring-up smoke test (`tools/bringup.cpp`) | renders a demo scene (2 affine layers + 6 blended affine sprites, priority-interleaved) to a PPM |
| **ARM7 CPU (ARM state)** (`cpu_arm7.hpp`) | **done + bring-up-verified** — data-proc (all opcodes, imm/reg/shifted operands, flags), MUL/MLA, LDR/STR, LDM/STM, B/BL, MRS/MSR |
| **Memory bus + MMIO map** (`bus.hpp`) | **done** — 4 MB RAM + a register block (palette/tileset/6 layers/1024 sprites, by console address); a cart lays data in RAM and configures the scene, RENDER composites |
| **Optional textured-quad unit** (`compositor.hpp` `Quad`) | **done** — affine-textured quads, priority-composited with layers/sprites (perspective floor + rotated banner, `tools/quad_bringup.cpp`) |
| **Audio: 32 voices + ADSR + pan** (`audio.hpp`) | **done** — pitched PCM voices, ADSR envelopes, stereo pan → 48 kHz mix; verified as a chord WAV (`tools/audio_bringup.cpp`). ADPCM / reverb / streamed channels = follow-ons |
| **Phase 3 — bring-up cartridge** (`tools/cart_bringup.cpp`) | **done** — an ARM program configures a scrolling scaled layer + a scaled/rotated sprite via MMIO, renders, end to end |
| **Phase 3 — out the RF path** (`rf_composite.hpp`) | **done** — the frame goes through an NTSC composite encode/decode (the platform signature); the cart writes clean + composite frames |
| **Per-scanline raster tables (HDMA-class)** (`compositor.hpp` `line_sx/sy`) | **done** — per-output-line layer offsets; a water-reflection ripple demo (`tools/raster_bringup.cpp`) |
| **Thumb (16-bit) instruction set** (`cpu_arm7.hpp`) | **done + verified** — the common formats + ARM↔Thumb interworking (BX toggles the T bit); `tools/thumb_test.cpp` runs a Thumb program and checks the result |
| **Feature/verification gate** (`tools/test_features.cpp`) | **done** — 9 cases (affine, alpha/additive blend, priority, quad, line-scroll, ARM + Thumb CPU) hashed against frozen goldens; `--bless` reblesses. CI-ready; the fast core verifies against this |
| **C ABI facade** (`famemu_ember32_core.cpp`) | **done + verified** — implements `FamemuCoreAPI` (load_rom / run_frame / video_rgb / audio / set_input / save-state); a ROM loads, runs a frame, and returns video through the same interface as Ember 8/16 (`tools/facade_test.cpp`) |
| **`.e32` cartridge file + app registration** (`tools/make_cart.cpp`, `tools/cart_file_test.cpp`) | **done + verified** — `make_cart` emits a flat `.e32` ROM image (program at 0, cart data at 0x1000+); `cart_file_test` loads it from disk through the facade exactly as the app does (fread → `load_rom` → `run_frame` → `video_rgb`) and confirms real content (320×240, scaled layer + bright sprite). The famemu app registers `builtin:ember32` (raw 24-bit RGB, Library “Ember 32” category) |
| **Banked modes + exceptions + exact bus timing** (`cpu_arm7.hpp`, `bus.hpp`) | **done + verified** — the 7 processor modes with banked r13/r14 (+ r8-r12 for FIQ) and per-mode SPSR; the exception entry/return sequence for Reset/Undef/SWI/PAbort/DAbort/IRQ/FIQ (vectors 0x00-0x1C); SWI (ARM + Thumb); an IRQ/FIQ controller on the bus (IRQ_ENABLE/FLAGS/FIQ_ENABLE, VBLANK source) driving the CPU lines; and an S/N/I bus-cycle counter (`tcycles`). `tools/exception_test.cpp` proves banked SPs, SPSR, FIQ-banked r8 isolation, live IRQ+FIQ delivery, and hand-counted timing (12/12) |
| A separate optimised fast core (verified vs this reference) | **TODO** (phase 4) |

## Run the bring-up

```sh
cd engines/ember32
c++ -std=c++17 -O2 -I.. tools/bringup.cpp -o /tmp/e32 && /tmp/e32 out.ppm
```

It writes a 320×240 PPM. The frame exercises: two independently rotated/scaled
background layers (one **additive**), six sprites at varied scale / rotation /
alpha / blend, and priority interleaving (the big centre orb draws *behind* the
glow-grid while the small orbs draw *in front*).

## Build a real cartridge file and load it (the app path)

```sh
cd engines/ember32
# 1. emit a flat .e32 ROM image (ARM program at 0, cart data at 0x1000+)
c++ -std=c++17 -O2 tools/make_cart.cpp -o /tmp/e32mk && /tmp/e32mk /tmp/demo.e32
# 2. load it from disk through the FamemuCoreAPI facade, exactly as the app does
c++ -std=c++17 -O2 -I.. tools/cart_file_test.cpp famemu_ember32_core.cpp -o /tmp/e32cf && /tmp/e32cf /tmp/demo.e32
```

The same `.e32` runs in the famemu app as `builtin:ember32` (raw 24-bit RGB,
320×240; Library → **Ember 32**). `cart_file_test` asserts the frame is real
content (scaled layer + bright sprite), not a flat backdrop.

## Verify the privileged machine (modes, exceptions, timing)

```sh
c++ -std=c++17 -O2 -I.. tools/exception_test.cpp -o /tmp/e32exc && /tmp/e32exc
```

A firmware-style ROM (vector table + handlers) sets a distinct SP per mode and a
FIQ-banked r8, then takes a SWI, an IRQ (vblank) and a FIQ (vblank routed to
FIQ); the test checks banked SPs, SPSR, FIQ register isolation, live interrupt
delivery, and the hand-counted S/N/I cycle total (12/12).

## Architecture

- `compositor.hpp` — the register-free reference compositor. You fill a
  `Compositor` (palette, tileset, `layers[6]`, `sprites[1024]`, backdrop) and
  call `render()` → a 24-bit `fb[W*H]`. `make_affine(angle, scale, …)` builds the
  dest→source matrices. Deliberately register-free for now so the model can be
  exercised directly; the CPU will later drive these same fields via MMIO.
- `ember32.hpp` — `System`: holds the compositor and exposes `framebuffer()` /
  `render_frame()`, matching `engines/nes` / `engines/snes`. The CPU, RAM,
  scanline tables, audio and quad unit plug in here without moving the compositor.

## What's next (phase 2 → 3)

1. **MMIO layer** — map the compositor's fields to a register block so a program
   (not hand-written C) configures layers/sprites/palette/VRAM.
2. **CPU** — an ARM7TDMI-class core (ARM + Thumb), cycle/bus-timing model.
   Toolchain is off-the-shelf (`clang`/`gcc`), so a bring-up cart can be written
   in C immediately.
3. **Per-scanline tables** — `render_frame()` walks scanlines, applying the
   register tables per line (raster splits, line-scroll, per-line colour).
4. **Audio**, then the **optional quad unit** (reuses the sprite texture path).
5. **Bring-up cart** (phase 3): boots, scrolls, draws a scaled/rotated sprite,
   out the RF path — the first end-to-end screenshot.

## Verification

Because Ember 32 is an original design, the reference is **this model**, not a
third-party emulator. The eventual fast core is checked pixel-for-pixel against
`compositor.hpp` on a suite of feature carts (every blend term, affine/rotate
edge cases, priority order, fill-rate overload, quad texture mapping) wired into
CI — the same discipline that gates Ember 8 and Ember 16.
