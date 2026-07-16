// ember32/tools/audio_bringup.cpp — audio reference smoke test. Plays a C-major
// chord: three voices share one single-cycle wavetable, pitched by playback
// rate, each with an ADSR envelope and a stereo pan; mixed to 48 kHz stereo and
// written as a .wav. Key-off partway exercises the release stage.
//   c++ -std=c++17 -O2 -I.. tools/audio_bringup.cpp -o /tmp/e32a && /tmp/e32a out.wav
#include "../audio.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace ember32;

static void put32(FILE* f, uint32_t v){ std::fputc(v,f);std::fputc(v>>8,f);std::fputc(v>>16,f);std::fputc(v>>24,f); }
static void put16(FILE* f, uint16_t v){ std::fputc(v,f);std::fputc(v>>8,f); }

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "e32_audio.wav";
    static Audio au;

    // single-cycle wavetable: a sine plus two soft harmonics (a mellow "pluck")
    const int L = 512; static int16_t wave[L];
    for (int i = 0; i < L; i++) {
        double t = 2 * M_PI * i / L;
        double s = std::sin(t) + 0.35 * std::sin(2 * t) + 0.15 * std::sin(3 * t);
        wave[i] = int16_t(s / 1.5 * 26000);
    }

    // rate so that freq = SR * rate / L  →  rate = freq * L / SR
    auto rateFor = [&](double hz){ return hz * L / double(Audio::SR); };
    double notes[3] = {261.63, 329.63, 392.00};     // C4, E4, G4
    float  pans[3]  = {0.35f, 0.5f, 0.65f};
    for (int k = 0; k < 3; k++) {
        Voice& v = au.voices[k];
        v.sample = wave; v.len = L; v.loop = true; v.loop_start = 0;
        v.rate = rateFor(notes[k]); v.vol = 0.28f; v.pan = pans[k];
        v.attack = 480; v.decay = 6000; v.sustain = 0.55f; v.release = 12000;   // ~10/125/250 ms
        v.keyOn();
    }

    const int total = int(Audio::SR * 0.7);          // 0.7 s
    const int keyoff = int(Audio::SR * 0.42);
    std::vector<int16_t> buf(total * 2);
    au.mix(buf.data(), keyoff);                       // sustain
    for (int k = 0; k < 3; k++) au.voices[k].keyOff();
    au.mix(buf.data() + keyoff * 2, total - keyoff);  // release tail

    FILE* f = std::fopen(out, "wb");
    uint32_t dataBytes = total * 2 * 2;
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, Audio::SR); put32(f, Audio::SR * 2 * 2); put16(f, 2 * 2); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, dataBytes);
    std::fwrite(buf.data(), 2, buf.size(), f);
    std::fclose(f);
    std::printf("wrote %s — C-major chord, 3 voices, ADSR, %d stereo frames @ %d Hz\n",
                out, total, Audio::SR);
    return 0;
}
