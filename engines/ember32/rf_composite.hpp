// ember32/rf_composite.hpp — put an Ember 32 frame THROUGH a composite signal.
//
// The platform's signature is "the colours and artifacts emerge from the signal
// itself, not a filter" — so a frame is encoded to an NTSC composite waveform and
// decoded back. This is a self-contained reference of that round-trip: it uses
// the SAME RGB→YIQ→subcarrier encode as the shared modulator
// (`src/dsp/ntsc_modulator.hpp` — y, u=0.492(b−y), v=0.877(r−y), chroma =
// u·sinθ + v·cosθ) and a matched synchronous decode. The luma/chroma crosstalk
// this creates is the real thing: dot-crawl on luma edges, chroma dots, and the
// soft colour fringing of composite video. (Production shares the app's exact
// modulator + decoder; this stands alone so the engine repo needs no vDSP.)
#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include "compositor.hpp"

namespace ember32 {

// Encode `in` (W×H RGB) to a composite signal and decode it back into `out`.
inline void composite_ntsc(const RGB* in, RGB* out, int W, int H) {
    const int S = 4;                          // samples per pixel
    const float CPP = 0.5f;                   // subcarrier cycles per pixel
    const float FINC = 2.0f * float(M_PI) * CPP / S;
    const int P = int(std::round(S / CPP));   // samples per subcarrier cycle
    std::vector<float> comp(W * S), luma(W * S), ci(W * S), cq(W * S);

    for (int y = 0; y < H; y++) {
        const RGB* row = in + y * W;
        float phase0 = y * float(M_PI);        // 180°/line → the classic dot-crawl

        // --- encode the row to a composite sample stream ---
        for (int x = 0; x < W; x++) {
            float r = row[x].r / 255.0f, g = row[x].g / 255.0f, b = row[x].b / 255.0f;
            float Y = 0.299f*r + 0.587f*g + 0.114f*b;
            float U = 0.492f*(b - Y), V = 0.877f*(r - Y);
            for (int s = 0; s < S; s++) {
                float th = phase0 + (x*S + s) * FINC;
                comp[x*S + s] = Y + U*std::sin(th) + V*std::cos(th);
            }
        }
        int N = W * S;
        // --- luma = box average over one subcarrier cycle (notches out chroma) ---
        for (int n = 0; n < N; n++) {
            float acc = 0; int c = 0;
            for (int k = -P/2; k <= P/2; k++) { int m = n+k; if (m>=0 && m<N) { acc += comp[m]; c++; } }
            luma[n] = acc / c;
        }
        // --- chroma = composite − luma, demodulated against the subcarrier ---
        for (int n = 0; n < N; n++) {
            float ch = comp[n] - luma[n];
            float th = phase0 + n * FINC;
            ci[n] = ch * std::sin(th); cq[n] = ch * std::cos(th);
        }
        // --- resolve one output pixel per column ---
        for (int x = 0; x < W; x++) {
            int c0 = x*S + S/2; float su = 0, sv = 0, sy = 0; int cu = 0;
            for (int k = -P/2; k <= P/2; k++) { int m = c0+k; if (m>=0 && m<N) { su += ci[m]; sv += cq[m]; cu++; } }
            for (int s = 0; s < S; s++) sy += luma[x*S + s];
            float Y = sy / S, U = 2*su/cu, V = 2*sv/cu;
            float r = Y + 1.140f*V, b = Y + 2.033f*U, g = (Y - 0.299f*r - 0.114f*b) / 0.587f;
            auto cl = [](float v){ int i = int(v*255.0f + 0.5f); return uint8_t(i<0?0:i>255?255:i); };
            out[y*W + x] = { cl(r), cl(g), cl(b) };
        }
    }
}

} // namespace ember32
