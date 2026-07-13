/* famemu_core.h — the engine contract every famemu core implements.
 *
 * One narrow C ABI so the app, the analog TV chain, and the store never know
 * which system is running. Engines are static libraries compiled into the app
 * (App Sandbox requires a single self-contained process). See docs/PLATFORM.md.
 *
 * MIT — part of the famemu platform. Clean-room: engines implementing this
 * must not contain copyleft emulator source.
 */
#ifndef FAMEMU_CORE_H
#define FAMEMU_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared button bitmask, one uint32 per port. (Prefixed FAMEMU_CORE_ so this
 * header can coexist with famemu_engine.h's libretro-position FAMEMU_BTN_*.) */
enum FamemuCoreButton {
    FAMEMU_CORE_BTN_A      = 1u << 0,
    FAMEMU_CORE_BTN_B      = 1u << 1,
    FAMEMU_CORE_BTN_SELECT = 1u << 2,
    FAMEMU_CORE_BTN_START  = 1u << 3,
    FAMEMU_CORE_BTN_UP     = 1u << 4,
    FAMEMU_CORE_BTN_DOWN   = 1u << 5,
    FAMEMU_CORE_BTN_LEFT   = 1u << 6,
    FAMEMU_CORE_BTN_RIGHT  = 1u << 7,
    /* SNES extends: */
    FAMEMU_CORE_BTN_X      = 1u << 8,
    FAMEMU_CORE_BTN_Y      = 1u << 9,
    FAMEMU_CORE_BTN_L      = 1u << 10,
    FAMEMU_CORE_BTN_R      = 1u << 11,
};

typedef struct FamemuCoreAPI {
    const char* system_id; /* "nes", "snes", ... */

    /* ROM lifecycle. load_rom copies what it needs; caller owns `data`. */
    int  (*load_rom)(const uint8_t* data, size_t len);
    void (*unload_rom)(void);
    void (*reset)(void);

    /* Run exactly one video frame (to vblank). */
    void (*run_frame)(void);

    /* Video: canonical RGB888, tightly packed, w*h*3 bytes.
     * NES: 256x240. SNES: 256x224. Valid until the next run_frame. */
    const uint8_t* (*video_rgb)(int* w, int* h);

    /* Audio: drain s16 interleaved stereo frames generated since last call.
     * Returns frames written (<= max_frames). sample_rate() is fixed per core. */
    size_t (*audio_read)(int16_t* out, size_t max_frames);
    int    (*sample_rate)(void);

    void (*set_input)(int port, uint32_t buttons);

    /* Save states (store titles need suspend/resume). */
    size_t (*state_size)(void);
    int    (*state_save)(uint8_t* buf, size_t len);
    int    (*state_load)(const uint8_t* buf, size_t len);
} FamemuCoreAPI;

/* Each engine exports one accessor: */
const FamemuCoreAPI* famemu_nes_core(void);
const FamemuCoreAPI* famemu_snes_core(void);

#ifdef __cplusplus
}
#endif
#endif /* FAMEMU_CORE_H */
