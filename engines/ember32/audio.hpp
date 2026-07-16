// ember32/audio.hpp — Ember 32 audio: a voice mixer, REFERENCE MODEL.
//
// 32 PCM voices, each with a pitch (playback rate, linearly interpolated), an
// ADSR volume envelope, and stereo pan; mixed to a 48 kHz stereo buffer with
// hard-clipping. This is the correctness reference the fast mixer is checked
// against. Layered on top of the voice model:
//   - IMA ADPCM decode (`ima_adpcm_decode`) — 4-bit compressed sample source.
//   - Streamed channels (`Stream`) — continuous PCM (music / CD-DA style), refillable.
//   - A feedback echo / reverb bus (`Echo`) — delay line + feedback + damping.
#pragma once
#include <cstdint>

namespace ember32 {

// ---- IMA/DVI ADPCM decode (public-domain algorithm) ------------------------
// Decodes `nbytes` of 4-bit nibbles (low nibble first) into `out` (nbytes*2
// samples), threading the predictor/step-index state so a stream can be decoded
// in blocks. Returns the sample count.
inline int ima_adpcm_decode(const uint8_t* in, int nbytes, int16_t* out,
                            int* predictor = nullptr, int* step_index = nullptr) {
    static const int STEP[89] = {
        7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
        130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
        1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
        5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
        27086,29794,32767 };
    static const int IDX[16] = { -1,-1,-1,-1,2,4,6,8, -1,-1,-1,-1,2,4,6,8 };
    int pred = predictor ? *predictor : 0;
    int idx  = step_index ? *step_index : 0;
    int n = 0;
    for (int b = 0; b < nbytes; b++) {
        for (int half = 0; half < 2; half++) {
            int nib  = half ? (in[b] >> 4) & 0xF : in[b] & 0xF;
            int step = STEP[idx];
            int diff = step >> 3;
            if (nib & 1) diff += step >> 2;
            if (nib & 2) diff += step >> 1;
            if (nib & 4) diff += step;
            if (nib & 8) diff = -diff;
            pred += diff;
            if (pred >  32767) pred =  32767;
            if (pred < -32768) pred = -32768;
            idx += IDX[nib];
            if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
            out[n++] = int16_t(pred);
        }
    }
    if (predictor) *predictor = pred;
    if (step_index) *step_index = idx;
    return n;
}

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

// A streamed channel: a continuous PCM buffer (music / CD-DA style), independent
// of the 32 note-voices, refillable on the fly. Mono or interleaved stereo.
struct Stream {
    const int16_t* data = nullptr;
    int len = 0;                        // frames
    double pos = 0.0, rate = 1.0;       // cursor + playback rate
    float vol = 1.0f, pan = 0.5f;       // pan used for mono streams
    bool loop = false, stereo = true, active = false;

    void play(const int16_t* d, int frames, bool st, bool lp) {
        data = d; len = frames; stereo = st; loop = lp; pos = 0.0; active = true;
    }
    void feed(const int16_t* d, int frames) { data = d; len = frames; pos = 0.0; active = true; }
    void stop() { active = false; }

    void next(float& L, float& R) {
        L = R = 0.0f;
        if (!active || !data || len == 0) return;
        int i = int(pos); float fr = float(pos - i);
        int j = i + 1; if (j >= len) j = loop ? 0 : len - 1;
        if (stereo) {
            L = (data[i*2+0] * (1 - fr) + data[j*2+0] * fr) / 32768.0f * vol;
            R = (data[i*2+1] * (1 - fr) + data[j*2+1] * fr) / 32768.0f * vol;
        } else {
            float s = (data[i] * (1 - fr) + data[j] * fr) / 32768.0f * vol;
            L = s * (1.0f - pan); R = s * pan;
        }
        pos += rate;
        if (pos >= len) { if (loop) { while (pos >= len) pos -= len; } else active = false; }
    }
};

// Feedback echo / reverb send bus (SNES-echo family): a stereo delay line with a
// feedback coefficient and an optional one-pole lowpass on the tail (damping).
struct Echo {
    static const int MAXD = 48000;      // up to 1.0 s of delay
    float bufL[MAXD] = {}, bufR[MAXD] = {};
    int len = 0;                        // delay in samples (0 = bypass)
    int wp = 0;
    float feedback = 0.0f;              // 0..~0.9 (>=1 self-oscillates)
    float wet = 0.0f;                   // wet mix added to the dry signal
    float damp = 0.0f;                  // 0=off .. 1=heavy lowpass on the feedback
    float zL = 0.0f, zR = 0.0f;         // lowpass state

    void set(int delay_samples, float fb, float wet_, float damp_ = 0.0f) {
        len = delay_samples > MAXD ? MAXD : (delay_samples < 0 ? 0 : delay_samples);
        feedback = fb; wet = wet_; damp = damp_;
    }
    void clear() { for (int i = 0; i < MAXD; i++) bufL[i] = bufR[i] = 0.0f; wp = 0; zL = zR = 0.0f; }

    // Process one stereo sample in place: dry passes through, wet echo added.
    void process(float& L, float& R) {
        if (len <= 0) return;
        int rp = wp - len; if (rp < 0) rp += MAXD;
        float dL = bufL[rp], dR = bufR[rp];
        if (damp > 0.0f) { zL += damp * (dL - zL); dL = zL; zR += damp * (dR - zR); dR = zR; }
        bufL[wp] = L + dL * feedback;
        bufR[wp] = R + dR * feedback;
        if (++wp >= MAXD) wp = 0;
        L += dL * wet; R += dR * wet;
    }
};

struct Audio {
    static const int SR = 48000;
    Voice voices[32];
    Stream streams[4];      // streamed channels (music / CD-DA)
    Echo echo;              // reverb/echo send bus (bypassed until set())

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
            for (auto& s : streams) {
                if (!s.active) continue;
                float sl, sr; s.next(sl, sr); L += sl; R += sr;
            }
            echo.process(L, R);         // bypassed when echo.len == 0 (default)
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
