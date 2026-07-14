// famemu SNES engine — DSP-1/1B HLE (NEC uPD77C25 program behavior, from the
// public DSP-1 reference; Super Mario Kart, Pilotwings, ...). Clean-room:
// the chip's internal data-ROM tables are recomputed from their mathematical
// definitions, not dumped — exact for the rounded trig/inverse entries, so
// the arithmetic commands match reference emulators bit-for-bit; the
// projection pipeline (Parameter/Raster/Project/Target) implements the
// documented 3-D math and is gated by our own goldens.
//
// Port protocol: DR (data) at offsets below `boundary`, SR (status, bit 7 =
// ready) above. Commands stream 16-bit words LSB-first through DR.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class Dsp1 {
public:
    Dsp1() { reset(); }

    void reset() {
        state_ = State::Command;
        cmd_ = 0;
        in_n_ = in_need_ = out_n_ = out_pos_ = 0;
        first_byte_ = true;
        std::memset(&sh_, 0, sizeof sh_);
    }

    uint8_t read_dr() {
        if (state_ != State::Output) return 0xFF;
        uint8_t v;
        const uint16_t w = out_[out_pos_ >> 1];
        v = (out_pos_ & 1) ? static_cast<uint8_t>(w >> 8)
                           : static_cast<uint8_t>(w);
        ++out_pos_;
        if (out_pos_ >= out_n_ * 2) {
            // Raster re-arms for the next scanline; others go idle.
            if (cmd_ == 0x0A) { state_ = State::Input; in_n_ = 0; in_need_ = 1; }
            else state_ = State::Command;
        }
        return v;
    }

    uint8_t read_sr() const { return 0x80; }  // HLE: always ready

    void write_dr(uint8_t v) {
        if (state_ == State::Command) {
            cmd_ = v & 0x3F;
            in_n_ = 0;
            first_byte_ = true;
            in_need_ = input_words(cmd_);
            state_ = (in_need_ == 0) ? State::Output : State::Input;
            if (in_need_ == 0) execute();
            return;
        }
        if (state_ != State::Input) return;
        if (first_byte_) { lo_ = v; first_byte_ = false; return; }
        first_byte_ = true;
        in_[in_n_++] = static_cast<int16_t>(lo_ | (v << 8));
        if (in_n_ >= in_need_) execute();
    }

    template <class S>
    void serialize(S& s) {
        s.io(state_); s.io(cmd_); s.io(in_); s.io(out_);
        s.io(in_n_); s.io(in_need_); s.io(out_n_); s.io(out_pos_);
        s.io(first_byte_); s.io(lo_); s.io(sh_);
    }

private:
    enum class State : uint8_t { Command, Input, Output };
    State state_;
    uint8_t cmd_;
    int16_t in_[16];
    uint16_t out_[16];
    int in_n_, in_need_, out_n_, out_pos_;
    bool first_byte_;
    uint8_t lo_;

    // Cross-command state (attitude matrices A/B/C + projection).
    struct Shared {
        int16_t mat[3][3][3];      // [set][row][col]
        int16_t fx, fy, fz, lfe, les, aas, azs;   // Parameter inputs
        int16_t vof, vva, cx, cy;                 // Parameter outputs
        int16_t cent_x, cent_y, cent_z;           // projection center
        int16_t sin_aas, cos_aas, sin_azs, cos_azs;
        int32_t hdist;                             // eye-ground distance
    } sh_;

    // ---- computed data-ROM equivalents ------------------------------------
    static int16_t sin_t(int16_t a) {  // angle: 65536 = one revolution
        static int16_t t[1024];
        static bool init = false;
        if (!init) {
            for (int i = 0; i < 1024; ++i)
                t[i] = static_cast<int16_t>(
                    __builtin_floor(32767.0 *
                        __builtin_sin(2.0 * 3.14159265358979323846 * i / 1024.0) + 0.5));
            init = true;
        }
        return t[static_cast<uint16_t>(a) >> 6 & 0x3FF];
    }
    static int16_t cos_t(int16_t a) {
        return sin_t(static_cast<int16_t>(a + 0x4000));
    }

    static int16_t mul15(int16_t a, int16_t b) {   // (a*b)>>15, chip rounding
        return static_cast<int16_t>((static_cast<int32_t>(a) * b) >> 15);
    }

    // Normalized inverse: 1/(s*2^-15) as (mantissa, exponent).
    static void inverse(int16_t s, int16_t e, int16_t* om, int16_t* oe) {
        if (s == 0) { *om = 0x7FFF; *oe = 0x002F; return; }
        int32_t v = s;
        int neg = 0;
        if (v < 0) { v = -v; neg = 1; }
        int shift = 0;
        while (v < 0x4000) { v <<= 1; ++shift; }
        while (v >= 0x8000) { v >>= 1; --shift; }
        // v normalized to [0x4000, 0x8000): mantissa = (2^29)/v lands in
        // (0x4000, 0x8000], one halving max.
        int32_t m = static_cast<int32_t>((536870912.0 / v) + 0.5);
        int16_t oexp = static_cast<int16_t>(1 - e + shift);
        while (m >= 0x8000) { m >>= 1; ++oexp; }
        *om = static_cast<int16_t>(neg ? -m : m);
        *oe = oexp;
    }

    static int16_t denorm(int16_t m, int16_t e) {  // m * 2^e clamped
        int32_t v = m;
        if (e > 0) { for (int i = 0; i < e && i < 16; ++i) v <<= 1; }
        else { for (int i = 0; i < -e; ++i) v >>= 1; }
        if (v > 0x7FFF) v = 0x7FFF;
        if (v < -0x8000) v = -0x8000;
        return static_cast<int16_t>(v);
    }

    static uint16_t isqrt32(uint32_t x) {
        uint32_t r = 0, bit = 1u << 30;
        while (bit > x) bit >>= 2;
        while (bit) {
            if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
            else r >>= 1;
            bit >>= 2;
        }
        return static_cast<uint16_t>(r);
    }

    static int input_words(uint8_t cmd) {
        switch (cmd) {
            case 0x00: case 0x20: return 2;   // Multiply
            case 0x10: case 0x30: return 2;   // Inverse
            case 0x04: case 0x24: return 2;   // Triangle (sin/cos)
            case 0x08: case 0x28: return 3;   // Radius / Distance
            case 0x18: case 0x38: return 4;   // Range
            case 0x0C: case 0x2C: return 3;   // Rotate 2D
            case 0x1C: case 0x3C: return 6;   // Polar (3-axis rotate)
            case 0x01: case 0x11: case 0x21: return 4;   // Attitude A/B/C
            case 0x03: case 0x13: case 0x23: return 3;   // Subjective
            case 0x0D: case 0x1D: case 0x2D: return 3;   // Objective
            case 0x0B: case 0x1B: case 0x2B: return 3;   // Scalar
            case 0x14: case 0x34: return 6;   // Gyrate
            case 0x02: case 0x22: return 7;   // Parameter
            case 0x0A: case 0x2A: return 1;   // Raster (streaming)
            case 0x06: case 0x26: return 3;   // Project
            case 0x0E: case 0x2E: return 2;   // Target
            case 0x0F: case 0x2F: return 1;   // Memory test
            case 0x1F: case 0x3F: return 1;   // Memory dump
            default: return 1;
        }
    }

    int matrix_set() const {
        if (cmd_ & 0x20) return 2;
        return (cmd_ & 0x10) ? 1 : 0;
    }

    void execute();
};

inline void Dsp1::execute() {
    state_ = State::Output;
    out_pos_ = 0;
    out_n_ = 0;
    auto emit = [&](int32_t v) { out_[out_n_++] = static_cast<uint16_t>(v); };

    switch (cmd_) {
        case 0x00: case 0x20:  // Multiply: (a*b)>>15
            emit(mul15(in_[0], in_[1]));
            break;

        case 0x10: case 0x30: {  // Inverse
            int16_t m, e;
            inverse(in_[0], in_[1], &m, &e);
            emit(m);
            emit(e);
            break;
        }

        case 0x04: case 0x24:  // Triangle: sin & cos scaled by radius
            emit(mul15(sin_t(in_[0]), in_[1]));
            emit(mul15(cos_t(in_[0]), in_[1]));
            break;

        case 0x08: {  // Radius: X^2+Y^2+Z^2 (32-bit, doubled like the chip)
            const int32_t r =
                (static_cast<int32_t>(in_[0]) * in_[0] +
                 static_cast<int32_t>(in_[1]) * in_[1] +
                 static_cast<int32_t>(in_[2]) * in_[2]) << 1;
            emit(r & 0xFFFF);
            emit((r >> 16) & 0xFFFF);
            break;
        }

        case 0x18: case 0x38: {  // Range: X^2+Y^2+Z^2-R^2 (high word)
            const int32_t r =
                (static_cast<int32_t>(in_[0]) * in_[0] +
                 static_cast<int32_t>(in_[1]) * in_[1] +
                 static_cast<int32_t>(in_[2]) * in_[2] -
                 static_cast<int32_t>(in_[3]) * in_[3]) << 1;
            emit((r >> 16) & 0xFFFF);
            break;
        }

        case 0x28: {  // Distance: sqrt(X^2+Y^2+Z^2)
            const uint32_t r =
                static_cast<uint32_t>(
                    static_cast<int32_t>(in_[0]) * in_[0] +
                    static_cast<int32_t>(in_[1]) * in_[1] +
                    static_cast<int32_t>(in_[2]) * in_[2]);
            emit(isqrt32(r << 1));
            break;
        }

        case 0x0C: case 0x2C: {  // Rotate 2D: angle, X, Y -> X', Y'
            // Products sum in 32 bits before the >>15 (chip/snes9x rounding).
            const int16_t s = sin_t(in_[0]), c = cos_t(in_[0]);
            emit(static_cast<int16_t>(
                (static_cast<int32_t>(in_[1]) * c +
                 static_cast<int32_t>(in_[2]) * s) >> 15));
            emit(static_cast<int16_t>(
                (-static_cast<int32_t>(in_[1]) * s +
                 static_cast<int32_t>(in_[2]) * c) >> 15));
            break;
        }

        case 0x1C: case 0x3C: {  // Polar: rotate (X,Y,Z) by Az,Ay,Ax
            const int16_t az = in_[0], ay = in_[1], ax = in_[2];
            int32_t x = in_[3], y = in_[4], z = in_[5];
            int32_t t;
            // X axis
            t = mul15(static_cast<int16_t>(y), cos_t(ax)) -
                mul15(static_cast<int16_t>(z), sin_t(ax));
            z = mul15(static_cast<int16_t>(y), sin_t(ax)) +
                mul15(static_cast<int16_t>(z), cos_t(ax));
            y = t;
            // Y axis
            t = mul15(static_cast<int16_t>(x), cos_t(ay)) +
                mul15(static_cast<int16_t>(z), sin_t(ay));
            z = -mul15(static_cast<int16_t>(x), sin_t(ay)) +
                mul15(static_cast<int16_t>(z), cos_t(ay));
            x = t;
            // Z axis
            t = mul15(static_cast<int16_t>(x), cos_t(az)) +
                mul15(static_cast<int16_t>(y), sin_t(az));
            y = -mul15(static_cast<int16_t>(x), sin_t(az)) +
                mul15(static_cast<int16_t>(y), cos_t(az));
            x = t;
            emit(x);
            emit(y);
            emit(z);
            break;
        }

        case 0x01: case 0x11: case 0x21: {  // Attitude: build rotation matrix
            const int m = matrix_set();
            const int16_t scale = in_[0];
            const int16_t az = in_[1], ay = in_[2], ax = in_[3];
            const int16_t sz = mul15(sin_t(az), scale), cz = mul15(cos_t(az), scale);
            const int16_t sy = sin_t(ay), cy = cos_t(ay);
            const int16_t sx = sin_t(ax), cx = cos_t(ax);
            // Rz * Ry * Rx composite (chip order), rows scaled.
            sh_.mat[m][0][0] = mul15(cz, cy);
            sh_.mat[m][0][1] = static_cast<int16_t>(mul15(cz, mul15(sy, sx)) + mul15(sz, cx));
            sh_.mat[m][0][2] = static_cast<int16_t>(-mul15(cz, mul15(sy, cx)) + mul15(sz, sx));
            sh_.mat[m][1][0] = static_cast<int16_t>(-mul15(sz, cy));
            sh_.mat[m][1][1] = static_cast<int16_t>(-mul15(sz, mul15(sy, sx)) + mul15(cz, cx));
            sh_.mat[m][1][2] = static_cast<int16_t>(mul15(sz, mul15(sy, cx)) + mul15(cz, sx));
            sh_.mat[m][2][0] = mul15(sy, scale);
            sh_.mat[m][2][1] = static_cast<int16_t>(-mul15(cy, sx));
            sh_.mat[m][2][2] = mul15(cy, cx);
            break;                          // no output
        }

        case 0x03: case 0x13: case 0x23: {  // Subjective: M * v
            const int m = matrix_set();
            for (int r = 0; r < 3; ++r)
                emit(mul15(sh_.mat[m][r][0], in_[0]) +
                     mul15(sh_.mat[m][r][1], in_[1]) +
                     mul15(sh_.mat[m][r][2], in_[2]));
            break;
        }

        case 0x0D: case 0x1D: case 0x2D: {  // Objective: M^T * v
            const int m = matrix_set();
            for (int c = 0; c < 3; ++c)
                emit(mul15(sh_.mat[m][0][c], in_[0]) +
                     mul15(sh_.mat[m][1][c], in_[1]) +
                     mul15(sh_.mat[m][2][c], in_[2]));
            break;
        }

        case 0x0B: case 0x1B: case 0x2B: {  // Scalar: first row dot v
            const int m = matrix_set();
            emit(mul15(sh_.mat[m][0][0], in_[0]) +
                 mul15(sh_.mat[m][0][1], in_[1]) +
                 mul15(sh_.mat[m][0][2], in_[2]));
            break;
        }

        case 0x14: case 0x34: {  // Gyrate: integrate angular velocity
            // in: Az,Ax,Ay, dU,dF,dR -> out: new Az,Ax,Ay (documented approx)
            const int16_t az = in_[0], ax = in_[1], ay = in_[2];
            const int16_t du = in_[3], df = in_[4], dr = in_[5];
            int16_t csec, m, e;
            csec = cos_t(ax);
            inverse(csec, 0, &m, &e);
            // ESec correction then integration
            const int16_t sec_df = denorm(mul15(df, m), e);
            const int16_t sec_dr = denorm(mul15(dr, m), e);
            emit(static_cast<int16_t>(az +
                 mul15(sec_df, sin_t(ay)) + mul15(sec_dr, cos_t(ay))));
            emit(static_cast<int16_t>(ax +
                 mul15(df, cos_t(ay)) - mul15(dr, sin_t(ay))));
            emit(static_cast<int16_t>(ay + du +
                 mul15(mul15(sec_df, sin_t(ay)), sin_t(ax)) +
                 mul15(mul15(sec_dr, cos_t(ay)), sin_t(ax))));
            break;
        }

        case 0x02: case 0x22: {  // Parameter: set up the projection
            sh_.fx = in_[0]; sh_.fy = in_[1]; sh_.fz = in_[2];
            sh_.lfe = in_[3]; sh_.les = in_[4];
            sh_.aas = in_[5]; sh_.azs = in_[6];
            sh_.sin_aas = sin_t(sh_.aas); sh_.cos_aas = cos_t(sh_.aas);
            sh_.sin_azs = sin_t(sh_.azs); sh_.cos_azs = cos_t(sh_.azs);
            // Eye above ground along the view pitch.
            sh_.cent_x = static_cast<int16_t>(
                sh_.fx + mul15(mul15(sh_.lfe, sh_.sin_azs), sh_.sin_aas));
            sh_.cent_y = static_cast<int16_t>(
                sh_.fy - mul15(mul15(sh_.lfe, sh_.sin_azs), sh_.cos_aas));
            sh_.cent_z = static_cast<int16_t>(
                sh_.fz + mul15(sh_.lfe, sh_.cos_azs));
            sh_.hdist = sh_.cent_z;
            // Raster offset of the vanishing line & screen center.
            const int16_t tan_azs =
                (sh_.cos_azs == 0) ? 0x7FFF
                                   : static_cast<int16_t>(
                    (static_cast<int32_t>(sh_.sin_azs) << 15) /
                    (sh_.cos_azs ? sh_.cos_azs : 1));
            sh_.vof = static_cast<int16_t>(-mul15(sh_.les, tan_azs));
            sh_.vva = static_cast<int16_t>(sh_.les - sh_.vof);
            sh_.cx = sh_.fx;
            sh_.cy = sh_.fy;
            emit(sh_.vof);
            emit(sh_.vva);
            emit(sh_.cx);
            emit(sh_.cy);
            break;
        }

        case 0x0A: case 0x2A: {  // Raster: mode-7 matrix for scanline Vs
            const int16_t vs = in_[0];
            // Ground scale for this raster: hdist / (vs projected slope).
            int32_t denom = vs + sh_.vof + mul15(sh_.les, sh_.sin_azs);
            if (denom == 0) denom = 1;
            int32_t scale = (static_cast<int32_t>(sh_.hdist) << 8) / denom;
            if (scale > 0x7FFF) scale = 0x7FFF;
            if (scale < -0x8000) scale = -0x8000;
            const int16_t s = static_cast<int16_t>(scale);
            emit(mul15(s, sh_.cos_aas));                       // A
            emit(mul15(s, sh_.sin_aas));                       // B
            emit(-mul15(s, sh_.sin_aas));                      // C (=-B)
            emit(mul15(s, sh_.cos_aas));                       // D (=A)
            break;
        }

        case 0x06: case 0x26: {  // Project world (X,Y,Z) to screen (H,V,M)
            int32_t x = in_[0] - sh_.cent_x;
            int32_t y = in_[1] - sh_.cent_y;
            int32_t z = in_[2] - sh_.cent_z;
            // View-space transform (yaw then pitch).
            const int32_t vx = mul15(static_cast<int16_t>(x), sh_.cos_aas) +
                               mul15(static_cast<int16_t>(y), sh_.sin_aas);
            int32_t vy = -mul15(static_cast<int16_t>(x), sh_.sin_aas) +
                         mul15(static_cast<int16_t>(y), sh_.cos_aas);
            const int32_t vz = mul15(static_cast<int16_t>(vy), sh_.sin_azs) +
                               mul15(static_cast<int16_t>(z), sh_.cos_azs);
            vy = mul15(static_cast<int16_t>(vy), sh_.cos_azs) -
                 mul15(static_cast<int16_t>(z), sh_.sin_azs);
            int32_t depth = sh_.lfe - vz;
            if (depth <= 0) depth = 1;
            const int32_t h = (vx * sh_.les) / depth;
            const int32_t v = (vy * sh_.les) / depth;
            int32_t m = (static_cast<int32_t>(sh_.les) << 7) / depth;
            if (m > 0x7FFF) m = 0x7FFF;
            emit(h);
            emit(v);
            emit(m);
            break;
        }

        case 0x0E: case 0x2E: {  // Target: screen (H,V) -> ground (X,Y)
            const int16_t h = in_[0], v = in_[1];
            int32_t denom = v + sh_.vof + mul15(sh_.les, sh_.sin_azs);
            if (denom == 0) denom = 1;
            const int32_t scale = (static_cast<int32_t>(sh_.hdist) << 8) / denom;
            const int16_t gx = static_cast<int16_t>((h * scale) >> 8);
            const int16_t gy = static_cast<int16_t>((static_cast<int32_t>(sh_.les) * scale) >> 8);
            emit(sh_.cx + mul15(gx, sh_.cos_aas) - mul15(gy, sh_.sin_aas));
            emit(sh_.cy + mul15(gx, sh_.sin_aas) + mul15(gy, sh_.cos_aas));
            break;
        }

        case 0x0F: case 0x2F:  // Memory test: pass
            emit(0x0000);
            break;
        case 0x1F: case 0x3F:  // Memory dump: HLE has no program ROM
            emit(0x0000);
            break;

        default:
            emit(0x0000);
            break;
    }
    if (out_n_ == 0) state_ = State::Command;  // set-up commands: no output
}

}  // namespace famemu::snes
