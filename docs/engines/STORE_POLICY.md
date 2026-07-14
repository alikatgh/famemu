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

Store titles ship as signed cartridge images; the app runs only
store-signed content plus the developer's own sideloaded builds in dev
mode. The app is a curated games platform — it does not browse, download,
or execute arbitrary ROMs, and nothing in the UI or listing may suggest it
plays historical commercial games.

## Revenue

Engines: free, forever, open source. Store split and pricing are set in
the publisher agreement (out of scope for this repo); the reference points
are Rocket Rush (bundled free) and KORA (paid flagship).
