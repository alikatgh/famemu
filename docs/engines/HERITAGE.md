# Heritage, clean-room policy, and verification

This page exists so the rest of the documentation doesn't have to name
historical platforms. It states our lineage honestly, once.

## Lineage

Ember 8 and Ember 16 implement the hardware behavior of the two classic
Nintendo consoles of the 8-bit and 16-bit eras (known in the community
documentation as the NES/Famicom and Super NES/Super Famicom). Their
register maps, timing, and cartridge formats follow the public
community documentation (nesdev / fullsnes and related references), so the
enormous existing body of homebrew knowledge, toolchains, and tutorials
applies directly to Ember development.

Those console names and marks belong to Nintendo. They appear on this page
for identification only ("nominative use") and must not appear in product
branding, App Store material, store listings, or game titles.

## Clean-room policy

- Every line of engine code is original, written from public documentation.
  No code from GPL or proprietary emulators is included or derived from.
- **No dumped chip ROMs.** The audio-CPU boot ROM is implemented as a
  behavioral protocol; the math-chip trig/inverse tables are computed from
  their mathematical definitions. Where a table's quantization differs
  from silicon by a couple of low bits, the file header says so.
- The one third-party algorithm included is the S-DD1-class decompressor,
  from Andreas Naive's analysis, which its author placed in the public
  domain — credited in `engines/snes/sdd1.hpp`.
- Reference emulators (snes9x) are used **only as test oracles**: our CI
  renders the same test cartridges in both and diffs frames. Nothing from
  them ships in the engines or the app.

## Verification methodology

Three layers, all in `ctest`:

1. **Community test cartridges** (CPU/APU correctness) run to golden CRCs.
2. **Lockstep differs**: our own feature test cartridges rendered by both
   our engine and the reference oracle, compared frame-by-frame at ≤7/255
   channel tolerance (currently 40 frames + 3 full-gameplay scenarios, all
   at 0.000%).
3. **Golden screens**: 66 CRC-pinned frames catch any behavioral drift,
   including features the oracle can't gate (mid-scanline raster splits,
   interlace fields, computed-table chip ops).

The gates are the documentation's warranty. A capability claimed in these
docs without a gate is a bug — file it against `docs/BUG_JOURNAL.md`
discipline.
