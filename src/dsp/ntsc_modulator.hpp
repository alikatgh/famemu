#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "nes_signal.hpp"
#include "ntsc_decoder.hpp"  // shares kFsc / kLineRate so encode and decode agree

namespace famidec {

// Forward NTSC-J modulator: RGB frame -> int8 IQ (.cs8), the inverse of the
// decode chain. The video carrier is placed at -offset_hz in the IQ band, so
// the very same NCO mixer the live pipeline uses on HackRF input brings it to
// 0 Hz. This mirrors the signal model proven by tests/synth_ntsc.cpp, but
// samples an arbitrary frame for active video instead of fixed color bars.
// Non-interlaced 240p, matching the Famicom.
//
// Feeding this straight into NtscDecoder is a pure-software loopback of the
// whole analog TV chain: the composite Y/C crosstalk, dot crawl and chroma
// bleed you get out are the *real* artifacts of squeezing color video through
// one composite channel and band-splitting it back -- not a faked shader.
class NtscModulator {
public:
    static constexpr int kLinesPerField = 262;
    static constexpr int kVsyncLines = 3;
    static constexpr int kPostVsyncBlank = 13;  // == NtscDecoder::kActiveStartLine
    static constexpr int kActiveLines = 240;

    NtscModulator(double sample_rate, double offset_hz)
        : fs_(sample_rate),
          omega_sc_(2.0 * M_PI * NtscDecoder::kFsc / sample_rate),
          omega_c_(-2.0 * M_PI * offset_hz / sample_rate),
          line_us_(1e6 / NtscDecoder::kLineRate),
          samples_per_line_(sample_rate / NtscDecoder::kLineRate) {}

    // Append one field (262 lines) of interleaved int8 IQ (I,Q,I,Q,...) for
    // `rgb` (w*h*3 bytes, row-major, R,G,B order, 0..255). The subcarrier and
    // carrier phase run continuously across fields, like real hardware.
    void modulate_field(const uint8_t* rgb, int w, int h,
                        std::vector<uint8_t>& out) {
        const int64_t total =
            static_cast<int64_t>(samples_per_line_ * kLinesPerField);
        out.reserve(out.size() + static_cast<size_t>(total) * 2);
        // The subcarrier phase (theta) and the carrier are both fixed-frequency,
        // so per sample they advance by a constant — no need to recompute a fmod
        // and two double-precision trig calls for every one of ~166k samples.
        // Resync both to the EXACT continuous phase once per field (so there is
        // no long-term drift and the seam matches fmod(omega*n_, 2pi)), then
        // advance within the field by a cheap add / complex multiply. Likewise
        // line and us come from a running sample position, not a per-sample
        // integer divide + fmod. Purely a speedup: the emitted IQ is identical
        // to the old path up to sub-ULP rounding (which int8 packing absorbs).
        const double two_pi = 2.0 * M_PI;
        double theta = std::fmod(omega_sc_ * static_cast<double>(n_), two_pi);
        if (theta < 0.0) theta += two_pi;
        std::complex<double> carrier(std::cos(omega_c_ * static_cast<double>(n_)),
                                     std::sin(omega_c_ * static_cast<double>(n_)));
        const std::complex<double> carrier_step(std::cos(omega_c_),
                                                 std::sin(omega_c_));
        // Subcarrier phasor, kept in lockstep with theta above (same resync,
        // same per-sample advance) so composite_ire never has to call
        // sin/cos itself for the generic chroma path or burst.
        std::complex<double> sc(std::cos(theta), std::sin(theta));
        const std::complex<double> sub_step(std::cos(omega_sc_),
                                             std::sin(omega_sc_));
        const double inv_fs_us = 1e6 / fs_;
        int line = 0;
        double pos = 0.0;  // sample offset within the current line (== fmod(il, spl))
        for (int64_t il = 0; il < total; ++il) {
            double us = pos * inv_fs_us;
            float ire = composite_ire(line, us, theta, sc, rgb, w, h);
            // Negative modulation: sync tip = 100% carrier, white ~= 12.5%.
            float amp = 0.75f - ire * (0.625f / 100.0f);
            if (amp < 0.0f) amp = 0.0f;  // physical over-modulation clamp
            out.push_back(pack(100.0f * amp * static_cast<float>(carrier.real())));
            out.push_back(pack(100.0f * amp * static_cast<float>(carrier.imag())));
            ++n_;
            pos += 1.0;
            if (pos >= samples_per_line_) { pos -= samples_per_line_; ++line; }
            theta += omega_sc_;
            if (theta >= two_pi) theta -= two_pi;
            carrier *= carrier_step;
            sc *= sub_step;
        }
    }

    // Generate the REAL NES composite (from recovered color indices) instead
    // of a generic RGB->NTSC re-encode. Colors/artifacts emerge from the signal.
    void set_real_nes(bool on) { real_nes_ = on; }

private:
    static uint8_t pack(float x) {
        int v = static_cast<int>(
            std::lround(std::fmax(-127.0f, std::fmin(127.0f, x))));
        return static_cast<uint8_t>(static_cast<int8_t>(v));
    }

    float composite_ire(int line, double us, double theta,
                        const std::complex<double>& sc, const uint8_t* rgb,
                        int w, int h) const {
        if (line < kVsyncLines)  // broad vertical-sync pulses, serrated at EOL
            return (us > line_us_ - 4.7) ? 0.0f : -40.0f;
        if (us < 4.7) return -40.0f;                    // hsync
        if (us >= 5.3 && us < 7.8)                       // color burst, back porch
            return -20.0f * static_cast<float>(sc.imag());
        if (us < 9.4 || us >= 62.0) return 0.0f;         // porches / blanking
        int active_line = line - kVsyncLines - kPostVsyncBlank;
        if (active_line < 0 || active_line >= kActiveLines) return 0.0f;
        double hfrac = (us - 9.4) / 52.6;                // 0..1 across active video
        int col = static_cast<int>(hfrac * w);
        col = col < 0 ? 0 : (col >= w ? w - 1 : col);
        int row = (h == kActiveLines) ? active_line
                                      : active_line * h / kActiveLines;
        row = row < 0 ? 0 : (row >= h ? h - 1 : row);
        const uint8_t* px = rgb + (static_cast<size_t>(row) * w + col) * 3;
        if (real_nes_) {
            int idx = nes_pal_.index_of(px[0], px[1], px[2]);
            // -3 sub-phases (90 deg) aligns the NES color reference to our
            // generic colorburst; calibrated against known frames.
            int phase = (static_cast<int>(theta / (2.0 * M_PI) * 12.0) + 9) % 12;
            if (phase < 0) phase += 12;
            float sig = nes_ntsc_signal(idx, phase);
            // Map NES voltage (black 0.518 .. white 1.962) to IRE.
            return (sig - 0.518f) / (1.962f - 0.518f) * 100.0f;
        }
        float r = px[0] / 255.0f, g = px[1] / 255.0f, b = px[2] / 255.0f;
        float y = 0.299f * r + 0.587f * g + 0.114f * b;
        float u = 0.492f * (b - y);
        float v = 0.877f * (r - y);
        float chroma = u * static_cast<float>(sc.imag()) +
                       v * static_cast<float>(sc.real());
        return (y + chroma) * 100.0f;
    }

    double fs_, omega_sc_, omega_c_, line_us_, samples_per_line_;
    int64_t n_ = 0;  // continuous sample index (subcarrier + carrier phase)
    bool real_nes_ = false;
    NesPalette nes_pal_;
};

}  // namespace famidec
