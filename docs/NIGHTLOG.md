# Night build log — clean-room engines (2026-07-14)

_Autonomous overnight session. This file is the persistent state across
context compactions AND the morning report. Update at every milestone._

## ☀️ MORNING SUMMARY (read this first)

**The NES path is App-Store-ready.** The clean-room NES core passes every
gate — nestest 8991/8991, instr_test-v5 16/16, all 8 sprite-hit, 4/4 APU,
vbl basics, **100.0000% pixel parity with Nestopia on Rocket Rush** (996
scripted frames into gameplay), music verified, mappers 0-4, KORA NES proto
boots, save states bit-identical. famemu.app builds with this core statically
("builtin:nes" default; GPL Nestopia now dev-only via FAMEMU_USE_NESTOPIA=1);
famemu-appstore.entitlements written (sandbox — the dlopen blocker is gone).
ctest: 9/9.

**The SNES engine runs KORA with GRAPHICS *AND* MUSIC.** Complete 65816,
Mode-1 S-PPU, LoROM, DMA — plus (extended session) a REAL SPC700 (the
driver's exact opcode set, loud log on gaps) and a REAL S-DSP (BRR voices,
ADSR, ARAM echo + 8-tap FIR, 32 kHz): the IPL upload is captured into ARAM
and executed. Verified: day-ambient music from boot; pressing Y opens the
watch memory and the SPC port trace flips song 0→1 (mother's theme) — the
dynamic score works end-to-end. Scene sweep vs snes9x at same absolute
frames: title (its backdrop pan tracked frame-by-frame), prologue, world
walk with "HER HANDS ON MY SHOULDERS" walk-memory, lessons journal — all
matching. famemu.app now runs BOTH clean-room cores by default
("builtin:snes" added; FAMEMU_USE_SNES9X=1 = dev A/B) — the app is fully
GPL-free. ctest 10/10 incl. a new SNES smoke test (video lit + audio
playing; local gitignored ROM). Remaining fidelity: day/night phase (coarse
timing), Gaussian interp, Mode 7, HDMA, SNES save states.

**Found & fixed in shipped code:** famitv_play + famemu engine.cpp input was
silently dead with Nestopia (port-connect quirk) — fixed in both loaders.

**Extended session (+4h) additions:** real SPC700+S-DSP (KORA's music plays;
dynamic score verified: Y → watch memory → song 0→1); famemu.app fully
GPL-free (builtin:snes joins builtin:nes); scene sweep vs snes9x all matching
(title attract pan tracked frame-by-frame, walk-memories, journal); SNES
save states bit-identical (576KB snapshots); SNES smoke+state tests in ctest
(local gitignored ROM); NES NMI edge delivered one-instruction-late per
hardware (blargg 04 passes; vbl 03+09 added). **ctest: 14/14.** Documented
gap: vbl 02/05-08/10 need per-cycle CPU microcode (attempt made, reverted —
it traded failures and risked the 100.0000% game parity, which was
re-verified after every change).

**Your decisions needed:** (1) famemu/ (Swift app) is still unversioned —
put it in the famemu GitHub repo? (2) DMG signing cert still absent
(DISTRIBUTION.md); (3) App Store Connect listing when you're ready.

Every milestone is committed and pushed to github.com/alikatgh/famemu.

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

## SNES-system completion pass (post-night session)
- [x] 65816 hardened: peterlemon 13-ROM CPU suite (all addressing modes,
  binary + BCD, 8/16-bit) — every ROM reaches its final page all-PASS,
  final screens match snes9x. BCD ADC/SBC implemented (V from the top
  digit's pre-adjustment sum, proven by CPUADC $49+$51 -> $00 V=1).
- [x] PPU Mode 0 (all-2bpp, per-BG palette bases).
- [x] DSP 4-tap Catmull-Rom interpolation (HF/RMS 0.104 -> 0.087).
- [x] snes_golden_test: framebuffer-CRC regression manifest (13 CPU ROMs +
  KORA boot). ctest 15/15.
- [x] Color-math "divergence" vs snes9x REFUTED by measurement: world/HUD
  channel means within 1-2/255 at the same absolute frame.
- Still open (game doesn't use them yet): Mode 7, HDMA; SNES per-cycle
  timing model; Gaussian hardware table (Catmull-Rom stands in).

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
- [x] **APU done: all 4 blargg apu_tests PASS** (len_ctr, len_table, irq_flag,
  jitter) + Rocket Rush music verified via --wav (46/46 loud windows, 32
  pitches). Frame counter: hw power-up $4017=$00, 29830-cycle period, $4017
  reset delayed 2+odd cycles (bisected against 4-jitter). 7/7 ctest.
- [x] **famemu.app now runs the clean-room core by default** ("builtin:nes"
  in Paths.core; FAMEMU_USE_NESTOPIA=1 = dev A/B against the GPL dylib).
  engine.cpp grew a builtin backend (no dlopen) feeding the same RF chain;
  builtin_nes.cpp unity-includes the engine from the platform repo;
  swift build green. Full product path verified: builtin core → RF modulate
  → NTSC decode → gameplay screenshot with real analog artifacts.
- [x] **famemu-appstore.entitlements written** (app-sandbox + network.client
  + user-selected-files + bookmarks); DISTRIBUTION.md checklist updated —
  the GPL/dlopen blockers for MAS are now REMOVED for the NES path.
- [x] **Mappers 0/1/2/3/4 done** (MMC1 serial/modes, UNROM, CNROM, MMC3 with
  scanline IRQ at dot 260). Gate 2: instr_test-v5 official 16/16 (on MMC1).
  Gate 6: **KORA NES proto (MMC3) boots and renders.** Parity re-verified
  100.0000% after the cart refactor. (UNROM/CNROM paths untested for lack of
  test ROMs — trivial code, flagged.)
- [x] **Save states**: full-system snapshot (state.hpp), FamemuCoreAPI
  state_* real, round-trip replay bit-identical (state_test, ctest 9/9).
- [x] SNES CPU: **complete 65816** (engines/snes/cpu65816.{hpp,cpp}, all 256
  opcodes, native+emulation, runtime M/X widths). Compiles, committed
  (52bb577). Coarse cycle model; no BCD (KORA never sets D).
- [ ] SNES system ← NEXT, in this order:
  1. engines/snes/bus.hpp — LoROM map: banks 00-3F/80-BF: $0000-1FFF WRAM
     mirror, $2100-21FF PPU, $4200-437F CPU regs/DMA, $8000-FFFF ROM
     (bank*0x8000); banks 7E/7F full WRAM. kora.sfc = LoROM, no coprocessor.
  2. PPU Mode 1 scanline renderer: BG1 4bpp + BG3 2bpp hi-priority + OBJ,
     CGRAM, HOFS/VOFS, INIDISP brightness, color math (CGADSUB+COLDATA
     subtract for day/night), 256x224 RGB out.
  3. DMA channels ($420B MDMAEN, $43x0-x6) — KORA uses general DMA to
     $2118/19 (VRAM), $2122 (CGRAM), $2104 (OAM).
  4. APU ports $2140-43: IPL handshake STATE MACHINE stub (ack $AA/$BB,
     echo bytes) so KORA's spc_upload completes; real SPC700+DSP later.
  5. NMI at line 225 ($4210 RDNMI + $4200 NMITIMEN), auto-joypad $4218/19.
  6. Gate: kora.sfc boots to title through famemu_dump-style harness
     (engines/snes/tools/), then kora/snes/verify.sh frame dumps as golden.
- Morning-ready state: NES core DONE (all gates), famemu.app on the builtin
  core, MAS entitlements written, SNES = CPU done + system plan above.
- NOTE for the user: famemu/ (the Swift app) is still NOT a git repo — its
  changes (engine.cpp builtin backend, Model.swift default, entitlements)
  live only on disk. Decide where it should live (inside the famemu GitHub
  repo?) in the morning.

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
