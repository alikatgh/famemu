# The engine C ABI

Every Ember engine implements one narrow C interface —
[`engines/include/famemu_core.h`](../../engines/include/famemu_core.h) — so
the app, the analog-TV render chain, the store runtime, and the test
harnesses never care which console is running. Engines are static libraries
compiled into the host app (App Sandbox wants one self-contained process).

## Getting a core

```c
#include "famemu_core.h"

const FamemuCoreAPI* core16 = famemu_ember16_core();  /* Ember 16 */
const FamemuCoreAPI* core8  = famemu_ember8_core();   /* Ember 8  */
```

Each returns a vtable of function pointers; `system_id` is `"ember16"` /
`"ember8"`. The older `famemu_snes_core()` / `famemu_nes_core()` accessors
remain as ABI-stable aliases returning the same vtables.

## Lifecycle

```c
core->load_rom(bytes, len);     /* copies what it needs; you own bytes  */
core->reset();                  /* power-cycle                          */
while (running) {
    core->set_input(0, buttons);          /* FAMEMU_CORE_BTN_* bitmask */
    core->run_frame();                    /* exactly one video frame   */
    int w, h;
    const uint8_t* rgb = core->video_rgb(&w, &h);   /* RGB888, w*h*3   */
    n = core->audio_read(pcm, max);       /* s16 interleaved stereo    */
}
core->unload_rom();
```

Notes:

- `video_rgb` dimensions can change per frame — Ember 16 switches to
  512-wide output when a game enters hi-res. Always honor `w`/`h`.
- `audio_read` drains whatever the frame generated; `sample_rate()` is
  fixed per core (Ember 16: 32000 Hz).
- `set_input(port, ...)`: port 0 is player 1. On Ember 16, ports 1–4 reach
  the multitap pads. Engine-specific extras (light gun, PAL query) are on
  the concrete C++ system classes, not the C ABI.

## Save states

```c
size_t n = core->state_size();
core->state_save(buf, n);       /* suspend */
core->state_load(buf, n);       /* resume  */
```

States are versioned with a magic; loading a stale-format state fails
cleanly (returns 0) rather than corrupting the machine. Store titles get
suspend/resume through this path — test it before submitting.

## Determinism contract

Given the same ROM, reset, and per-frame input sequence, engines produce
identical frames and audio on every platform. The entire store QA pipeline
(golden CRCs, lockstep diffs, input-script replays) is built on this — do
not add wall-clock or randomness to an engine. Anything time-like must come
through the API (e.g. the RTC on SPC7110-class carts is host-settable via
`Spc7110::set_rtc`, never `time()` inside the engine).
