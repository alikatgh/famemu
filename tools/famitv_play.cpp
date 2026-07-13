// Live player: NES ROM -> Nestopia libretro core -> NtscModulator ->
// NtscDecoder -> SdlDisplay. An emulated game is re-encoded as an NTSC-J RF
// signal and decoded by the very same analog chain the HackRF pipeline runs,
// so you're watching the game "through a real TV". Input comes from a Switch
// Pro Controller (or keyboard) via SDL; the core's audio plays via SDL.
//
// Build with CMake (preferred):
//   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
//   ./build/famitv_play <core.dylib> <rom.nes>
//
// Run:
//   ./build/famitv_play \
//     ../third_party/nestopia/libretro/nestopia_libretro.dylib  game.nes
//
// Headless verify (no window/input; dumps decoded BMP frames):
//   ./build/famitv_play <core.dylib> <rom.nes> --dump out_dir
//
// Keys: q/ESC quit, c color/gray.  Pad: dpad/stick move, A=A, B=B,
// Start, Select(Back).  Keyboard: arrows, X=A, Z=B, Enter=Start, RShift=Select.
#include <dlfcn.h>

#include <SDL2/SDL.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <complex>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "libretro.h"

#include "config.hpp"
#include "dsp/am_detector.hpp"
#include "dsp/dc_blocker.hpp"
#include "dsp/fir.hpp"
#include "dsp/frame.hpp"
#include "dsp/nco.hpp"
#include "dsp/crt.hpp"
#include "dsp/ntsc_decoder.hpp"
#include "dsp/ntsc_modulator.hpp"
#include "ui/sdl_display.hpp"
#include "util/bmp.hpp"

using namespace famidec;

namespace {

// ---- dynamically loaded libretro core ----
struct Core {
    void* handle = nullptr;
    void (*init)() = nullptr;
    void (*deinit)() = nullptr;
    void (*run)() = nullptr;
    bool (*load_game)(const retro_game_info*) = nullptr;
    void (*unload_game)() = nullptr;
    void (*get_av)(retro_system_av_info*) = nullptr;
    void (*get_sysinfo)(retro_system_info*) = nullptr;
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video)(retro_video_refresh_t) = nullptr;
    void (*set_audio_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_audio)(retro_audio_sample_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;

    template <class T>
    void sym(T& fn, const char* name) {
        fn = reinterpret_cast<T>(dlsym(handle, name));
        if (!fn) std::fprintf(stderr, "warning: missing symbol %s\n", name);
    }
    bool load(const char* path) {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
            return false;
        }
        sym(init, "retro_init");
        sym(deinit, "retro_deinit");
        sym(run, "retro_run");
        sym(load_game, "retro_load_game");
        sym(unload_game, "retro_unload_game");
        sym(get_av, "retro_get_system_av_info");
        sym(get_sysinfo, "retro_get_system_info");
        sym(set_environment, "retro_set_environment");
        sym(set_video, "retro_set_video_refresh");
        sym(set_audio_batch, "retro_set_audio_sample_batch");
        sym(set_audio, "retro_set_audio_sample");
        sym(set_input_poll, "retro_set_input_poll");
        sym(set_input_state, "retro_set_input_state");
        return init && run && load_game && get_av && set_environment &&
               set_video && get_sysinfo && set_audio_batch && set_audio &&
               set_input_poll && set_input_state && unload_game && deinit;
    }
};

// ---- host state (libretro callbacks are plain C function pointers) ----
struct Host {
    int fw = 0, fh = 0;
    retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
    std::vector<uint8_t> rgb;  // fw*fh*3, R,G,B
    bool have_frame = false;
    std::string system_dir;
    uint16_t buttons = 0;  // bit i == RETRO_DEVICE_ID_JOYPAD_i
};
Host g;

SDL_AudioDeviceID g_audio = 0;  // 0 = no audio device
Uint32 g_audio_cap = 0;         // max queued bytes before dropping (caps latency)

void core_log(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_WARN) return;  // only warnings/errors
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

bool env_cb(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool*>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            g.pixfmt = *static_cast<const retro_pixel_format*>(data);
            return true;  // accept whatever the core prefers
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char**>(data) = g.system_dir.c_str();
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback*>(data)->log = core_log;
            return true;
        default:
            return false;  // defaults for everything else
    }
}

void video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;  // duplicate-frame signal: keep the previous g.rgb
    g.fw = static_cast<int>(width);
    g.fh = static_cast<int>(height);
    g.rgb.resize(static_cast<size_t>(width) * height * 3);
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * pitch;
        uint8_t* dst = g.rgb.data() + static_cast<size_t>(y) * width * 3;
        for (unsigned x = 0; x < width; ++x) {
            uint8_t r, gg, b;
            if (g.pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
                uint32_t px;
                std::memcpy(&px, row + static_cast<size_t>(x) * sizeof(px), sizeof px);
                r = (px >> 16) & 0xff; gg = (px >> 8) & 0xff; b = px & 0xff;
            } else if (g.pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
                uint16_t px;
                std::memcpy(&px, row + static_cast<size_t>(x) * sizeof(px), sizeof px);
                r = static_cast<uint8_t>(((px >> 11) & 0x1f) << 3);
                gg = static_cast<uint8_t>(((px >> 5) & 0x3f) << 2);
                b = static_cast<uint8_t>((px & 0x1f) << 3);
            } else {  // 0RGB1555
                uint16_t px;
                std::memcpy(&px, row + static_cast<size_t>(x) * sizeof(px), sizeof px);
                r = static_cast<uint8_t>(((px >> 10) & 0x1f) << 3);
                gg = static_cast<uint8_t>(((px >> 5) & 0x1f) << 3);
                b = static_cast<uint8_t>((px & 0x1f) << 3);
            }
            dst[x * 3 + 0] = r; dst[x * 3 + 1] = gg; dst[x * 3 + 2] = b;
        }
    }
    g.have_frame = true;
}

void input_poll_cb() {}  // state is refreshed in the main loop
int16_t input_state_cb(unsigned port, unsigned device, unsigned, unsigned id) {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD || id >= 16) return 0;
    return (g.buttons >> id) & 1;
}

size_t audio_batch_cb(const int16_t* data, size_t frames) {
    if (g_audio && data) {
        // frames = stereo sample pairs; 2 int16 per pair.
        Uint32 bytes = static_cast<Uint32>(frames * 2 * sizeof(int16_t));
        if (SDL_GetQueuedAudioSize(g_audio) < g_audio_cap)  // bound latency
            SDL_QueueAudio(g_audio, data, bytes);
    }
    return frames;
}
void audio_sample_cb(int16_t l, int16_t r) {
    if (g_audio) {
        int16_t s[2] = {l, r};
        if (SDL_GetQueuedAudioSize(g_audio) < g_audio_cap)  // bound latency
            SDL_QueueAudio(g_audio, s, sizeof s);
    }
}

uint16_t read_input(SDL_GameController* c) {
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    auto k = [&](SDL_Scancode s) { return ks[s] != 0; };
    bool up = k(SDL_SCANCODE_UP), dn = k(SDL_SCANCODE_DOWN);
    bool lf = k(SDL_SCANCODE_LEFT), rt = k(SDL_SCANCODE_RIGHT);
    bool a = k(SDL_SCANCODE_X), b = k(SDL_SCANCODE_Z);
    bool start = k(SDL_SCANCODE_RETURN), select = k(SDL_SCANCODE_RSHIFT);
    if (c) {
        auto gb = [&](SDL_GameControllerButton bt) {
            return SDL_GameControllerGetButton(c, bt) != 0;
        };
        up |= gb(SDL_CONTROLLER_BUTTON_DPAD_UP);
        dn |= gb(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        lf |= gb(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        rt |= gb(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        a |= gb(SDL_CONTROLLER_BUTTON_A);
        b |= gb(SDL_CONTROLLER_BUTTON_B);
        start |= gb(SDL_CONTROLLER_BUTTON_START);
        select |= gb(SDL_CONTROLLER_BUTTON_BACK);
        int ax = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -8000) lf = true; else if (ax > 8000) rt = true;
        if (ay < -8000) up = true; else if (ay > 8000) dn = true;
    }
    uint16_t out = 0;
    auto s = [&](int id, bool on) { if (on) out |= (1u << id); };
    s(RETRO_DEVICE_ID_JOYPAD_UP, up);       s(RETRO_DEVICE_ID_JOYPAD_DOWN, dn);
    s(RETRO_DEVICE_ID_JOYPAD_LEFT, lf);     s(RETRO_DEVICE_ID_JOYPAD_RIGHT, rt);
    s(RETRO_DEVICE_ID_JOYPAD_A, a);         s(RETRO_DEVICE_ID_JOYPAD_B, b);
    s(RETRO_DEVICE_ID_JOYPAD_START, start); s(RETRO_DEVICE_ID_JOYPAD_SELECT, select);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <core.dylib> <rom.nes> [--dump <outdir>]\n",
                     argv[0]);
        return 2;
    }
    const char* core_path = argv[1];
    const char* rom_path = argv[2];
    // Optional flags after the ROM. --dump <outdir> is headless (no window /
    // input); the rest are "TV look" knobs.
    bool dump_mode = false;
    std::string dump_dir;
    float opt_noise = 0.0f;      // analog snow/grain (try 0.02)
    float opt_scanlines = 0.0f;  // CRT scanline darkening 0..1 (try 0.35)
    float opt_sat = 1.0f;        // color saturation
    float opt_hue = 0.0f;        // hue trim, degrees
    double opt_bw = 4.3e6;       // channel bandwidth Hz (lower = softer)
    bool opt_crt = false;        // full CRT display simulation
    bool opt_real = true;        // real NES composite signal (default; --generic disables)
    CrtParams crtp;              // CRT knobs (overridable below for look-dev)
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
        if (a == "--dump") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--dump requires an <outdir> argument\n");
                return 2;
            }
            dump_mode = true;
            dump_dir = argv[++i];
        }
        else if (a == "--noise") opt_noise = std::atof(val());
        else if (a == "--scanlines") opt_scanlines = std::atof(val());
        else if (a == "--sat") opt_sat = std::atof(val());
        else if (a == "--hue") opt_hue = std::atof(val());
        else if (a == "--bw") opt_bw = std::atof(val());
        else if (a == "--crt") opt_crt = true;
        else if (a == "--real") opt_real = true;
        else if (a == "--generic") opt_real = false;
        else if (a == "--crt-curve") crtp.curvature = std::atof(val());
        else if (a == "--crt-scan") crtp.scanline = std::atof(val());
        else if (a == "--crt-mask") crtp.mask = std::atof(val());
        else if (a == "--crt-glow") crtp.glow = std::atof(val());
        else if (a == "--crt-bright") crtp.brightness = std::atof(val());
        else if (a == "--crt-beam") crtp.beam = std::atof(val());
        else if (a == "--crt-vig") crtp.vignette = std::atof(val());
        else std::fprintf(stderr, "ignoring unknown arg: %s\n", a.c_str());
    }
    opt_scanlines = std::clamp(opt_scanlines, 0.0f, 1.0f);
    {  // NstDatabase.xml (optional) sits alongside the core
        std::string cp = core_path;
        auto slash = cp.find_last_of('/');
        g.system_dir = (slash == std::string::npos) ? "." : cp.substr(0, slash);
    }

    Core core;
    if (!core.load(core_path)) return 1;

    retro_system_info si;
    std::memset(&si, 0, sizeof si);
    core.get_sysinfo(&si);
    std::printf("core: %s %s\n", si.library_name ? si.library_name : "?",
                si.library_version ? si.library_version : "");

    core.set_environment(env_cb);
    core.init();
    core.set_video(video_cb);
    core.set_audio_batch(audio_batch_cb);
    core.set_audio(audio_sample_cb);
    core.set_input_poll(input_poll_cb);
    core.set_input_state(input_state_cb);

    // Load the ROM.
    std::FILE* fp = std::fopen(rom_path, "rb");
    if (!fp) {
        std::fprintf(stderr, "cannot open ROM %s\n", rom_path);
        return 1;
    }
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        std::fprintf(stderr, "ftell failed on ROM %s\n", rom_path);
        std::fclose(fp);
        return 1;
    }
    std::vector<uint8_t> rom(static_cast<size_t>(sz));
    if (std::fread(rom.data(), 1, rom.size(), fp) != rom.size()) {
        std::fprintf(stderr, "short read on ROM\n");
        std::fclose(fp);
        return 1;
    }
    std::fclose(fp);

    retro_game_info gi;
    std::memset(&gi, 0, sizeof gi);
    gi.path = rom_path;
    gi.data = rom.data();
    gi.size = rom.size();
    if (!core.load_game(&gi)) {
        std::fprintf(stderr, "core failed to load ROM\n");
        core.deinit();
        return 1;
    }

    retro_system_av_info av;
    std::memset(&av, 0, sizeof av);
    core.get_av(&av);
    std::printf("game: %ux%u @ %.3f fps  (pixfmt=%d)\n", av.geometry.base_width,
                av.geometry.base_height, av.timing.fps,
                static_cast<int>(g.pixfmt));

    // Analog TV chain (same as the live HackRF pipeline).
    Config cfg;
    cfg.sample_rate = 10e6;
    cfg.offset_hz = 2.0e6;
    cfg.mode = Config::Mode::Color;
    cfg.saturation = opt_sat;
    cfg.hue_deg = opt_hue;
    TripleBuffer tb;
    NtscDecoder dec(cfg, tb);
    NtscModulator mod(cfg.sample_rate, cfg.offset_hz);
    mod.set_real_nes(opt_real);
    DcBlocker dcb;
    Nco mixer(cfg.offset_hz, cfg.sample_rate);
    FirFilterC lpf(design_lowpass(opt_bw, cfg.sample_rate, 63));
    EnvelopeDetector env;
    Crt crt;

    std::vector<uint8_t> iq;
    std::vector<std::complex<float>> cbuf;
    std::vector<float> comp;

    // Advance one emulated frame, re-encode it as NTSC and push it through the
    // analog decode chain. Shared by the headless dump and the live loop.
    auto step = [&](uint16_t buttons) {
        g.buttons = buttons;
        core.run();  // -> video_cb fills g.rgb (and audio_*_cb queues sound)
        if (!g.have_frame || g.fw <= 0 || g.fh <= 0) return;
        iq.clear();
        mod.modulate_field(g.rgb.data(), g.fw, g.fh, iq);
        const size_t ns = iq.size() / 2;
        cbuf.resize(ns);
        comp.resize(ns);
        for (size_t i = 0; i < ns; ++i) {
            std::complex<float> c(static_cast<int8_t>(iq[2 * i]) / 128.0f,
                                  static_cast<int8_t>(iq[2 * i + 1]) / 128.0f);
            cbuf[i] = dcb.process(c) * mixer.next();
        }
        if (opt_noise > 0.0f) {  // channel noise -> analog snow
            static thread_local std::mt19937 rng(0x1234u);
            std::normal_distribution<float> nd(0.0f, opt_noise);
            for (size_t i = 0; i < ns; ++i)
                cbuf[i] += std::complex<float>(nd(rng), nd(rng));
        }
        lpf.process(cbuf.data(), cbuf.data(), ns);
        env.process(cbuf.data(), comp.data(), ns);
        dec.process(comp.data(), ns);
    };

    // Optional CRT scanlines: darken every other (doubled) output row.
    auto apply_scanlines = [&](Frame& fr) {
        if (opt_scanlines <= 0.0f) return;
        float keep = 1.0f - opt_scanlines;
        for (int y = 1; y < Frame::kHeight; y += 2) {
            uint32_t* row =
                fr.rgba.data() + static_cast<size_t>(y) * Frame::kWidth;
            for (int x = 0; x < Frame::kWidth; ++x) {
                uint32_t px = row[x];
                uint8_t r = static_cast<uint8_t>((px & 0xff) * keep);
                uint8_t gg = static_cast<uint8_t>(((px >> 8) & 0xff) * keep);
                uint8_t b = static_cast<uint8_t>(((px >> 16) & 0xff) * keep);
                row[x] = 0xff000000u | (b << 16) | (gg << 8) | r;
            }
        }
    };

    if (dump_mode) {
        std::error_code ec;
        std::filesystem::create_directories(dump_dir, ec);
        if (ec) {
            std::fprintf(stderr, "cannot create dump dir %s: %s\n",
                         dump_dir.c_str(), ec.message().c_str());
            core.unload_game();
            core.deinit();
            return 1;
        }
        const uint16_t START = 1u << RETRO_DEVICE_ID_JOYPAD_START;
        int saved = 0;
        uint64_t last_seq = 0;
        for (int frame = 0; frame < 520 && saved < 4; ++frame) {
            // Tap START a few times to walk past title / menus.
            uint16_t b = ((frame >= 60 && frame < 68) ||
                          (frame >= 150 && frame < 158) ||
                          (frame >= 240 && frame < 248))
                             ? START
                             : 0;
            step(b);
            const Frame* f = tb.acquire();
            if (f && f->seq != last_seq) {
                last_seq = f->seq;
                if (frame >= 400) {  // dump a few late, locked frames
                    char p[512];
                    std::snprintf(p, sizeof p, "%s/rom_%02d.bmp",
                                  dump_dir.c_str(), saved);
                    Frame out;
                    if (opt_crt) {
                        crt.apply(*f, out, crtp);
                    } else {
                        out = *f;
                        apply_scanlines(out);
                    }
                    if (write_bmp(out, p)) {
                        ++saved;
                    } else {
                        std::fprintf(stderr, "failed to write %s\n", p);
                    }
                }
            }
        }
        std::printf("dump: saved=%d frames=%llu lines=%llu coasted=%llu\n",
                    saved,
                    static_cast<unsigned long long>(dec.stats().frames.load()),
                    static_cast<unsigned long long>(dec.stats().lines.load()),
                    static_cast<unsigned long long>(
                        dec.stats().lines_coasted.load()));
        core.unload_game();
        core.deinit();
        return saved > 0 ? 0 : 1;
    }

    // ---- live window ----
    SdlDisplay disp;
    if (!disp.init("famitv - NES through the RF decoder")) {
        std::fprintf(stderr, "display init failed\n");
        core.unload_game();
        core.deinit();
        return 1;
    }
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) {
                std::printf("controller: %s\n", SDL_GameControllerName(pad));
                break;
            }
        }
    }
    if (!pad) std::printf("no game controller found; using keyboard\n");

    // Audio: play the core's PCM straight to the speakers (the RF chain is
    // video-only). SDL_QueueAudio keeps it simple; the queue is capped to
    // bound latency if the render loop briefly outruns playback.
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    int arate = av.timing.sample_rate > 0
                    ? static_cast<int>(av.timing.sample_rate)
                    : 48000;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = arate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio) {
        g_audio_cap =
            static_cast<Uint32>(have.freq) * 2 * sizeof(int16_t) / 5;  // ~0.2 s
        SDL_PauseAudioDevice(g_audio, 0);
        std::printf("audio: %d Hz stereo\n", have.freq);
    } else {
        std::fprintf(stderr, "audio unavailable: %s (muted)\n", SDL_GetError());
    }

    Frame shown;
    bool have_shown = false;
    uint64_t prev_frames = 0;
    for (bool running = true; running;) {
        KeyAction act = disp.poll();
        if (act == KeyAction::Quit) break;
        if (act == KeyAction::ToggleColor)
            cfg.mode = (cfg.mode == Config::Mode::Color) ? Config::Mode::Gray
                                                         : Config::Mode::Color;
        step(read_input(pad));

        const Frame* f = tb.acquire();
        if (f) {
            if (opt_crt) {
                crt.apply(*f, shown, crtp);
            } else {
                shown = *f;
                apply_scanlines(shown);
            }
            have_shown = true;
        }
        OsdStats st;
        st.line_locked = dec.stats().line_locked.load();
        st.burst_amp = dec.stats().burst_amp.load();
        st.frames = dec.stats().frames.load();
        st.vsync_locked = st.frames > prev_frames;
        prev_frames = st.frames;
        st.freq_mhz = 91.25;
        st.audio_mhz = 95.75;
        st.channel = 1;
        disp.render(have_shown ? &shown : nullptr, st);
    }

    if (g_audio) SDL_CloseAudioDevice(g_audio);
    if (pad) SDL_GameControllerClose(pad);
    core.unload_game();
    core.deinit();
    return 0;
}
