// ember32/audio.hpp — Ember 32 audio: a voice mixer, REFERENCE MODEL.
//
// 32 PCM voices, each with a pitch (playback rate, linearly interpolated), an
// ADSR volume envelope, and stereo pan; mixed to a 48 kHz stereo buffer with
// hard-clipping. This is the correctness reference the fast mixer is checked
// against. ADPCM decode, the reverb/echo bus and the streamed CD channels are
// documented follow-ons (README) — they layer onto this same voice model.
#pragma once
#include <cstdint>

namespace ember32 {

struct Voice {
    const int16_t* sample = nullptr;   // PCM data
    int len = 0;
    bool loop = false; int loop_start = 0;
    double pos = 0.0, rate = 1.0;       // playback cursor + rate (pitch)
    float vol = 1.0f, pan = 0.5f;       // 0..1 gain, pan 0=L .. 1=R
    // ADSR, times in samples; sustain is a level 0..1
    int attack = 1, decay = 1; float sustain = 1.0f; int release = 1;

    // --- envelope state ---
    int phase = 0;                      // 0 idle, 1 A, 2 D, 3 S, 4 R
    float env = 0.0f;
    bool active = false;

    void keyOn()  { active = true; phase = 1; env = 0.0f; pos = 0.0; }
    void keyOff() { if (active) phase = 4; }

    float envStep() {
        switch (phase) {
            case 1: env += 1.0f / (attack > 0 ? attack : 1); if (env >= 1.0f) { env = 1.0f; phase = 2; } break;
            case 2: env -= (1.0f - sustain) / (decay > 0 ? decay : 1); if (env <= sustain) { env = sustain; phase = 3; } break;
            case 3: env = sustain; break;
            case 4: env -= sustain / (release > 0 ? release : 1); if (env <= 0.0f) { env = 0.0f; phase = 0; active = false; } break;
            default: env = 0.0f; break;
        }
        return env;
    }

    // one mono sample (pre-pan), advancing the cursor + envelope
    float next() {
        if (!active || !sample || len == 0) return 0.0f;
        int i = int(pos); float fr = float(pos - i);
        int j = i + 1;
        if (j >= len) j = loop ? loop_start : len - 1;
        float s = (sample[i] * (1 - fr) + sample[j] * fr) / 32768.0f;
        pos += rate;
        if (pos >= len) { if (loop) { while (pos >= len) pos -= (len - loop_start); } else { active = false; } }
        return s * vol * envStep();
    }
};

struct Audio {
    static const int SR = 48000;
    Voice voices[32];

    // Mix `frames` stereo frames into `out` (interleaved L,R int16).
    void mix(int16_t* out, int frames) {
        for (int f = 0; f < frames; f++) {
            float L = 0, R = 0;
            for (auto& v : voices) {
                if (!v.active) continue;
                float s = v.next();
                L += s * (1.0f - v.pan);
                R += s * v.pan;
            }
            out[f * 2 + 0] = clip(L);
            out[f * 2 + 1] = clip(R);
        }
    }

private:
    static int16_t clip(float x) {
        int v = int(x * 32767.0f);
        return int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
};

} // namespace ember32
