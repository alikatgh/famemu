# famitv review — 2026-07-13 (local ultra/max, `/code-review ultra` fallback)

Scope: uncommitted "famitv" work in `famicom-rf-hackrf-decoder` — NES ROM →
`NtscModulator` → real `NtscDecoder` chain → screen. 7 source files, ~1200 lines
(new: `ntsc_modulator.hpp`, `nes_signal.hpp`, `crt.hpp`, `util/bmp.hpp`,
`tools/famitv_play.cpp`, `tools/famitv_loopback.cpp`, `scripts/famitv.sh`;
modified: `CMakeLists.txt`). Method: 8 scoped sonnet finders (10 protocol angles)
over a shared digest, then verify + gap-sweep against a full read in the main context.

The core DSP is sound: sample-rate/offset agreement, carrier sign convention,
and the modulator↔decoder line alignment (`kPostVsyncBlank == kActiveStartLine`,
counted from vsync-end) all check out. Findings cluster in **unvalidated CLI
knobs producing UB**, **tools reporting success when they wrote nothing**, and the
**CMake optionalization dropping a loud-failure invariant**.

## Refuted / not-a-bug (recorded so they aren't re-raised)
- **Live `c` color toggle is NOT dead.** `NtscDecoder` holds `const Config& cfg_`
  (a reference), and the toggle + decode run on the same thread synchronously, so
  the mutation is seen at decode time.
- **No 3-line vertical shift.** Modulator active video starts 13 lines after
  vsync-end (`kVsyncLines(3) + kPostVsyncBlank(13)` → abs line 16), and the decoder's
  `kActiveStartLine = 13` is "lines after vsync end" — they agree.

## Findings (most-severe first)

1. **Tools report success (exit 0) when they wrote zero BMPs.**
   `tools/famitv_loopback.cpp:169` and `tools/famitv_play.cpp:419` do `++saved`
   *unconditionally* after the write call; `write_bmp` failure is ignored
   (`util/bmp.hpp` returns `bool` — dropped; loopback's local copy returns `void`).
   Neither tool creates the output dir. Run `famitv_loopback loopback_out` (or
   `famitv_play … --dump out`) before the dir exists → every `fopen` fails, yet the
   program prints `saved=3/4` and returns 0. `--dump` with no arg is worse:
   `val()` falls back to the string `"0"`, so it "dumps" to a dir named `0`.
   Defeats the headless-verify workflow (~/.claude/CLAUDE.md §3). *Fix: check the
   write result, gate `++saved` on it, create the dir (or fail non-zero).*

2. **`--crt-beam 0` → divide-by-zero → NaN → UB.** `crt.hpp:48`
   `inv_sig2 = 1/(2*beam*beam)` is `+inf` when beam=0; at any pixel where the
   scanline phase `d==0`, `-(d*d)*inf = NaN`, which sails through `std::exp`,
   `std::clamp` (all NaN compares false → returned unclamped), and into
   `static_cast<uint32_t>(NaN*255+0.5)` — float→int of a non-representable value is
   UB (garbage/crash). `-ffast-math` (CMake) makes NaN handling even more undefined.
   *Fix: clamp/guard beam to a small positive minimum.*

3. **`--scanlines >1` → negative float → `uint8_t` cast UB.**
   `tools/famitv_play.cpp:384` The `if (opt_scanlines <= 0) return;` guard only
   blocks ≤0; with `--scanlines 1.5`, `keep = 1-1.5 = -0.5`, `(px&0xff)*keep` is
   negative, and `static_cast<uint8_t>(-100.0f)` is UB (negative float → unsigned is
   not modulo-defined, unlike int→unsigned). *Fix: clamp `opt_scanlines` to [0,1].*

4. **Modulator envelope amplitude never clamped to ≥0 → phase inversion on
   saturated colors.** `ntsc_modulator.hpp:68` `amp = 0.75 - ire*0.00625`. In the
   generic (`--generic`) path a fully-saturated bright pixel exceeds ~120 IRE (e.g.
   pure yellow ≈133 IRE), driving `amp` negative; `pack(100*amp*carrier)` then emits
   a 180°-inverted IQ sample instead of clamping the AM envelope at 0 (physical
   over-modulation). The default `real_nes` path caps at 100 IRE and is unaffected;
   the loopback scene happens to use ≤191-valued colors, so it doesn't trip today.
   *Fix: `amp = max(0, …)`.*

5. **`famidec` (the primary app) is now silently skipped instead of failing
   loudly.** `CMakeLists.txt` (the `if(HACKRF_FOUND AND SDL2_FOUND)` gate).
   Previously `pkg_check_modules(HACKRF REQUIRED …)` + `find_package(SDL2 REQUIRED)`
   hard-failed configure when a dep was missing. Now a machine/CI missing libhackrf
   or SDL2 gets a fully "successful" configure+build with `famidec` never produced —
   only a `message(STATUS …)` line, easily lost (and swallowed by the script's
   `>/dev/null`). Removed invariant. *Fix: keep a loud error unless an explicit
   `-DFAMITV_TOOLS_ONLY` opt-out is set.*

6. **`sdl2_iface` can end up empty even when `SDL2_FOUND`.** `CMakeLists.txt:26`
   The `if(TARGET SDL2::SDL2) … elseif(TARGET PkgConfig::SDL2)` has no else. A config
   package exporting e.g. `SDL2::SDL2-static` (or a CONFIG target with flat includes)
   sets `SDL2_FOUND=TRUE` but matches neither branch → `famidec`/`famitv_play` (gated
   only on `SDL2_FOUND`) link a no-op interface and fail at compile/link with
   "SDL2/SDL.h not found" / undefined `SDL_*`. Also the parent-include-dir fixup is
   applied only on the pkg-config branch, not the CONFIG branch.

7. **Core function pointers called without null-check beyond `load()`'s required
   set.** `tools/famitv_play.cpp:281,288,429` `Core::load()` returns success on
   `init && run && load_game && get_av && set_environment && set_video`, but
   `get_sysinfo`, the audio/input setters, and `unload_game`/`deinit` are invoked
   unconditionally (only a warning is printed if `dlsym` returned null). A core
   missing any of them null-derefs. (These are mandatory libretro exports, so it's
   defensive, but the warn-then-deref is a latent crash.)

8. **Per-sample `std::sin`/`std::cos` in the generic chroma path** —
   `ntsc_modulator.hpp:122` The class's own comment (lines 45–53) explains it
   switched carrier/subcarrier to a running-phasor multiply *specifically to avoid*
   two trig calls per ~166k samples — but `composite_ire`'s generic chroma still does
   `sin(theta)`+`cos(theta)` per active sample (~252k trig calls/field; loopback runs
   this for all 48 fields). *Fix: derive sin/cos from a maintained rotating unit
   vector like the carrier already does.*

9. **Duplicate `write_bmp`.** `tools/famitv_loopback.cpp:84` reimplements
   `src/util/bmp.hpp:15` nearly verbatim (~35 lines). `famitv_play.cpp` already
   `#include "util/bmp.hpp"` and calls the shared one. The copies have already
   diverged (`bool` vs `void` return, different fopen-fail diagnostic). *Fix: include
   and call the shared helper.*

10. **`video_cb` unaligned `reinterpret_cast` of a core-supplied row.**
    `tools/famitv_play.cpp:152` `reinterpret_cast<const uint32_t*>(src + y*pitch)[x]`
    (and `uint16_t*`) assumes 4/2-byte alignment of a `pitch`/`data` chosen by the
    dlopen'd core — misaligned load is UB (SIGBUS on strict-align targets) and
    violates strict aliasing. Works on x86/arm64 in practice. *Fix: `memcpy` the
    pixel, or assert alignment.*

11. **`famitv.sh` execs a possibly-absent binary after a "successful" build.**
    `scripts/famitv.sh:22` Only checks whether `build/famitv_play` is missing, then
    builds and unconditionally `exec`s it — but CMake creates that target only when
    `SDL2_FOUND AND EXISTS libretro.h`. Without the sibling nestopia checkout or SDL2,
    `cmake --build` exits 0 (target simply skipped), and `exec` fails with an opaque
    "No such file or directory". *Fix: re-check the binary exists after building and
    print the real cause.*

12. **Black RGB maps to `$0D` (blacker-than-black), a −11.6 IRE pedestal, in the
    default path.** `nes_signal.hpp:61` `index_of` breaks ties by first array index,
    so `(0,0,0)` → color 13 (`$0D`), not the canonical safe black `$0F`. `$0D` forces
    a constant 0.350 V (`color>12 → hi=lo`) versus the file's own reference black
    0.518 V, i.e. every black pixel sits below 0 IRE. Likely floored back to black by
    the decoder (so often invisible), but it's a real fidelity deviation from the
    stated calibration. *Fix: prefer `$0F` for pure black (bias the tie-break, or map
    black explicitly).*

13. **`ftell()` == −1 not checked → `vector(SIZE_MAX)` throw + fd leak.**
    `tools/famitv_play.cpp:302` If seek/tell fails on `rom_path` (FIFO, device, IO
    error), `sz=-1`, `static_cast<size_t>(sz)=SIZE_MAX`, and `std::vector<uint8_t>
    rom(SIZE_MAX)` throws uncaught `length_error`/`bad_alloc` (abort), with `fp`
    unclosed. *Fix: check `sz >= 0` and error out cleanly.*

14. **`build_glow` comment claims a brightness threshold that doesn't exist.**
    `crt.hpp:127` "Downsample (box) with a brightness threshold so only lit areas
    glow" — the loop is a plain 4×4 box average with no threshold, so dark/mid areas
    bloom as much as highlights. The file states the math will be ported "identical"
    to a Metal shader, so a shader author trusting the comment would add a cutoff the
    CPU reference lacks. (Also `crt.hpp:17`'s pipeline comment lists "vignette →
    brightness/gamma" but brightness is applied *before* halation/vignette.)
    *Fix: implement the threshold or correct both comments.*

15. **`audio_sample_cb` has no latency cap** unlike `audio_batch_cb`.
    `tools/famitv_play.cpp:189` The batch path bounds the SDL queue to ~0.2 s; the
    single-sample path queues unconditionally. A core driving audio via the
    single-sample callback would grow the queue without bound if render outruns
    playback. (Nestopia uses the batch callback, so latent.) *Fix: apply the same cap.*

## Lower-severity notes (not counted in the 15)
- `crt.hpp:126` `assign(n,0)` zero-fills gr_/gg_/gb_ each frame though every element
  is overwritten immediately — use `resize` to skip the redundant fill.
- `nes_signal.hpp` `index_of` is called per active sample (default path); a cache
  helps after warmup, but per-source-pixel precompute would avoid the ~2× oversample
  re-lookups.
- `famitv_play.cpp:259` `--real` is a no-op (`opt_real` already defaults true); only
  `--generic` changes behavior.
- `bmp.hpp:45` `fwrite`/`fclose` returns unchecked → a truncated write still returns
  `true` (the documented contract is only "false if it can't be opened").
- `famitv_play.cpp:315,436` early error-returns (`load_game`/`disp.init` fail) skip
  `core.deinit()`/`unload_game()` — leaks whatever the core allocated in `retro_init`
  (process exits anyway, so minor).
- The `-ffast-math` Release flag is pre-existing (unchanged by this diff) but now
  governs new NaN-prone CRT math (see #2).

## Fixes applied — 2026-07-13 (workflow)

| id | file | status | note |
|----|------|--------|------|
| #1 | `tools/famitv_play.cpp`, `tools/famitv_loopback.cpp` | fixed | Dir created via `create_directories`; `++saved` now gated on `write_bmp()` returning `true`; `--dump` with no arg errors (exit 2) instead of defaulting to dir `"0"`. |
| #2 | `src/dsp/crt.hpp` | fixed | Beam floored to `1e-3f` before `inv_sig2 = 1/(2*beam*beam)`, so `--crt-beam 0` can no longer produce `+inf`/NaN into the `float->uint32_t` cast. |
| #3 | `tools/famitv_play.cpp` | fixed | `opt_scanlines` clamped to `[0,1]` right after arg parsing, so `keep = 1-opt_scanlines` can no longer go negative into the `uint8_t` cast. |
| #4 | `src/dsp/ntsc_modulator.hpp` | fixed | `amp` floored to `0.0f` after computation, so saturated-color pixels can no longer drive a phase-inverted IQ sample. |
| #5 | `CMakeLists.txt` | fixed | Added `FAMITV_TOOLS_ONLY` option; missing `libhackrf`/SDL2 now `FATAL_ERROR` (names the missing dep) unless the option is explicitly set — loud-failure invariant restored. |
| #6 | `CMakeLists.txt` | fixed | Added `else()` fallback to the `sdl2_iface` target-detection chain; falls back to `SDL2_LIBRARIES`/`SDL2_INCLUDE_DIRS` or sets `SDL2_FOUND FALSE` so downstream gates skip instead of linking an empty interface. |
| #7 | `tools/famitv_play.cpp` | fixed | `Core::load()`'s success check extended to all 13 symbols actually called unconditionally elsewhere in the file. |
| #8 | `src/dsp/ntsc_modulator.hpp` | fixed | Generic chroma path now uses a resynced-per-field subcarrier phasor (`sc`/`sub_step`) instead of per-sample `sin`/`cos` calls. |
| #9 | `tools/famitv_loopback.cpp` | fixed | Local duplicate `write_bmp` (returned `void`) removed; now includes and calls the shared `util/bmp.hpp` (`bool`-returning) helper. |
| #10 | `tools/famitv_play.cpp` | fixed (not independently verified by workflow verifier) | `reinterpret_cast` pixel reads in `video_cb` replaced with `std::memcpy` into a local for all three pixel formats, avoiding unaligned-load UB. |
| #11 | `scripts/famitv.sh` | fixed (not independently verified by workflow verifier) | Post-build guard added: if `build/famitv_play` still isn't executable after the CMake build, prints the missing-dependency explanation and exits 1 instead of falling through to an opaque `exec` failure. |
| #12 | `src/dsp/nes_signal.hpp` | fixed | Exact RGB `(0,0,0)` now short-circuits to palette index `0x0F` before the nearest-match tie-break loop. |
| #13 | `tools/famitv_play.cpp` | fixed (not independently verified by workflow verifier) | `ftell()` return checked for `<0` before the `vector` allocation; errors out cleanly instead of risking a `SIZE_MAX` allocation. |
| #14 | `src/dsp/crt.hpp` | fixed (comment-only, not independently verified by workflow verifier) | Pipeline comment corrected to the actual mask→brightness→halation→vignette→gamma order; `build_glow` comment corrected to state it's an unconditional box average (no brightness threshold exists). |
| #15 | `tools/famitv_play.cpp` | fixed (not independently verified by workflow verifier) | `audio_sample_cb` now applies the same SDL queue latency cap (`SDL_GetQueuedAudioSize < g_audio_cap`) already used by `audio_batch_cb`. |
| note (assign) | `src/dsp/crt.hpp` | fixed | Redundant `assign(n,0)` zero-fill replaced with `resize(n)` since every element is unconditionally overwritten immediately after. |
| note (bmp) | `src/util/bmp.hpp` | fixed | `fwrite`/`fclose` return values now checked; a truncated write or failed close returns `false` instead of an unconditional `true`. |
| note (leak) | `tools/famitv_play.cpp` | fixed | `core.deinit()`/`unload_game()` now called before early returns on `load_game()`/`disp.init()` failure. |

No finding was marked `correct=false` by a verifier — nothing flagged needs-attention. Findings #10, #11, #13, #14, #15 and the three lower-severity notes had fixes applied per the fix stage but were not covered by an explicit verifier verdict in this workflow run; they read as correct against the fix-stage's own description but should get a first-time look if picked up again.

**Build/test outcome:** Configuring exactly as specified (no `-DFAMITV_TOOLS_ONLY=ON`) now correctly hard-fails via fix #5's new `FATAL_ERROR` (this machine has no `libhackrf`) — expected, and itself a confirmation the fix works. Reconfigured with `-DFAMITV_TOOLS_ONLY=ON`: all offline targets built clean (`fam_dsp`, `synth_ntsc`, `famitv_loopback`, `famitv_play`), header syntax checks passed, the `synth_ntsc` golden-decoder ctest passed (1/1, 1.26s), and `famitv_loopback` ran end-to-end (exit 0, `decoded frames=47 lines=12570 coasted=141 saved=3`, wrote `frame_00/01/02.bmp` ~922KB each) — confirming fix #1's dir-creation/write-check path actually produces output, not just avoids the old false-success failure mode.
