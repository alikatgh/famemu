# Ember 32 — specification

The 32-bit Ember console. **Status: design-frozen; phase 2 (reference model)
underway — the 2D compositor is done and bring-up-verified** (see
[`engines/ember32/`](../../engines/ember32/)). This is the spec chosen from
[EMBER32_SCOPE.md](EMBER32_SCOPE.md) — a **2D-first 32-bit console on a real RISC
CPU, with an optional textured-quad unit for pseudo-3D**. It keeps the pixel-art
soul and the RF/analog-TV signature of Ember 8/16 while taking a real
generational leap in sprites, colour, scaling and sound. Numbers here are the
target contract for the reference implementation; they may be refined during
bring-up but not without updating this file.

Source: `engines/ember32/` (reference model in progress). Facade:
`famemu_ember32_core()` (added when wired into the app). `system_id`: `ember32`.

## At a glance

| | |
|---|---|
| CPU | 32-bit RISC (ARM7TDMI-class) @ ~16.7 MHz, cycle/bus-timing model |
| Memory | 4 MB main RAM (+ 512 KB fast on-chip scratch) |
| Video | 320×240 base · up to 640×480 interlaced · 24-bit output |
| Sprites | 1024 in the list, affine (scale/rotate/shear), ≤512×512, per-sprite alpha, per-line fill budget |
| Backgrounds | 6 scroll layers, each independently scaled/rotated, per-scanline register tables |
| Colour math | true alpha + additive/subtractive blend, per-pixel priority, mosaic, global fade, colour LUTs |
| 3-D (optional) | textured/Gouraud **quad** unit (not a general triangle GPU) |
| Audio | 32-voice PCM/ADPCM + ADSR + reverb/echo bus + 2 streamed channels, 48 kHz stereo |
| Cartridge | ≤64 MB linear, header-declared capabilities, battery/flash save, optional streamed asset region |
| Peripherals | 2–4 digital pads; optional analog stick + rumble |
| Output | composite / RF (the Ember signature) |

## CPU

A full ARM7TDMI-class 32-bit RISC: ARM + Thumb instruction sets, the standard
integer pipeline, IRQ/FIQ, and a cycle-accurate **bus-timing model** — every
access is charged its region's real cost (fast on-chip scratch vs. main RAM vs.
ROM wait-states), and DMA steals bus cycles, exactly as Ember 16 charges FastROM
/ DMA. Chosen for a **mature, free toolchain**: `clang`/`gcc` compile C and the
wick language straight to ARM/Thumb — no bespoke assembler, the way ca65 already
serves Ember 8/16.

*(MIPS-R3000 and SH-2 were considered; ARM7TDMI wins on toolchain maturity and
the availability of good reference cores to cross-check timing.)*

## Video — the 2D compositor

The heart of the machine. A scanline compositor with the defining features of
the mid-90s 2D peak:

- **Sprite engine.** Up to 1024 sprites in the display list; each carries a 2×3
  **affine matrix**, so every sprite scales, rotates and shears freely, sub-pixel
  positioned, with per-sprite alpha and a priority key. A real **per-scanline
  fill-rate budget** is modelled — overloading a line drops/flickers sprites in
  the hardware's order, the same authenticity discipline as the Ember 8/16 PPUs
  (documented in the bug journal, gated by feature carts).
- **Scroll layers.** 6 tile layers, 8×8 or 16×16 tiles, each independently
  scaled/rotated (every layer is "mode-7-class", not just one). Per-scanline
  **register tables** (the HDMA idea generalised) drive scroll/scale/palette/
  window per line — raster splits, line-scroll water, parallax, and per-line
  colour all render pixel-accurately.
- **Colour + blend.** 24-bit internal pipeline. True per-pixel alpha, additive
  and subtractive blend, a per-pixel priority resolve between all layers and
  sprites, global brightness fade, mosaic, and colour lookup tables.
- **Windows/clip.** Two windows per layer with combine (AND/OR/XOR) and
  clip-to-black, as Ember 16 has, generalised across the 6 layers.
- **Output stage.** Composites to a 24-bit buffer, then feeds the **existing
  RF/NTSC encoder** — composite artifacts and every CRT / VHS / LCD / mono /
  composite look apply to Ember 32 exactly as they do to Ember 8/16. The analog
  signature is preserved with zero new work.

## Optional quad unit (pseudo-3D)

A **textured/Gouraud quad** rasteriser — the Saturn idea of "3-D as distorted
sprites", *not* a general polygon GPU. It draws four-cornered, texture-mapped,
optionally shaded quads with affine (not perspective-correct) mapping, reusing
the sprite texture path. This gives cheap pseudo-3D — perspective floors,
scaling-sprite shooters, racers, Mode-7++ — for devs who want it, with **no 3-D
content pipeline required** to ship a great 2D game. It is a capability a
cartridge opts into, not a mandate.

## Audio

A 32-voice sound subsystem: PCM and ADPCM samples with ADSR/GAIN envelopes, a
small effects bus (reverb/echo), pitch modulation and noise, plus **two streamed
channels** for CD-quality music, mixed at 48 kHz stereo. The mix may optionally
pass through the app's "mono TV speaker" `AudioFX` colouring for period sets.

## Cartridge / format

A clean **linear ROM up to 64 MB**, header-declared capabilities (video modes,
quad unit, audio voices, save type), battery or flash **save RAM**, and an
optional **streamed asset region** for music/large assets. Signed by the same
store pipeline with the same `.emsig` posture as Ember 8/16 — same security
model, same "it could burn to silicon" honesty.

## Regions and output

NTSC (60 Hz) and an optional PAL-class (50 Hz) timing from the cartridge header,
same as Ember 16. The frame buffer switches width for interlaced hi-res exactly
as a TV would show it. Everything renders down the composite/RF path.

## Verification model

Because Ember 32 is an **original design** (not a clone of one real console), the
authoritative reference is **our own portable C model** of the compositor, CPU
and audio — correctness over speed, "the spec, executable." The fast core is
verified **pixel-for-pixel** against that model, plus a suite of **feature test
carts** (raster splits, every blend term, affine/rotate edge cases, fill-rate
overload order, quad texture mapping) wired into CI — the same discipline that
gates Ember 8 (community test ROMs) and Ember 16 (13 CPU carts + reference
lockstep). This is the platform's authenticity claim, and it is non-negotiable.

## Reference game

TBD — a small original launch title (the "Rocket Rush of Ember 32") proves the
content loop end to end: wick + toolchain → cartridge → signed → runs → verified.
It becomes the model for store submissions, as KORA is for Ember 16.

## Build phases (see EMBER32_SCOPE.md §6)

1. **Spec** — this document. ✅
2. **Reference model** — the portable C model; correctness first. 🔄 **in progress**:
   the 2D compositor is done + bring-up-verified (`engines/ember32/compositor.hpp`,
   `tools/bringup.cpp`); CPU, MMIO, per-scanline tables, audio and the quad unit next.
3. **Bring-up cart** — boots, scrolls, draws a scaled/rotated sprite, out the RF path, screenshot.
4. **Fast core + gates** — optimise, verify vs the reference, feature carts in CI.
5. **Toolchain + launch title** — wick support + the first original game.

## Decisions (the scope's open questions, resolved)

Settled for phase 2 — changing any of these re-opens the spec:

- **CPU = ARM7TDMI-class.** Mature, free toolchain (`clang`/`gcc`, ARM + Thumb) and
  good reference cores to cross-check timing; ideal for a 2D-first machine. MIPS /
  SH-2 would fit an early-3D pivot — not our direction.
- **3-D = quad-only.** The optional textured/Gouraud quad unit, *no* general polygon
  GPU: pseudo-3D for those who want it, with no 3-D content pipeline required to ship
  a game. A full polygon pipeline is deferred to a separate "Ember 3D" project.
- **Fantasy-spec, verified vs. our own reference model.** Ember 32 is an original
  design (not a clone of one console), so the authority is the portable C model in
  [`engines/ember32/`](../../engines/ember32/) — already begun; the compositor is
  done and bring-up-verified.
