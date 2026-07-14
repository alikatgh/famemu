# Bug Journal

**Read this before debugging.** Every fix here taught a generalizable lesson.
The top section is the cheat-sheet; the chronological log has the receipts.

When you fix a bug, append an entry. **Same commit as the fix.** Five lines
max per entry. No drift.

Global rules: `~/.claude/CLAUDE.md`.

---

## Patterns to scan for FIRST

Before reproducing, grep this list for the shape of your bug.

- **Unvalidated CLI float knobs: clamp/floor before use** — a negative float
  cast to an unsigned type is UB, and `1/(2*x*x)` with `x=0` makes
  `inf->NaN` that survives `std::clamp` into a float->int cast.
- **A tool that writes output files must create its output dir AND check
  the write return value** — otherwise it reports success (exit 0) having
  written nothing; the empty-dir case reads as "passed".
- **dlopen'd plugin: every symbol you call unconditionally must be in the
  `load()`-time required set**, or a missing one is a null-deref at call
  time (warn-and-continue is not enough).
- **Making a required build dep optional silently drops the loud-failure
  invariant** — gate the primary target on an explicit opt-out, else
  `FATAL_ERROR`.
- **Making a build gate `FATAL`-by-default breaks every caller that ran the
  plain configure** — after adding a fatal dep check, grep the project's own
  `cmake`/CI/script invocations and pass the new opt-out where appropriate;
  a per-file review won't surface this cross-file interaction.
- **PPU off-by-ones hide in BG/OBJ asymmetry** — NES: sprite rows are OAM
  Y+1; SNES: fb row 0 is scanline 1 (BG needs +1, OBJ doesn't). Verify with
  a cross-emulator pixel diff or alignment test ROM, never by eye.
- **A "scoped to game X" emulator rots as game X grows** — re-diff against
  a reference emulator through scripted GAMEPLAY (movement, state changes),
  not just the boot/title frame; align game time (boot pad), not host time.
- **Lockstep diff SHAPE identifies the subsystem before any code-reading** —
  sprite-band-only = OBJ evaluator; outline-of-everything = 1px shift (find
  axis with an np.roll sweep); ±1-unit channel dots = one color-math term;
  solid frame = one side never booted (header/mapping detection).
- **Undocumented hardware behavior: probe ROM beats documentation** — build
  a minimal .sfc that makes each candidate behavior visually distinct and
  let the reference emulator adjudicate (see tests/features/sprlimit.s).
- **CPU timing models: per-access bus costs ARE the cycle count** — adding
  blanket per-op overhead double-charges and shows up as dropped game
  frames (input/scroll lag vs reference), not as rendering errors.

---

## Reusable tools

Add entries here for any reusable harness you build in `scripts/`. Format:
script name → one-line "what bug it was built to catch".

- `engines/snes/tests/verify_features.sh` — lockstep-diffs 29 frames against
  snes9x across windows/mosaic/all BG modes/OPT/HiROM/mul-div-IRQ/SA-1/
  SuperFX/OBJ limits. Catches any divergence a scoped golden CRC would bake
  in as "correct". Skips cleanly without the parent snes9x checkout.
- `engines/snes/tests/features/build.sh` — rebuilds all feature/coprocessor
  test ROMs (ca65) and patches real header checksums at an EXPLICIT offset
  (guessing the offset from the image mis-detects the mapping).

---

## Chronological log

Newest first. Five lines max per entry. File:line citations beat prose.

### 2026-07-14 (PM3) · Gate a decompressor you can't encode with a known-answer test
Symptom: SPC7110 decompressor had no gate — no PD test cart, and hand-authoring a compressed stream needs the (unwritten) encoder.
Cause: the decoder is total (any bytes decode to *some* deterministic output), so a lockstep frame test wasn't needed — just faithfulness of the port.
Fix: KAT — run OUR port and byuu's reference decoder over the SAME pseudo-random "data ROM" (identical LCG), require byte-identical output for modes 0/1/2 (snes_spc7110_test.cpp, tools/spc7110_ref.md documents regen).
**Lesson:** to gate a faithful port of a total function, feed both it and the reference the same arbitrary input and diff — no valid/encoded input needed. Hoist the reference's method-`static` decoder state to instance members or two machines share it.

### 2026-07-14 (PM2) · SA-1 char-conversion: the oracle's SOURCE beats guessing the protocol
Symptom: CC1/CC2 test slots 100% off vs snes9x while famemu matched my own design exactly.
Cause: I guessed CC semantics (trigger regs, type-bit polarity, pixel packing); snes9x's actual model differs on all three (CC2 = $224F-only, 16 pixels/write, tile per 4 writes; CC1 converts during S-CPU DMA from banks $40+, not window reads; DCNT bit4 CLEAR = CC2).
Fix: read snes9x sa1.cpp/dma.cpp (public), implement those semantics (sa1_impl.hpp); sa1.sfc now 8/8 lockstep.
**Lesson:** when the reference emulator IS the gate and its source is public, read it before designing a protocol test — one fetch beats three guess-iterate cycles. (Public-DOMAIN algorithms — S-DD1 by Andreas Naive — can be taken exactly, with credit.)

### 2026-07-14 (PM2) · Test-ROM boot pad shifts which result slot a sampled frame shows
Symptom: DSP-1 lockstep frames "matched the wrong values"; decoded slots looked shifted by one.
Cause: dump_ppm gives snes9x 24 boot frames and famemu gets the same pad, so a frame sampled at script-time n displays slot (n+24)>>5, not n>>5.
Fix: read famemu's slot words from WRAM directly (SNES_DEBUG_SLOTS in snes_dump) before interpreting color-decoded values.
**Lesson:** in lockstep harnesses only FRAME EQUALITY is meaningful; decoding game state out of pixels needs the pad accounted for — or a debug port.

### 2026-07-14 (PM2) · python str.replace patches BOTH read() and write() twins
Symptom: build broke with "void function 'write' should not return a value" after adding chip bus hooks.
Cause: scripted str.replace on a guard block that exists in both SnesSystem::read and ::write inserted read-style hooks into write.
Fix: re-patch with unique surrounding context per function.
**Lesson:** when patching C++ with replace-scripts, anchor on text unique to the FUNCTION, not the guard — read/write pairs are near-identical by design.

### 2026-07-14 · Blanket per-opcode "decode cycle" made the 65816 ~20% slow; game dropped frames
Symptom: KORA play_clouds lockstep 16% off; boy 2px short (pyw 1024 vs 1026) — outdoor only.
Cause: cpu65816.cpp charged +6 master cycles per opcode ON TOP of per-access costs; real memory ops have no extra internal cycle, so streaming-heavy frames overran and the game dropped 2 frames (indoor, no streaming, matched — the tell).
Fix: charge the internal cycle only when an instruction did no bus work beyond its fetch (cpu65816.cpp, end of step()).
**Lesson:** in a bus-cost timing model the per-access costs ARE the cycle count; blanket per-op overhead double-charges. A too-slow CPU shows up as game frame drops, not rendering bugs.

### 2026-07-14 · OBJ time-over drops the LOWEST OAM indices (settled empirically)
Symptom: featppu sprite band ~2% off vs snes9x in every case; famemu kept sprites 0-8, snes9x kept 11-19.
Cause: the 34-sliver budget was spent in range order; hardware/snes9x fetch tiles from the LAST in-range sprite backward, so time-over hides the highest-priority sprites.
Fix: sppu.cpp evaluate_objects draws the range list in reverse with overwrite fill (low index still wins overlaps); tests/features/sprlimit.s is the probe.
**Lesson:** when two emulators disagree on undocumented behavior, don't re-read docs — build a minimal probe ROM and let the reference emulator adjudicate in one run.

### 2026-07-14 · Half-subtract color math: snes9x drops the subtrahend's low bit
Symptom: featppu case 9 (subtract+half) had ±1-unit channel diffs on ~5% of pixels; add+half and subtract-no-half were exact.
Cause: famemu computed (m−s)>>1; snes9x computes (m−(s&~1))>>1 per channel (its guard-bit packed math).
Fix: sppu.cpp render_line masks the operand's low bit before a halved subtract.
**Lesson:** bisect color-math mismatches by toggling one CGADSUB/CGWSEL bit per variant ROM — three lockstep runs isolated the exact term.

### 2026-07-14 · Offset-per-tile column decision must include the BG's own fine scroll
Symptom: featppu case 6 (mode 2 OPT) 1.1% scattered column-edge diffs vs snes9x.
Cause: OPT applied for x>=8 in screen space; hardware shifts the tile-column boundary by the BG's own hofs&7.
Fix: sppu.cpp fetch_bg_pixel uses optcx = x + (hofs&7); entry column = (optcx&~7)−8 + (bg3hofs&~7).
**Lesson:** every "per-tile" PPU feature keys off post-fine-scroll tile boundaries, not screen-pixel columns.

### 2026-07-14 · Checksum patched at the wrong header offset flipped an SA-1 cart to HiROM
Symptom: sa1.sfc booted into $A5 filler at 00:A5A5 in famemu (snes9x ran it); S-CPU stuck in its wait loop → black screen, 100% lockstep diff.
Cause: the ROM build script picked the checksum slot by reading $FFD5 — $A5 filler on a LoROM image — and patched a valid pair into the bogus HiROM slot, so score-based mapping detection chose HiROM and skipped SA-1 instantiation.
Fix: tests/features/build.sh takes the header offset per ROM explicitly.
**Lesson:** with score-based header detection, a "valid" checksum in the WRONG slot flips the mapping; tools that patch headers must be told the layout, never guess it from the image.

### 2026-07-14 · Lockstep diff triage cheat-sheet (from the SNES generalization pass)
Diff confined to sprite rows = OBJ evaluator; outline-of-everything = 1px shift (np.roll sweep finds axis/magnitude); ±1-unit single-channel dots = one color-math term; whole-frame solid = one side never booted (mapping/detection).
Gates: engines/snes/tests/verify_features.sh (29 lockstep frames: windows/mosaic/modes/OPT/HiROM/mul-div-IRQ/SA-1/SuperFX/OBJ limits) + kora/snes/verify_famemu.sh.
**Lesson:** classify the diff SHAPE before touching code — each shape maps to one subsystem.

### 2026-07-14 · HDMA scanline mapping: match snes9x's row = table-line y+1
Symptom: hdma.sfc wave/gradient off by one line vs snes9x (odd rows shifted 1px).
Cause: engine stepped HDMA once at scanline 0 AND once before fb row 0; snes9x maps fb row y to table line y+1 (no scanline-0 slot).
Fix: hdma_init() only at line 0; one hdma_step() before each rendered row (snes_system.hpp run_frame).
**Lesson:** when the gate is "identical to snes9x", match the REFERENCE's timing model, not the datasheet's — and note the divergence in a comment for a future hardware golden. Also: a test ROM writing both VRAM data ports needs VMAIN=$80, or the address advances mid-word and every tile grows a garbage row (seen as dashes in BOTH emulators = your ROM, not the engine).

### 2026-07-14 · Golden CRCs regenerated with a stale test binary "passed" garbage
Symptom: 5 of 6 new mode7.sfc golden entries shared one CRC despite visually distinct screens; ctest still green.
Cause: after the Mode 7 PPU change I rebuilt only snes_dump; snes_golden_test --gen (and the verify run) used the PRE-Mode7 binary, baking its garbage screens in as goldens.
Fix: `cmake --build . -j8` (all targets) before any --gen; regenerated — 6 distinct CRCs.
**Lesson:** regenerating goldens with a stale binary is self-licking — always full-rebuild before --gen, and eyeball that supposedly-different cases produce different CRCs (identical CRCs across distinct scenes = the tool didn't see your change).

### 2026-07-14 · SNES BG rendered one scanline low (scanline 0 is never displayed)
Symptom: kora.sfc famemu frame == snes9x frame rolled up 1px (33% pixel mismatch, dy=-1 → mean diff 1.1).
Cause: sppu.cpp fetch_bg_pixel sampled BG line y+VOFS for fb row y; hardware shows scanlines 1-224, so fb row y is scanline y+1.
Fix: sy = y + 1 + VOFS (sppu.cpp); OBJ unchanged — OAM "appears at Y+1" cancels in fb-row space.
**Lesson:** SNES fb row 0 = scanline 1; BG needs the +1, sprites don't. Same family as the NES OAM Y+1 entry below — every PPU has an off-by-one pact between BG and OBJ; verify with a cross-emulator pixel diff, not eyeballing.

### 2026-07-14 · Day/night subtract ignored the subscreen (BG2 cloud shadows missing)
Symptom: in-game kora.sfc frames 22% off vs snes9x — soft circles darker in snes9x, uniform elsewhere.
Cause: sppu.cpp color math always used COLDATA; KORA sets CGWSEL=$02 + TS=$02 (kora.s:237): subtract the SUBSCREEN (BG2 cloud blobs), fixed colour only where it's transparent.
Fix: BG2 layer + $2108/$210F/$2110/$212D/$2130 regs; resolve_screen(TS) as math operand, COLDATA fallback (no halving on fallback).
**Lesson:** "scoped-to-one-game" PPUs rot silently as the game grows — diff against a reference emulator through real GAMEPLAY input scripts, not just the title screen; kora/snes/verify_famemu.sh now does exactly that.

### 2026-07-14 · Lockstep cross-emulator diffs need a boot-length offset
Symptom: verify_famemu.sh cloud/ripple animations phase-shifted between famemu and snes9x on identical input scripts.
Cause: famemu's coarse timing finishes KORA's init ~4 host-frames before snes9x, so `frame`-driven animation (BG2HOFS drift kora.s:3010, ripple kora.s:338) disagrees at equal host frames.
Fix: famemu gets a 20-frame boot pad vs dump_ppm's 24 (kora/snes/verify_famemu.sh); animation granularity (1px per 8f) forgives the residual.
**Lesson:** frame-lockstep comparisons between emulators must align GAME time, not host time — pad boot until an animation-phase probe matches, and beware macOS `seq 1 0` emitting "1 0" (it counts down): use `for ((i=0;i<n;i++))` in harnesses.

### 2026-07-14 · PPU background rendered 1px left (reload/shift order)
Symptom: blargg sprite_hit 02.alignment FAILED #3 — sprite "hit" a bg tile it shouldn't touch; hit fired at x=127 for a tile at x=128.
Cause: engines/nes/ppu.cpp tick() ran bg_fetch_step (shifter reload) BEFORE shift_bg in the same dot, giving the fresh tile one extra shift.
Fix: shift THEN reload (sample → shift → reload per dot). ppu.cpp fetch_window block.
**Lesson:** in a shifter pipeline, reload must come after the current dot's shift — verify pipelines with an alignment test ROM, not by eyeballing game output.

### 2026-07-14 · Sprites drawn one scanline too high
Symptom: blargg 02.alignment FAILED #8 after the X fix; title star sprites offset vs Nestopia.
Cause: evaluate_sprites used row = line - OAM.y; hardware delays sprites one line (visible at y+1..y+h).
Fix: row = line - OAM.y - 1 (engines/nes/ppu.cpp evaluate_sprites).
**Lesson:** OAM Y+1 delay is easy to drop when the eval already runs "one line ahead" — the two offsets don't cancel.

### 2026-07-14 · All input silently dead through the libretro loaders (Nestopia)
Symptom: no game responded to famemu/famitv input via Nestopia; headless START scripts left titles unchanged.
Cause: Nestopia keeps controller ports unconnected for ROMs outside its database (all homebrew), and #defines RETRO_DEVICE_AUTO == RETRO_DEVICE_JOYPAD, so connecting with plain JOYPAD re-runs the same failing auto-detect.
Fix: call retro_set_controller_port_device(port, (1u<<8)|RETRO_DEVICE_JOYPAD) (the SUBCLASS gamepad) after load_game — famemu/Sources/FamemuEngine/engine.cpp + tools/famitv_play.cpp.
**Lesson:** after wiring a libretro core, verify input end-to-end with a scripted button press + frame diff; "compiles and renders" says nothing about input.

### 2026-07-14 · Harness self-bug: dump_ppm args miscounted, script silently ignored
Symptom: an hour chasing "Nestopia ignores input" — every scripted run produced identical frames.
Cause: dump_ppm takes starts/hold/script at argv[5-7]; I passed an extra 0 so the script landed in argv[7]="0" (parsed as zero segments) and the real script in argv[8] was never read.
Fix: correct invocation `dump_ppm core rom frames out 0 0 "script"`; famemu_dump prints frames-run so an accidental 0-frame run is visible.
**Lesson:** when two emulators "disagree", first verify both actually RAN the scenario (frame counts in output) — a harness that silently no-ops reads as a target bug.

### 2026-07-13 · `famitv.sh` broken by the new famidec `FATAL_ERROR` (cross-file regression)
Symptom: once fix #5 made a missing `libhackrf` a `FATAL_ERROR`, `scripts/famitv.sh` aborted at `cmake` configure on any machine without libhackrf — even though `famitv_play` (all the launcher builds) doesn't need it.
Cause: `famitv.sh:24` ran plain `cmake ... -DCMAKE_BUILD_TYPE=Release` with no `-DFAMITV_TOOLS_ONLY=ON`, so the new default-fatal famidec gate fired; the per-file fix verifiers each saw only one file and couldn't catch the interaction.
Fix: `famitv.sh:24` — pass `-DFAMITV_TOOLS_ONLY=ON` (the launcher only builds `famitv_play`, whose CMake gate is independent of that option). Verified: tools-only configure skips famidec benignly and `famitv_play` builds+links.
**Lesson:** a build gate made fatal-by-default must be reconciled with every script/CI that runs the plain configure — pattern above.

### 2026-07-13 · Tools report success with unwritten output
Symptom: `famitv_play --dump` / `famitv_loopback` exit 0 and print `saved=N` even when the output dir doesn't exist and every write failed.
Cause: `++saved` was unconditional after the write call; `write_bmp`'s `bool` return was dropped (loopback had its own `void`-returning duplicate); no dir creation; `--dump` with no arg silently fell back to a dir literally named `"0"`.
Fix: `famitv_play.cpp:259,414-422,448-452`, `famitv_loopback.cpp:27,102` — `create_directories` before writing; `++saved` only on `write_bmp()==true`; `--dump` with no arg now errors (exit 2); loopback now calls the shared `util/bmp.hpp` instead of its local duplicate.
**Lesson:** pattern — output-writing tool must create its dir + check the write return value.

### 2026-07-13 · `--crt-beam 0` → NaN → UB in `pack()`
Symptom: `--crt-beam 0` could crash or emit garbage CRT frames.
Cause: `crt.hpp:48` `inv_sig2 = 1/(2*beam*beam)` is `+inf` at `beam=0`; at the scanline center (`d==0`) `-(d*d)*inf = NaN`, which survives `exp`/`std::clamp` (NaN compares always false) into a `float->uint32_t` cast — UB.
Fix: `crt.hpp:48` — floor via `const float beam_w = std::max(p.beam, 1e-3f);`, used only for `inv_sig2`; `p` itself untouched.
**Lesson:** pattern — clamp/floor CLI float knobs before use in `1/(2*x*x)`-shaped formulas.

### 2026-07-13 · `--scanlines >1` → negative float → `uint8_t` cast UB
Symptom: `--scanlines 1.5` (or any value `>1`) could produce UB pixel values.
Cause: the existing guard only blocked `<=0`; `keep = 1 - opt_scanlines` went negative, and `static_cast<uint8_t>(negative float)` is UB (not modulo-defined like `int->unsigned`).
Fix: `famitv_play.cpp:284` — `opt_scanlines = std::clamp(opt_scanlines, 0.0f, 1.0f);` right after arg parsing, before any use.
**Lesson:** pattern — clamp/floor CLI float knobs before use.

### 2026-07-13 · Modulator envelope amplitude went negative on saturated colors
Symptom: generic-path fully-saturated bright pixels (e.g. yellow ≈133 IRE) emitted a 180°-phase-inverted IQ sample instead of a clamped envelope.
Cause: `ntsc_modulator.hpp:75` `amp = 0.75 - ire*0.00625` was never floored; `ire>120` drives `amp<0`, flipping carrier phase (physical AM envelopes can't go negative).
Fix: `ntsc_modulator.hpp:75` — `if (amp < 0.0f) amp = 0.0f;` immediately after computing `amp`.
**Lesson:** physical AM envelopes must be clamped `>=0` at the point of computation, not assumed non-negative from the input range.

### 2026-07-13 · Per-sample `sin`/`cos` in the generic chroma path
Symptom: `composite_ire`'s generic chroma path did two trig calls per active sample (~252k/field) despite the file's own comment saying it switched to a rotating-phasor multiply specifically to avoid this.
Cause: only the carrier had been converted to a running phasor; the subcarrier used in the generic path still called `std::sin(theta)`/`std::cos(theta)` directly.
Fix: `ntsc_modulator.hpp:64-66,84,99-101,106,131-132` — added a resynced-per-field subcarrier phasor `sc`/`sub_step`, advanced once per sample alongside `carrier`; `composite_ire` now takes `sc` and reads `.imag()`/`.real()` instead of calling `sin`/`cos`.
**Lesson:** when a file already documents a phasor-multiply optimization for one signal, grep for every other `sin(theta)`/`cos(theta)` call sharing the same `theta` — a partial conversion is easy to miss.

### 2026-07-13 · `famidec` silently skipped instead of failing loudly
Symptom: CMake configure/build reported success with `famidec` (the primary app) never produced when `libhackrf`/SDL2 were missing.
Cause: the `if(HACKRF_FOUND AND SDL2_FOUND)` gate had only a `message(STATUS)` skip branch — no opt-in/opt-out distinction, so a missing dep was indistinguishable from an intentional tools-only build.
Fix: `CMakeLists.txt:12,77-94` — added `FAMITV_TOOLS_ONLY` option; missing deps now `FATAL_ERROR` (naming which dep) unless that option is explicitly set.
**Lesson:** pattern — making a required build dep optional must gate on an explicit opt-out, else `FATAL_ERROR`.

### 2026-07-13 · `sdl2_iface` could end up empty even when `SDL2_FOUND`
Symptom: an SDL2 package exporting a non-standard CONFIG target (e.g. `SDL2::SDL2-static`) could set `SDL2_FOUND=TRUE` but link nothing, failing later at compile/link with an opaque "SDL2/SDL.h not found".
Cause: `CMakeLists.txt:26` `if(TARGET SDL2::SDL2) elseif(TARGET PkgConfig::SDL2)` chain had no `else` branch.
Fix: `CMakeLists.txt:36-54` — added an `else()` fallback using `SDL2_LIBRARIES`/`SDL2_INCLUDE_DIRS`; warns and sets `SDL2_FOUND FALSE` if nothing usable, so downstream gates correctly skip instead of linking an empty interface.
**Lesson:** a target-detection `if/elseif` chain needs an `else` that fails loud, not one that silently falls through to an empty interface.

### 2026-07-13 · Core symbols null-derefed if missing beyond `load()`'s required set
Symptom: a libretro core missing `get_sysinfo`/audio/input setters/`unload_game`/`deinit` would null-deref deep in the run loop after only a warning at load time.
Cause: `Core::load()`'s success check omitted several function pointers that are called unconditionally later in the file.
Fix: `famitv_play.cpp:95-97` — extended `load()`'s `&&` chain to require all 13 symbols actually called elsewhere in the file.
**Lesson:** pattern — every symbol called unconditionally after `dlopen` must be in `load()`'s required set.

### 2026-07-13 · Black RGB `(0,0,0)` mapped to `$0D` (blacker-than-black) instead of `$0F`
Symptom: pure black pixels sat at 0.350V, below the file's own reference black of 0.518V — a real fidelity deviation.
Cause: `NesPalette::index_of` broke a 3-way nearest-match tie (indices 13/14/15, all RGB `(0,0,0)`) by picking the lowest array index (`0x0D`), not the safe reference black `0x0F`.
Fix: `nes_signal.hpp:58` — short-circuit: exact RGB `(0,0,0)` returns `0x0F` directly, before the nearest-match loop runs.
**Lesson:** nearest-neighbor tie-breaks on lookup tables need an explicit preferred-index override for known-important exact matches — "first index wins" is an accidental default, not a design choice.

---

## Update protocol

1. Fix the bug.
2. Append a 5-line entry under "Chronological log" (newest first).
3. If the lesson is new, add a "Patterns to scan for FIRST" bullet.
4. Commit the fix + the journal entry **together**. Same SHA.

Skip step 2 and the journal decays. Don't.
