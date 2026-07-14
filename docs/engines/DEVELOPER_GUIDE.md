# Writing games for Ember 8 / Ember 16

You are writing a real cartridge image. The toolchain is the mature cc65
suite (`brew install cc65`): `ca65` assembles 6502 (Ember 8) and 65816
(Ember 16), `ld65` links against a memory config.

## Start from the working examples

| | Ember 8 | Ember 16 |
|---|---|---|
| Full game | `game/rocket.s` | `kora/snes/kora.s` |
| Minimal ROM + linker config | `engines/nes/tests/` | `engines/snes/tests/mode7/` (`lorom32.cfg`) |
| Build script | `game/build.sh` | `kora/snes/build.sh` |
| Feature demos | — | `engines/snes/tests/features/*.s` (windows, hi-res, raster splits, every expansion chip) |

The feature test ROMs double as tutorial code: each one sets up video from
reset, uploads tiles/palettes, and exercises exactly one subsystem in ~200
lines.

## Cartridge headers

Ember 16 images carry the standard 16-bit cartridge header at $7FC0 (LoROM)
or $FFC0 (HiROM): title, map mode, chip byte, sizes, region, checksum. The
engine detects mapping and expansion chips from it — get the chip byte and
checksum right or detection will pick the wrong mapping.
`engines/snes/tests/features/build.sh` shows how to patch a real checksum
(pass the header offset explicitly; never guess it from the image).

Region byte ≥ 2 selects PAL (312 lines, 50 Hz).

## Running your game

```sh
# Headless: run N frames with scripted input, dump frames as PPM
build/snes_dump game.sfc /tmp/out "0:60;8:2;0:120"   # hold nothing 60f, Start 2f, ...

# In the app: drop the image on famemu.app (dev builds run unsigned carts)
```

The input-script grammar (`HEXMASK:FRAMES;...`) is shared by every harness
— the same script drives your game in CI, in the lockstep differ, and in
store QA.

## Testing like the engine does

- **Golden screens**: `snes_golden_test manifest` CRCs your frames at fixed
  frame counts. Any unintended change to rendering fails CI. Add your
  game's boot/title/gameplay frames to your own manifest.
- **Determinism**: run your input script twice; byte-identical output is
  required for store submission.
- **Save states**: `snes_state_test` round-trips a state mid-game and
  requires identical continuation.
- **Audio**: `snes_dump --wav` writes the soundtrack; listen, and keep an
  RMS/peak sanity check in your build.

## Debugging

- `SNES_DEBUG_ZP=1 snes_dump ...` prints zero-page state per frame (wire
  your game's key variables into low ZP and you get free tracing).
- `SNES_DEBUG_SLOTS=1` dumps ZP words $02+ — the convention the feature
  ROMs use for "result slots".
- Raster/timing issues: read `docs/BUG_JOURNAL.md` first — the diff-shape
  cheat sheet there maps symptoms to subsystems.

## What the engine guarantees you

Hardware-authentic behavior — verified against reference implementations,
not approximated. If you write to the mode-7 matrix mid-frame, change
scroll via HDMA, overload a scanline with sprites, or split the backdrop
color mid-line with an H-IRQ, you get what silicon would give you. The
constraints are the medium; they are enforced honestly.
