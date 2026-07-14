# famemu store — content policy

The store exists to fund the platform: the engines are free, the app is
free, curated games are paid. That only works if every title is clean.

## What the store accepts

1. **Original works only.** You must own or license every byte: code,
   graphics, music, samples, text. Ports of your own games from other
   platforms are fine.
2. **No third-party IP.** No characters, names, logos, level layouts,
   sprite rips, or "inspired" assets close enough to confuse. No historical
   console trademarks anywhere in the title, art, or description
   (see [HERITAGE.md](HERITAGE.md) for why we're strict).
3. **No dumped content.** Commercial ROMs, extracted assets, chip-ROM
   dumps, or copyrighted sound drivers are rejected outright — including
   inside "homages".
4. **Public-domain / licensed middleware is fine** with attribution in the
   submission manifest (e.g. a PD sound driver, a licensed font).

## Technical requirements

- Builds from source with `ca65`/`ld65` (source escrow with the store —
  it is not published, but we must be able to rebuild your image).
- Correct cartridge header, real checksum, declared chip usage.
- **Deterministic**: your submitted input scripts must reproduce your
  submitted golden frames on our CI, twice.
- **Suspend/resume**: save-state round-trip mid-gameplay must continue
  identically (players get suspend on phone calls; it must not corrupt).
- Runs to completion of the submitted script with no engine warnings
  (unimplemented-feature logs fail QA).
- Every engine subsystem is implemented; there is no "unsupported feature"
  carve-out. (The last gap, the SPC7110-class decompressor, is done and
  gated against the reference decoder.)

## Distribution and signing

Store titles ship as signed cartridge images; the app runs only store-signed
content plus the developer's own sideloaded builds in Developer Mode. The app
is a curated games platform — it does not browse, download, or execute
arbitrary ROMs, and nothing in the UI or listing may suggest it plays
historical commercial games.

**How it works (implemented).** The store signs each cartridge with an
Ed25519 key: a detached signature over the exact cartridge bytes, distributed
as a sidecar `<cart>.emsig`. The app bundles only the store's *public* key and
verifies with CryptoKit before a cartridge is allowed to run
(`CartridgeSecurity` in the app; `isValidSignature(sig, for: romBytes)`).
A cartridge whose signature is missing or invalid is a *sideload*:

- **Off (default, App Store builds):** only store-signed cartridges run. An
  unsigned cartridge is blocked with a clear message.
- **Developer Mode on (Settings, or a DEBUG build / `FAMEMU_DEV=1`):**
  unsigned cartridges you sideload also run — how you test your own game.

Signing is done offline with `famemu/scripts/ember-store-sign.py`
(`keygen` / `sign` / `verify` / `pubkey`). The private key is the store's
crown jewel — held offline, never committed; rotate by re-keying, re-embedding
the new public key, and re-signing the catalog. The signer (Python Ed25519)
and the verifier (CryptoKit Curve25519) are interoperable — checked by
signing the free game and verifying it from both sides.

## Revenue

Engines: free, forever, open source. Store split and pricing are set in
the publisher agreement (out of scope for this repo); the reference points
are Rocket Rush (bundled free) and KORA (paid flagship).
