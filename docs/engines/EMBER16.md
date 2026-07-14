# Ember 16 — specification

The 16-bit Ember console. Source: `engines/snes/`. Facade:
`famemu_snes_core()` (internal id pending rename).

## At a glance

| | |
|---|---|
| CPU | 16-bit 65816, master-cycle timing (fast/slow bus regions, FastROM) |
| Video | 256×224 (512-wide hi-res, PAL 50 Hz supported), 15-bit color |
| Backgrounds | up to 4 layers, 8 video modes incl. rotation/scaling (mode 7) |
| Sprites | 128, up to 32/line, sizes 8×8–64×64, hardware limits modeled |
| Audio | dedicated audio CPU + 8-voice sample DSP (BRR), echo, noise |
| Cartridge | LoROM/HiROM up to 8 MB, battery RAM, expansion chips |
| Peripherals | 2 pads, 5-player multitap, light gun (Super-Scope-class) |

## CPU

Complete 65816: all 256 opcodes, decimal mode, emulation mode, interrupts.
Timing is a master-cycle model — every bus access costs its region's real
speed (6/8/12 cycles, FastROM select), DMA/HDMA steal CPU time, WRAM
refresh is charged. Gated by 13 community CPU test cartridges.

## Video

All eight background modes with their real per-layer bit depths, including:

- **Mode 7** rotation/scaling with the hardware's 13-bit clipping quirks,
  plus the EXTBG priority layer.
- **True hi-res** (modes 5/6 and pseudo-hires): genuine 512-wide output
  with the hardware's subscreen/mainscreen pixel weave. The frame buffer
  switches width dynamically, exactly like a TV would show it.
- **Offset-per-tile** (modes 2/4/6), 16×16 tiles, mosaic, two windows per
  layer with combine logic, color math (add/subtract, half, fixed color,
  clip-to-black) — every term verified against a reference implementation.
- **Per-scanline and mid-scanline effects**: HDMA tables drive registers per
  line, and any register write mid-line takes effect at the CPU's current
  dot (raster splits render pixel-accurately; see `tests/features/raster.s`).
- Correct sprite range/time limits — including the hardware's surprising
  drop order under overload (documented in the bug journal).

## Audio

A real audio subsystem, not a mixer: the main CPU uploads a sound driver
through the mailbox ports to a dedicated audio CPU (full 256-opcode
instruction set) driving the 8-voice DSP — BRR sample decoding, ADSR and
GAIN envelopes, 4-tap Gaussian interpolation, noise, pitch modulation, and
echo with the 8-tap FIR filter, at 32 kHz stereo.

## Expansion chips

Cartridges can declare expansion chips in the header; all of these ship in
the engine:

| Chip class | What it adds | Verification |
|------------|--------------|--------------|
| SA-1-class | second 65816 at ~10.7 MHz, fast RAM, DMA, char conversion, bitmap RAM views | 8 lockstep frames, 0.000% |
| GSU-class (SuperFX) | RISC coprocessor with pixel-plot pipeline for 3-D | lockstep, 0.000% |
| DSP-1-class | fixed-point math + 3-D projection unit | 5 commands bit-exact vs reference; trig table computed, not dumped |
| DSP-2-class | bitmap conversion/scaling unit | all 5 ops lockstep, 0.000% |
| Cx4-class | wireframe/math unit | functional HLE |
| S-DD1-class | entropy decompression fed straight into DMA | exact public-domain algorithm |
| SPC7110-class | data banking + RTC | decompressor **not yet implemented** (the one gap) |
| ST010-class | driving-game math unit | main ops implemented |

## Regions and peripherals

NTSC (262 lines/60 Hz) and PAL (312 lines/50 Hz) from the cartridge header.
Multitap for 5 players, and light-gun support with beam-position latching
(`SnesSystem::set_gun`).

## Reference game

KORA (`kora/snes/`) is the flagship: a full 65816 game with metatile world
streaming, HDMA effects, subscreen color-math cloud shadows, and a
generated sound driver — its build (`kora/snes/build.sh`, ca65) and verify
scripts are the model for store submissions.
