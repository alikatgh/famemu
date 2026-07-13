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

---

## Reusable tools

Add entries here for any reusable harness you build in `scripts/`. Format:
script name → one-line "what bug it was built to catch".

<!-- Examples:
- `scripts/render_dashboard.py` — drives the real `main.dashboard` view
  via `test_request_context + login_user`. Catches route-level NameError
  /BuildError that `render_template()` alone misses.
-->

---

## Chronological log

Newest first. Five lines max per entry. File:line citations beat prose.

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
