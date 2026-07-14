# famemu — Platform Vision & Engine Roadmap

_Decision record. 2026-07-13. Extends [DISTRIBUTION.md](DISTRIBUTION.md)._

> **2026-07-14 update — the engines have names and docs.** The two cores are
> now branded **Ember 8** and **Ember 16** (historical console names are
> internal/heritage-only from here on — App Store + trademark posture), both
> are feature-complete and gated, and the developer-facing documentation
> lives in [docs/engines/](engines/README.md): specs, C ABI, developer
> guide, store content policy, and the clean-room/heritage statement.

## The vision

**famemu is an open-source retro platform, funded by content.**

```
famemu app (open source, MIT)
 ├── engines: clean-room NES core → clean-room SNES core → (more later)
 ├── the analog heart: real NTSC modulate→decode chain + CRT (already built)
 ├── bundled game: Rocket Rush (NES, ours, PD) — installs and plays instantly
 └── STORE: in-app storefront for original homebrew
      ├── KORA (SNES, ours) — first paid title, ~$5
      └── third-party developers sell their own games (curated, by request)
```

The code is the funnel; the catalog and community are the moat. Anyone can
fork the player — nobody can fork the storefront, the curation, or the games.

## Ground rules (non-negotiable)

1. **Every store title is original IP.** Sellers warrant they own everything
   (code, art, music). No Nintendo-derived content, no real-person likenesses,
   no third-party ROMs. One infringing title endangers the whole store —
   curation is a legal firewall, not just taste.
2. **App Store builds contain zero GPL** (see DISTRIBUTION.md). Clean-room
   engines only. The DMG build may keep GPL cores (Nestopia) during transition.
3. **Study behavior, never copy copyleft source.** Reference docs: NESdev /
   SNESdev wikis, test ROMs, our own hardware experience. GPL emulator source
   is off-limits for engine authors.

## Two channels, two payment rails

| | DMG (our site) | Mac App Store |
|---|---|---|
| Store payments | Stripe (~3%) — ours | Apple IAP (15–30%) mandatory for digital goods |
| Third-party payouts | Stripe Connect | IAP + our ledger |
| Engine licensing | Nestopia OK short-term | clean-room cores only |
| Review | notarization (automated) | full App Review (Guideline 4.7 allows retro emulators, 2024+) |

Same catalog on both; prices may differ to absorb Apple's cut. The DMG channel
ships first and is the store's home base.

## Marketplace duties (when the store opens)

- **Merchant of record**: taxes/VAT (Stripe Tax, or Paddle/Lemon Squeezy to
  outsource MoR), refunds, payouts (Stripe Connect), receipts.
- **Curation pipeline**: submission → IP warranty + provenance check →
  playtest → listing. Start invitation/request-only.
- **DRM stance**: signed receipts + account library; no hard DRM. ROMs are
  trivially copyable — the itch.io model (convenience + fair price + supporting
  the dev) is the realistic protection. Don't burn engineering on DRM.
- **Rev share**: decide before the first third-party title (industry norm 70/30
  to 88/12; we should undercut Steam to attract homebrew devs).

## Engine architecture — multi-core from day one

One narrow C ABI every engine implements; the Swift app and the analog chain
never know which system is running:

```c
// famemu_core.h — the contract (sketch)
typedef struct FamemuCoreAPI {
    const char* system_id;                 // "nes", "snes", ...
    bool  (*load_rom)(const uint8_t* data, size_t len);
    void  (*reset)(void);
    void  (*run_frame)(void);              // exactly one video frame
    // video: canonical RGB888 framebuffer + dims (NES 256×240, SNES 256×224)
    const uint8_t* (*video_rgb)(int* w, int* h);
    // audio: s16 interleaved stereo written since last call
    size_t (*audio_read)(int16_t* out, size_t max_frames);
    void  (*set_input)(int port, uint32_t buttons);   // shared button bitmask
    // save states (store titles need suspend/resume)
    size_t (*state_size)(void);
    bool  (*state_save)(uint8_t* buf, size_t len);
    bool  (*state_load)(const uint8_t* buf, size_t len);
} FamemuCoreAPI;
```

- Engines are **static libraries** compiled into the app (App Sandbox requires
  a single self-contained process — the dlopen'd-dylib design is DMG-only and
  retires with Nestopia).
- The analog chain is already system-agnostic: `NtscModulator` samples any RGB
  frame. NES uses the real-composite path (`nes_signal.hpp`); SNES uses the
  generic RGB→NTSC path — both get the authentic TV look for free.
- The libretro loader in `FamemuEngine/engine.cpp` stays during transition;
  clean-room cores slot in behind the same facade, then it's deleted.

## Engine 1: clean-room NES core

Scope is **homebrew-first**, not every-commercial-game-ever:

| Component | Target |
|---|---|
| CPU | 6502 (official opcodes → then unofficial), cycle-counted |
| PPU | scanline-accurate first, tighten to dot-accuracy where tests demand |
| APU | 2 pulse + triangle + noise + DMC |
| Mappers | NROM(0), UNROM(2), CNROM(3), MMC1(1), MMC3(4) — covers the vast majority of homebrew; Rocket Rush is NROM |
| Input | 2 standard pads |

**Verification gates (in order, each is a milestone):**
1. `nestest.nes` golden CPU log — instruction-exact trace match.
2. blargg's `instr_test-v5`, `cpu_timing_test`.
3. blargg's `ppu_vbl_nmi`, sprite-hit tests (sprite-0 hit matters: Rocket Rush
   uses the sprite-0 split-scroll technique).
4. blargg's `apu_test` subset.
5. **Rocket Rush plays identically vs Nestopia** — frame-dump diff via the
   existing `dump_ppm` harness (input-script replays, pixel-compare).
6. KORA's NES prototype (MMC3) boots and plays.

## Engine 2: clean-room SNES core (KORA's vehicle)

Scoped to **what KORA uses**, expanding later:

| Component | Target |
|---|---|
| CPU | 65816, LoROM mapping only |
| PPU | Mode 1 (BG1 4bpp + BG3 2bpp text + OBJ), color math (day/night, fades), palette CGRAM animation; Mode 7 later (the crossing scene) |
| APU | SPC700 + DSP: BRR samples, the IPL upload handshake, echo off initially |
| DMA | general DMA + vblank OAM/CGRAM patterns KORA uses; HDMA later |
| No coprocessors | no SA-1/SuperFX/DSP-1 — homebrew doesn't have them |

**Verification gates:** KORA's own `verify.sh` frame dumps become the golden
tests (title → prologue → world → practice screens → day/night → audio FFT via
`check_audio_snes`). Add public SNES test ROMs (peterlemon's, blargg SPC tests)
as accuracy backstops. **Milestone: KORA runs start-to-endless in famemu.**

## Store product shape (v1)

- **Store tab** in the app: catalog (cover art service already exists),
  purchase, download to library, play. Titles = ROM + metadata + cover +
  signed receipt.
- **Backend v1**: static catalog JSON + Stripe Checkout links + signed
  download URLs. No accounts until needed; receipts keyed to purchase email.
- **Submission v1**: a form + email. Human review. Contracts before code.

## Order of work

1. **NES core** → gates 1–5 → replaces Nestopia in the DMG build → MAS
   submission becomes possible (sandbox entitlements swap per DISTRIBUTION.md).
2. **Store v1** (DMG channel, Stripe) with Rocket Rush free + first-party demos.
3. **SNES core** to the KORA milestone → **KORA launches as the store's
   flagship paid title (~$5)**.
4. Third-party submissions open (curated).
5. MAS build with IAP mirror of the catalog.

The app stays MIT throughout. Engines are ours, clean-room, MIT. Games are
NOT open source — they're the products.
