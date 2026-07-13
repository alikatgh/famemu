# famemu — Distribution & Licensing Decisions

_Decision record. Last updated 2026-07-12._

## The product

**famemu** ("family emulator"): a macOS app that plays **open-source / homebrew /
public-domain** NES ROMs through an accurate analog-TV pipeline (real RF/NTSC
decode + a Metal CRT shader), with a polished library UI. It is **not** for
pirated commercial games — the App-Store-eligible framing is "runs the retro
games you legally own or that are freely distributable."

## Two distribution channels (they are NOT the same)

| | Notarized **DMG** | **Mac App Store** |
|---|---|---|
| Artifact | `.dmg` (direct download from our site) | `.pkg` via App Store Connect |
| Review | Apple **notarization** only (automated) | Full **App Review** (human) |
| Sandbox | Not required | **Required** (App Sandbox entitlement) |
| GPL code | **OK** (comply: offer source) | **Conflicts** — avoid (see below) |
| Subprocess/dylib design | OK | Not OK — must be one sandboxed process |

A DMG is **not** how you get onto the App Store. They are separate.

## The licensing wall

- **Nestopia (our current core) is GPLv2.** GPL is fine in a **notarized DMG**
  (we ship/offer source, keep notices). But GPL and the App Store's Terms of
  Service conflict (the VLC-on-iOS precedent): the store adds usage/DRM
  restrictions the GPL forbids. **=> Nestopia cannot ship on the Mac App Store.**
- **QuickNES / Nes_Snd_Emu** (blargg) are **LGPL** — more tolerable, but LGPL on
  the App Store is still legally debated (relinking requirement).
- Truly App-Store-clean requires a **permissive (MIT/BSD) or original** core.

## Decisions (chosen)

1. **Now: notarized DMG on our own device**, keeping the **Nestopia** core for
   maximum accuracy. GPL is fine here. This is the immediate target.
2. **Later: write our own NES engine** for the App Store path. It must be a
   **clean-room / permissively-licensed** implementation:
   - We may **study behavior** of Nestopia (GPL), QuickNES & Nes_Snd_Emu (LGPL)
     and other emulators, and reimplement — **but must NOT copy GPL source** into
     our engine (that would make famemu GPL and re-create the wall).
   - LGPL parts (e.g. blargg's audio) could be used **only** as properly-isolated
     dynamic libraries if we later decide LGPL is acceptable; safest is original
     code.
   - Goal: an accurate NES core + APU we own outright, so the App Store build has
     zero third-party-copyleft entanglement.
3. **CRT / NTSC shaders:** open-source CRT shaders (crt-royale, CRT-Guest, etc.)
   are mostly GPL — usable in the **DMG** build, but the **App Store** build needs
   **original Metal shaders** (which we're writing anyway).

## Signing & notarization (DMG build)

`famemu/scripts/make_dmg.sh` builds `famemu.app`, signs it, wraps it in a DMG,
and — when credentials are present — notarizes + staples it. The flow mirrors
the Circles pipeline (`CirclesMac/scripts/build-dmg.sh`).

**No creds → ad-hoc** (runs on this Mac; needs right-click → Open elsewhere;
cannot be notarized). This is the default `bash scripts/make_dmg.sh`.

**Full notarized, double-click-anywhere build:**

```bash
DEVELOPER_ID_APP="Developer ID Application: NAME (TEAMID)" \
NOTARY_PROFILE="AC_NOTARY" \
bash famemu/scripts/make_dmg.sh
```

Notary auth — pick ONE:
- `NOTARY_PROFILE` — a `notarytool` keychain profile, created once with
  `xcrun notarytool store-credentials AC_NOTARY --apple-id … --team-id … --password …`
  (or `--key AuthKey_*.p8 --key-id … --issuer …` to seed it from an API key).
- `NOTARY_KEY` + `NOTARY_KEY_ID` + `NOTARY_ISSUER` — App Store Connect API key
  (`.p8`) directly. This machine already has `AuthKey_MNT8D4TX7D.p8`; the
  **issuer UUID** comes from App Store Connect → Users and Access → Integrations.
- `NOTARY_APPLE_ID` + `NOTARY_TEAM_ID` + `NOTARY_PASSWORD` (app-specific pw).

The script **auto-detects** any `Developer ID Application` identity in the
keychain (explicit `DEVELOPER_ID_APP` overrides), signs the `dlopen`'d Nestopia
core first, then the app with the hardened runtime + `famemu.entitlements`
(which only sets `disable-library-validation`, needed so the core loads at
runtime), then signs the DMG container. It also accepts the same env-var names
as the other projects' `.notarize-env` (`APPLE_ID` /
`APPLE_APP_SPECIFIC_PASSWORD` / `APPLE_TEAM_ID`), so:

```bash
source /path/to/.notarize-env && bash famemu/scripts/make_dmg.sh
```

notarizes famemu with no extra flags.

> **Prerequisite (checked 2026-07-13): the cert isn't on this machine yet.**
> The identity behind the other notarized macOS apps (localhostexplorer,
> quenderin) is **`Developer ID Application: Albert Nikanorov (9M2B2P4KSA)`**,
> with notarization via the `APPLE_ID`/`APPLE_APP_SPECIFIC_PASSWORD`/
> `APPLE_TEAM_ID` trio from a `.notarize-env`. Those scripts ran on a different
> machine (their example path is under `/Users/s_avelova/…`). *This* Mac's
> keychain has only an *Apple Development* cert + the ASC API key
> `AuthKey_MNT8D4TX7D.p8` (a different team, `MNT8D4TX7D`) — the
> App-Store-via-API path — and no `.notarize-env`. So to emit a notarized famemu
> DMG here: import the `9M2B2P4KSA` Developer ID Application cert (+ private key)
> into this keychain and drop the `.notarize-env` beside it, **or** run
> `make_dmg.sh` on the machine/CI that already has both. **No licensing
> blocker** — GPL is fully compatible with a notarized DMG; the GPL/App-Store
> conflict above is store-only.

## Compliance checklist (DMG build)

- [ ] Keep Nestopia's GPL license text + our offer-of-source in the app's About.
- [ ] Bundle the Nestopia dylib **inside** `famemu.app`, code-signed.
- [x] Signing + **notarize** flow wired into `make_dmg.sh` (env-var gated).
- [ ] Provide a **Developer ID Application** cert to produce the notarized DMG.

## Compliance checklist (future App Store build)

- [ ] Own engine (no GPL code) + original shaders.
- [ ] App Sandbox entitlement; single self-contained process (no subprocess exec).
- [ ] Only ships/loads freely-distributable ROMs or user-provided files.
- [ ] Follows App Review Guideline 4.7 (retro emulators, updated 2024).
