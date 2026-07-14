# The Ember Engines

**Ember 8** and **Ember 16** are famemu's two fantasy consoles: free,
open-source game engines with the exact constraints of classic 8-bit and
16-bit console hardware. You write a cartridge image with a standard 6502 /
65816 toolchain; the famemu app runs it on iPhone, iPad, and Mac; the famemu
store distributes it.

The pitch to developers, in one line: *PICO-8's model with real-hardware
authenticity — your game is a genuine cartridge image that could burn to
silicon, and the engine's behavior is verified pixel-for-pixel against
reference implementations of the original hardware class.*

## The model

- **The engines are free and open source** (zlib/MIT, this repository).
  Anyone can build games on them, embed them, port them.
- **The store is curated.** famemu ships with Rocket Rush (Ember 8) built
  in; KORA (Ember 16) and third-party titles are sold through the in-app
  store. Only original works are accepted — see
  [STORE_POLICY.md](STORE_POLICY.md).
- **The app is a games platform, not an emulator.** It runs only content
  signed by the store pipeline. That is the App Store posture: a console
  platform with its own catalog, like any other game engine runtime.

## Naming

"Ember 8" and "Ember 16" are the only public names. Do not use the names of
historical consoles in app copy, store listings, screenshots, or marketing.
Developer documentation refers to hardware lineage generically ("the classic
8-bit console class") except in [HERITAGE.md](HERITAGE.md), which maps our
terminology to the community's standard documentation once, in one place.
Code identifiers (`nes`, `snes` in `system_id`, file paths) are internal and
scheduled to migrate to `ember8`/`ember16` at the next ABI-breaking change.

## Documentation map

| Doc | What it covers |
|-----|----------------|
| [EMBER8.md](EMBER8.md) | Ember 8 specification: CPU, video, audio, cartridge format, limits |
| [EMBER16.md](EMBER16.md) | Ember 16 specification: full feature matrix including expansion chips |
| [API.md](API.md) | The C ABI every engine implements (`famemu_core.h`) |
| [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) | Toolchain, building a cartridge, testing, debugging |
| [STORE_POLICY.md](STORE_POLICY.md) | What the store accepts; content and legal requirements |
| [HERITAGE.md](HERITAGE.md) | Hardware lineage, clean-room policy, verification methodology |

## Public site

The marketing + status pages live in [`site/`](../../site/) (static, no build)
— `index.html` is the pitch, `state.html` is the public feature/verification
matrix. Intended at `ember.famemu.aulenor.com`. Keep `state.html` in sync
with the gates: only mark a capability verified if `ctest` proves it.

## Status

Both engines are complete and gated:

- **Ember 8**: 6502-family CPU, tile/sprite video (256×240), 5-channel
  audio, cartridge mappers 0/1/2/3/4 — verified by CPU test ROM golden
  screens and an analog-RF rendering chain.
- **Ember 16**: full 16-bit CPU + audio DSP, 8 video modes (rotation/
  scaling, true 512-wide hi-res, per-scanline raster effects), 8-channel
  sample-based audio, and the complete expansion-chip lineup (math, 3-D,
  decompression, and dual-CPU coprocessors) — verified by 40 lockstep
  frames against a reference implementation at 0.000% divergence plus 66
  golden screens, plus a decompressor conformance test. No remaining gaps —
  the last one (the SPC7110-class decompressor) is implemented and gated.

Every claim above is enforced by `ctest` in this repository — if it's in
the docs, there's a gate for it.
