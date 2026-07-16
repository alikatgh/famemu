# Ember 32 — scoping proposal

**Status: SCOPING, not a spec.** This frames the design decision, recommends a
direction, and sizes the work. The numbers below are a *proposed* target, not a
frozen spec — the one thing that must be decided first is the **direction**
(§1), because it changes everything downstream. Nothing here is built yet.

Prereqs it assumes: Ember 8 ([EMBER8.md](EMBER8.md)) and Ember 16
([EMBER16.md](EMBER16.md)) shipped and verified; the free-engine / curated-store
model ([README.md](README.md)); and RF/analog-TV output as the platform's
signature ("the colours and artifacts emerge from the signal itself").

---

## 1. The decision that comes first: what *is* the 32-bit Ember?

The real 32-bit generation forked in two, and Ember has to pick a soul:

| | **A — Early-3D console** | **B — 2D powerhouse** |
|---|---|---|
| Archetype | PS1 / Saturn / N64 | Neo-Geo / CPS-3 / Saturn-2D |
| Image | textured polygons, lo-fi 3D | huge scaled/rotated sprites, many layers |
| Ember's soul | **changes** — pixels become polygons | **kept** — still pixel art, just spectacular |
| Content bar for devs | high (3D modelling pipeline) | same as today (draw sprites, bigger) |
| New core to build | a whole GPU + geometry + a 3D CPU | a fancier version of the PPU we know |
| RF/TV signature | intact (still composite out) | intact |
| Reference oracle | a real console's emulator | our own spec model (fantasy design) |

There is also a **naming vs. capability** tension: "Ember 32" (after 8, 16)
*reads* as "the 32-bit / 5th-gen console," which culturally means 3D. But the
platform's whole value — an accessible, curated catalogue of **pixel-art** games
that "could burn to silicon" — lives in column B.

**A third axis:** clone a *real* class (inherits a real toolchain + a reference
emulator to verify against, the way Ember 8/16 verify against NES/SNES) **vs.**
an *original* Ember fantasy spec (design freedom, but "the reference" becomes our
own portable C model rather than someone else's silicon).

---

## 2. Recommendation

**Ember 32 = a 2D-first 32-bit console (column B), on a real off-the-shelf RISC
CPU, with an *optional* textured-quad unit for pseudo-3D — verified against our
own reference model.**

Why this threads every constraint:

- **Keeps the soul.** Ember sells pixel-art games on a real TV. A pure-3D clone
  abandons that; a 2D powerhouse makes the *same* art spectacular (big sprites,
  hardware scale/rotate, alpha, parallax, far more colour).
- **Buildable by this team.** We already own two authentic 2D PPUs. A richer 2D
  video core is a large-but-known problem; a full PS1-class GPU + geometry (GTE)
  + microcoded RSP is arguably bigger than Ember 8 + 16 combined.
- **Doesn't raise the content bar.** Devs keep drawing sprites and tiles — no
  3D-modelling pipeline required to ship a great Ember 32 game. The optional
  quad unit lets ambitious devs do racers / Mode-7++ / sprite-scaling shooters
  without *mandating* 3D.
- **Real toolchain, no bespoke assembler.** Base the CPU on **ARM7TDMI** (or a
  MIPS/SH-class RISC) so `clang`/`gcc` cross-compile C and wick out of the box —
  the way ca65 already serves Ember 8/16.
- **RF/TV intact.** Composite output stays the signature; the effect pipeline
  (incl. the new [VHS/LCD/mono/composite looks](../../../famemu/Sources/famemu/CrtShader.swift))
  applies unchanged.

The full early-3D console (column A) stays on the table as a *later, separate*
"Ember 3D" — a deliberate 3D platform, not a forced reinterpretation of the 2D
lineage. Scoping it is its own project.

---

## 3. Proposed spec (recommended direction) — at a glance

Progressing from Ember 16, aimed at "the dream 2D machine of ~1994–97":

| | Ember 16 (today) | **Ember 32 (proposed)** |
|---|---|---|
| CPU | 16-bit 65816 @ ~3.5 MHz | 32-bit RISC (ARM7TDMI-class) @ ~16–33 MHz |
| Memory | 128 KB WRAM | 4–8 MB main RAM |
| Resolution | 256×224 (512 hi-res) | 320×240 base, up to 640×480 interlaced |
| Colour | 15-bit palette | **24-bit framebuffer**, direct or paletted |
| Sprites | 128, 32/line, ≤64×64 | **1024+, scaled & rotated, ≤512×512, alpha** |
| Layers | 4 BG, mode-7 rotate/scale | 4–6 scroll layers, per-line, rotate/scale each |
| Blending | color-math add/sub | true alpha, additive, per-pixel priority |
| 3D (optional) | — | textured/gouraud **quad** unit (no full polygon GPU) |
| Audio | 8-voice BRR DSP | **16–32 voice** PCM/ADPCM + streamed CD-quality mix |
| Cartridge | ≤8 MB LoROM/HiROM | ≤64 MB linear, battery/flash save, optional stream |
| Output | composite / RF | composite / RF (unchanged signature) |
| Peripherals | 2 pads + multitap | 2–4 pads, analog stick + rumble (optional) |

Every number is a knob for the direction discussion, not a commitment.

---

## 4. Video core (the heart of the work)

A **2D compositor** with the era's defining features:

- **Sprite engine**: a large sprite list; each sprite an affine cell (2×3
  matrix) → free scale/rotate/shear, sub-pixel positioned, with per-sprite alpha
  and priority. Model a real per-line fill-rate budget (so "too much on one line"
  drops/flickers authentically, like Ember 8/16 already do).
- **Scroll layers**: 4–6 tile layers, each independently scaled/rotated, with
  per-scanline register tables (the HDMA idea, generalised) for raster splits,
  line-scroll water, parallax, and window/clip logic.
- **Colour + blend**: 24-bit internal, true alpha and additive blend, per-pixel
  priority resolve, a global fade/mosaic, and colour LUTs.
- **Optional quad unit**: draw textured/gourand-shaded **quads** (Saturn-style —
  "3D as distorted sprites"), *not* a general triangle GPU. Gives pseudo-3D and
  perspective floors cheaply, and reuses the sprite texture path. This is the one
  place we borrow the *idea* of column A without its cost.
- **Output stage**: renders to a 24-bit buffer, then **down into the existing
  RF/NTSC encoder** so composite artifacts and the CRT/effect looks apply exactly
  as they do for Ember 8/16. The analog-TV signature is preserved for free.

Reference model = a portable, obviously-correct C compositor; the fast core is
verified pixel-for-pixel against it (the internal method Ember already uses),
plus a suite of feature test carts (raster splits, blend terms, affine edge
cases, fill-rate overload order).

---

## 5. CPU, audio, cartridge

- **CPU:** ARM7TDMI-class keeps us on a mature, free toolchain (`clang`/`gcc`,
  and wick lowering to C → ARM). Alternatives: MIPS (PS1 lineage) or SH-2
  (Saturn). Decision criterion = toolchain maturity + a good reference core to
  cross-check timing. Model a real cycle/bus-timing budget as Ember 16 does.
- **Audio:** 16–32 hardware voices, PCM/ADPCM samples, ADSR, a small effects bus
  (reverb/echo), and one or two **streamed** channels for CD-quality music —
  mixed at 44.1/48 kHz, then optionally band-limited by the existing "mono TV
  speaker" `AudioFX` colouring.
- **Cartridge/format:** a clean linear ROM up to ~64 MB, header-declared
  capabilities, battery/flash save, optional streamed asset region — signed by
  the same store pipeline, same `.emsig` posture as Ember 8/16.

---

## 6. What has to be built (and roughly how big)

| Piece | Effort | Risk |
|---|---|---|
| CPU core (real RISC) + timing | **M** (toolchain exists; timing model is the work) | Low–Med |
| 2D compositor (sprites+layers+blend) | **XL** (the centrepiece) | Med |
| Optional quad unit | **M** (reuses texture path) | Med |
| Audio (voices + streaming + effects) | **L** | Low–Med |
| Cartridge/format + save + signing | **S** (mirror Ember 16) | Low |
| RF/NTSC output integration | **S** (feed the existing encoder) | Low |
| Reference model + feature-cart gates | **L** (non-negotiable — it's the authenticity claim) | Low |
| wick language + std lib for the new CPU | **L** | Med |
| Tooling: asset pipeline, docs, samples | **M** | Low |

Total is a **major** effort — plan it as its own multi-phase project, not a
sprint. Suggested phases:

1. **Paper spec** — freeze §3 numbers once the direction is chosen; write EMBER32.md.
2. **Reference model first** — the portable C compositor + CPU, correctness over speed. This *is* the spec, executable.
3. **Bring-up cart** — a hand-written test cart that boots, scrolls, draws scaled sprites; wire RF output; screenshot it.
4. **Fast core + gates** — optimise, verify pixel-for-pixel vs the reference, add feature carts to CI.
5. **Toolchain + a real game** — wick support, a small original launch title (the "Rocket Rush of Ember 32") to prove the content loop.

---

## 7. Platform fit (unchanged model)

- **C ABI:** a `famemu_ember32_core()` facade beside the 8/16 cores; the app
  already switches cores by `system_id` (add `ember32`; keep 8/16 stable).
- **Store:** original-IP-only, determinism + save-state QA gates, source escrow —
  same [STORE_POLICY.md](STORE_POLICY.md). A 32-bit tier can price higher.
- **Naming/heritage:** public name is **Ember 32** only; lineage stays generic
  except in [HERITAGE.md](HERITAGE.md). If the direction clones a real class,
  keep the clean-room policy (no dumped BIOS/microcode; reference emulators as
  test oracles only).
- **RF signature:** preserved — Ember 32 renders into the same composite encoder,
  so every CRT set and the new VHS/LCD/mono/composite looks apply to it too.

---

## 8. Open questions (yours to decide)

1. **Direction (the big one):** 2D-first with optional quads *(recommended)*,
   full early-3D console, or an even more conservative "Ember 16 Turbo"?
2. **Fantasy spec vs. clone a real class** — do we want a real reference
   emulator to verify against (authenticity, but inherit a real console's warts),
   or design the ideal spec and treat our C model as the reference?
3. **CPU:** ARM7TDMI *(recommended for toolchain)* vs MIPS vs SH-2.
4. **How much 3D** — none, quad-only *(recommended)*, or a real polygon pipeline?
5. **Priority vs. the rest of the roadmap** — this is a major build; where does it
   sit against store launch, iOS, and finishing 8/16?

Once §1/§8 are answered, this becomes `EMBER32.md` (the frozen spec) and phase 2
begins.
