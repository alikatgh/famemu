#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "frame.hpp"

namespace famidec {

// CRT display simulation applied to a decoded Frame (640x480). This is the
// CPU reference implementation of the "screen" half of the accurate-TV look
// (the "signal" half is the RF/NTSC decode). It will be ported to a Metal
// fragment shader for the real-time app; the math is identical.
//
// Stages: barrel geometry -> bilinear resample -> scanline beam profile ->
// aperture-grille phosphor mask -> brightness -> halation/bloom (blurred
// bright areas added back) -> vignette -> gamma compensation.
struct CrtParams {
    float curvature = 0.10f;   // barrel distortion amount (0 = flat)
    float scanline = 0.42f;    // scanline gap darkening 0..1
    float mask = 0.30f;        // aperture-grille phosphor depth 0..1
    float glow = 0.40f;        // halation/bloom amount
    float brightness = 1.45f;  // gain to offset mask+scanline darkening
    float gamma = 1.08f;       // output gamma
    float vignette = 0.28f;    // corner darkening
    float beam = 0.34f;        // scanline beam width (smaller = tighter lines)
};

class Crt {
public:
    void apply(const Frame& src, Frame& dst, const CrtParams& p) {
        const int W = Frame::kWidth, H = Frame::kHeight;
        if (dst.rgba.size() != src.rgba.size()) dst.rgba.resize(src.rgba.size());

        r_.resize(static_cast<size_t>(W) * H);
        g_.resize(r_.size());
        b_.resize(r_.size());
        for (int i = 0; i < W * H; ++i) {
            uint32_t px = src.rgba[static_cast<size_t>(i)];
            r_[i] = srgb2lin((px & 0xff) / 255.0f);
            g_[i] = srgb2lin(((px >> 8) & 0xff) / 255.0f);
            b_[i] = srgb2lin(((px >> 16) & 0xff) / 255.0f);
        }
        build_glow(W, H);

        const float beam_w = std::max(p.beam, 1e-3f);
        const float inv_sig2 = 1.0f / (2.0f * beam_w * beam_w);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
                // Barrel geometry.
                float cx = u * 2.0f - 1.0f, cy = v * 2.0f - 1.0f;
                float wx = cx * (1.0f + p.curvature * cy * cy);
                float wy = cy * (1.0f + p.curvature * cx * cx);
                float su = wx * 0.5f + 0.5f, sv = wy * 0.5f + 0.5f;
                if (su < 0.0f || su > 1.0f || sv < 0.0f || sv > 1.0f) {
                    dst.rgba[static_cast<size_t>(y) * W + x] = 0xff000000u;
                    continue;
                }
                float r, g, b;
                sample_full(su * W - 0.5f, sv * H - 0.5f, r, g, b);

                // Scanline beam (240 lines across the height).
                float sl = sv * (H / 2.0f);
                float d = sl - std::floor(sl) - 0.5f;
                float beam = std::exp(-(d * d) * inv_sig2);
                float scan = (1.0f - p.scanline) + p.scanline * beam;
                r *= scan; g *= scan; b *= scan;

                // Aperture-grille phosphor mask (RGB stripes on output x).
                float md = 1.0f - p.mask;
                int m = x % 3;
                r *= (m == 0 ? 1.0f : md);
                g *= (m == 1 ? 1.0f : md);
                b *= (m == 2 ? 1.0f : md);

                // Brightness compensation.
                r *= p.brightness; g *= p.brightness; b *= p.brightness;

                // Halation / bloom: add blurred bright neighborhood.
                float gr, gg, gb;
                sample_glow(su, sv, gr, gg, gb);
                r += gr * p.glow; g += gg * p.glow; b += gb * p.glow;

                // Vignette.
                float vig = 1.0f - p.vignette * (cx * cx + cy * cy);
                if (vig < 0.0f) vig = 0.0f;
                r *= vig; g *= vig; b *= vig;

                dst.rgba[static_cast<size_t>(y) * W + x] =
                    0xff000000u | (pack(b, p.gamma) << 16) |
                    (pack(g, p.gamma) << 8) | pack(r, p.gamma);
            }
        }
    }

private:
    static float srgb2lin(float c) { return c * c; }  // cheap gamma ~2.0
    static uint32_t pack(float lin, float gamma) {
        float c = std::pow(std::clamp(lin, 0.0f, 1.0f), 1.0f / (2.0f * gamma));
        return static_cast<uint32_t>(c * 255.0f + 0.5f);
    }

    void sample_full(float fx, float fy, float& r, float& g, float& b) const {
        const int W = Frame::kWidth, H = Frame::kHeight;
        int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        float tx = fx - x0, ty = fy - y0;
        int x1 = std::min(x0 + 1, W - 1), y1 = std::min(y0 + 1, H - 1);
        x0 = std::clamp(x0, 0, W - 1); y0 = std::clamp(y0, 0, H - 1);
        auto at = [&](int x, int y, const std::vector<float>& c) {
            return c[static_cast<size_t>(y) * W + x];
        };
        auto lerp2 = [&](const std::vector<float>& c) {
            float a = at(x0, y0, c) * (1 - tx) + at(x1, y0, c) * tx;
            float d = at(x0, y1, c) * (1 - tx) + at(x1, y1, c) * tx;
            return a * (1 - ty) + d * ty;
        };
        r = lerp2(r_); g = lerp2(g_); b = lerp2(b_);
    }

    // Quarter-resolution blurred copy used for halation/bloom.
    void build_glow(int W, int H) {
        gw_ = W / 4; gh_ = H / 4;
        size_t n = static_cast<size_t>(gw_) * gh_;
        gr_.resize(n); gg_.resize(n); gb_.resize(n);
        // Downsample (box, 4x4 average) over the full frame.
        for (int y = 0; y < gh_; ++y)
            for (int x = 0; x < gw_; ++x) {
                float ar = 0, ag = 0, ab = 0;
                for (int dy = 0; dy < 4; ++dy)
                    for (int dx = 0; dx < 4; ++dx) {
                        int sx = x * 4 + dx, sy = y * 4 + dy;
                        size_t i = static_cast<size_t>(sy) * W + sx;
                        ar += r_[i]; ag += g_[i]; ab += b_[i];
                    }
                size_t gi = static_cast<size_t>(y) * gw_ + x;
                gr_[gi] = ar / 16.0f; gg_[gi] = ag / 16.0f; gb_[gi] = ab / 16.0f;
            }
        blur_sep(gr_); blur_sep(gg_); blur_sep(gb_);
    }

    void blur_sep(std::vector<float>& buf) {
        static const float k[5] = {0.06136f, 0.24477f, 0.38774f, 0.24477f, 0.06136f};
        tmp_.assign(buf.size(), 0.0f);
        // horizontal
        for (int y = 0; y < gh_; ++y)
            for (int x = 0; x < gw_; ++x) {
                float a = 0;
                for (int t = -2; t <= 2; ++t) {
                    int xx = std::clamp(x + t, 0, gw_ - 1);
                    a += buf[static_cast<size_t>(y) * gw_ + xx] * k[t + 2];
                }
                tmp_[static_cast<size_t>(y) * gw_ + x] = a;
            }
        // vertical (run several passes for a wider, softer glow)
        for (int pass = 0; pass < 3; ++pass) {
            for (int y = 0; y < gh_; ++y)
                for (int x = 0; x < gw_; ++x) {
                    float a = 0;
                    for (int t = -2; t <= 2; ++t) {
                        int yy = std::clamp(y + t, 0, gh_ - 1);
                        a += tmp_[static_cast<size_t>(yy) * gw_ + x] * k[t + 2];
                    }
                    buf[static_cast<size_t>(y) * gw_ + x] = a;
                }
            std::swap(buf, tmp_);
        }
        std::swap(buf, tmp_);
    }

    void sample_glow(float u, float v, float& r, float& g, float& b) const {
        float fx = u * gw_ - 0.5f, fy = v * gh_ - 0.5f;
        int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, gw_ - 1);
        int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, gh_ - 1);
        int x1 = std::min(x0 + 1, gw_ - 1), y1 = std::min(y0 + 1, gh_ - 1);
        float tx = std::clamp(fx - x0, 0.0f, 1.0f), ty = std::clamp(fy - y0, 0.0f, 1.0f);
        auto s = [&](const std::vector<float>& c) {
            float a = c[static_cast<size_t>(y0) * gw_ + x0] * (1 - tx) +
                      c[static_cast<size_t>(y0) * gw_ + x1] * tx;
            float d = c[static_cast<size_t>(y1) * gw_ + x0] * (1 - tx) +
                      c[static_cast<size_t>(y1) * gw_ + x1] * tx;
            return a * (1 - ty) + d * ty;
        };
        r = s(gr_); g = s(gg_); b = s(gb_);
    }

    std::vector<float> r_, g_, b_;
    std::vector<float> gr_, gg_, gb_, tmp_;
    int gw_ = 0, gh_ = 0;
};

}  // namespace famidec
