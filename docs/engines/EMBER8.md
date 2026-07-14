# Ember 8 — specification

The 8-bit Ember console. Source: `engines/nes/`. Facade: `famemu_nes_core()`
(internal id pending rename).

## At a glance

| | |
|---|---|
| CPU | 8-bit 6502-family (no BCD), ~1.79 MHz |
| Video | 256×240, 64-color master palette, 25 colors on screen per frame class |
| Backgrounds | one 32×30 tile layer + scroll, 8×8 tiles, 2bpp |
| Sprites | 64 total, 8 per scanline, 8×8 or 8×16 |
| Audio | 2 pulse + triangle + noise + sample (DPCM) channels |
| Cartridge | PRG banks + CHR banks/RAM, mappers 0/1/2/3/4 |
| Output | RGB888 via the core API — plus the analog-RF chain for CRT looks |

## CPU

Full documented 6502 instruction set with hardware-accurate cycle counts.
The engine is gated by the standard community CPU test cartridge run to a
golden log (`nestest_harness`).

## Video

Scanline-based tile renderer with hardware-faithful behaviors:

- Pattern/name/attribute table addressing, both nametable mirroring modes
  plus mapper-controlled mirroring.
- Sprite 0 hit and the 8-sprites-per-line overflow, so classic raster
  techniques (status-bar splits via sprite-0, scroll changes mid-frame)
  work exactly as on hardware. Rocket Rush's HUD split uses this.
- NTSC master palette generated from the same model as the analog chain.

## Audio

The 5-channel PSG: two pulse channels with sweep/envelope/length counters,
triangle with linear counter, LFSR noise, and DPCM sample playback that
fetches over the CPU bus (with the hardware's DMA steal). Frame counter in
both 4- and 5-step modes. Gated by the standard APU timing test cartridges
(`blargg_harness`, 4 tests in CI).

## Cartridge format

Standard `.nes` container (iNES header). Supported board classes:

| Mapper | Board class | Gives you |
|--------|-------------|-----------|
| 0 | fixed 32K | simplest possible game |
| 1 | serial-shift banking | 512K PRG, CHR banking, switchable mirroring |
| 2 | PRG bank switch | large games with CHR-RAM |
| 3 | CHR bank switch | animation-heavy small games |
| 4 | the workhorse | fine-grained PRG/CHR banks + scanline IRQ counter |

The scanline IRQ (mapper 4) is the tool for parallax splits and effects
beyond sprite-0.

## Reference game

Rocket Rush (`game/rocket.s`, ca65) is the maintained example: data-driven
enemy formations, vertical-mirroring scroll with a sprite-0 HUD split, and
a build script (`game/build.sh`) that assembles a store-ready cartridge.
