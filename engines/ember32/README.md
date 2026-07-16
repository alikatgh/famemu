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
| **Memory bus + MMIO** (`bus.hpp`) | **done** — 4 MB RAM + an MMIO window onto the GPU; an ARM program configures a scene + triggers render (`tools/cpu_bringup.cpp`) |
| Thumb / banked FIQ-IRQ modes / exact bus timing | **TODO** (the CPU runs real ARM programs; these harden it) |
| Full MMIO map (every layer/sprite/palette/VRAM field) | **TODO** (small map today; grows without moving the CPU/GPU) |
| Per-scanline register tables (HDMA-class) | **TODO** |
| 32-voice + streamed audio | **TODO** |
| Optional textured-quad unit | **TODO** |
| Feature test carts + fast-core lockstep gates | **TODO** (phase 4) |

## Run the bring-up

```sh
cd engines/ember32
c++ -std=c++17 -O2 -I.. tools/bringup.cpp -o /tmp/e32 && /tmp/e32 out.ppm
```

It writes a 320×240 PPM. The frame exercises: two independently rotated/scaled
background layers (one **additive**), six sprites at varied scale / rotation /
alpha / blend, and priority interleaving (the big centre orb draws *behind* the
glow-grid while the small orbs draw *in front*).

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
