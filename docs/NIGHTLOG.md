# Night build log — clean-room engines (2026-07-14)

_Autonomous overnight session. This file is the persistent state across
context compactions AND the morning report. Update at every milestone._

## Mission (user, before sleep)
Work all night toward: clean-room engines (NES first, SNES next) and a
famemu.app ready for Mac App Store submission (GPL-free, sandboxable).
Priority: (1) NES gate 5 parity → (2) APU → (3) mappers+KORA proto →
(4) static core into famemu.app → (5) entitlements/build → (6) SNES start.

## Ground rules for the night
- Commit + push each milestone to github.com/alikatgh/famemu (origin/main).
- Update this file + docs/BUG_JOURNAL.md as things land (same commit).
- No user questions until morning — pick sensible defaults, note them here.
- Clean-room discipline: NESdev/SNESdev docs + test ROMs only, no GPL source.

## Status board
- [x] Gate 1: 6502 passes nestest 8991/8991 (commit c79c61f)
- [x] PPU (dot-driven, Loopy, sprite-0) + system + FamemuCoreAPI facade
- [x] Rocket Rush boots + renders (title, story) on the clean-room core
- [x] **Gate 5: pixel parity vs Nestopia — 100.0000% (61440/61440 px)** at
  frame 996, through 3 START transitions into live gameplay (waves, HUD,
  scroll, sprites). Title @264: 99.79%. The road there found and fixed:
  - MY dump_ppm arg miscount ("0 0 0 script" puts script in argv[8], ignored →
    every nestopia run was 24 boot frames only; wasted an hour chasing "dead
    input" that was my own harness bug).
  - PPU bug 1: reload-then-shift in the same dot gave fresh tiles an extra
    shift → whole BG rendered 1px left (blargg 02.alignment #3 caught it).
  - PPU bug 2: sprites drawn at OAM Y, not Y+1 (02.alignment #8 caught it).
  - Nestopia quirk (real, fixed in both loaders): ports stay unconnected for
    out-of-database ROMs; plain RETRO_DEVICE_JOYPAD is #defined to AUTO and
    re-runs the failing auto-detect — must pass SUBCLASS (1<<8)|1.
- [x] **Gate 3 (partial): all 8 blargg sprite-hit tests PASS** (basics,
  alignment, corners, flip, left_clip, right_edge, screen_bottom,
  double_height) + ppu_vbl_basics PASS. blargg_harness supports the $6000
  protocol + screen dumps.
- [ ] APU (pulse x2, tri, noise, DMC, frame counter) + audio_read
- [ ] Mappers 1/2/3/4 (+ MMC3 A12 IRQ) → KORA NES proto (gate 6)
- [ ] blargg test ROMs (instr/ppu_vbl_nmi/sprite-hit/apu) as ctest targets
- [ ] famemu.app: static famemu_nes core replaces libretro/nestopia dlopen
- [ ] Sandbox entitlements set + clean build of famemu.app
- [ ] SNES: 65816 core start (LoROM, kora.sfc as target)

## Decisions made overnight (defaults picked without asking)
- Parity metric: nearest-NES-index match ≥99.5% per frame across title,
  story, and gameplay frames (palettes differ between emulators; index space
  is the common ground).
- (add as they happen)

## Where things are
- Engine: engines/nes/ in the famemu repo (cpu, ppu, cart, system, facade).
- Harnesses: build/nestest_harness, build/famemu_dump,
  engines/nes/tests/compare_frames.py; Nestopia reference via
  FAMEMU_RAW=1 game/dump_ppm (script arg #8, same grammar).
- Scratch frames: $SCRATCHPAD/famemu_frames/.
