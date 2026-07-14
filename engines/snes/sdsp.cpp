// famemu SNES engine — S-DSP implementation. See sdsp.hpp.
#include "sdsp.hpp"

namespace famemu::snes {

// Canonical envelope rate table: counter periods for rates 0..31 (in 32 kHz
// samples). Rate 0 = never.
static constexpr int kRateTable[32] = {
    0,    2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160,
    128,  96,   80,   64,   48,   40,  32,  24,  20,  16,  12,  10,  8,
    6,    5,    4,    3,    2,    1,
};

// 4-tap Gaussian kernel with the hardware's table layout (512 entries,
// looked up fwd/rev around the fractional position). Values are computed
// from a windowed Gaussian normalized to the hardware's ~2048 DC gain —
// the curve, not a dump.
static const int16_t* gauss_table() {
    static int16_t g[512];
    static bool init = false;
    if (!init) {
        double raw[512];
        double peak = 0.0;
        for (int i = 0; i < 512; ++i) {
            const double x = (511 - i) / 511.0;           // 0 at the peak end
            const double window = 0.5 * (1.0 + __builtin_cos(3.14159265358979 * x));
            raw[i] = __builtin_exp(-4.5 * x * x) * window;
            if (raw[i] > peak) peak = raw[i];
        }
        // Normalize: at frac=0 the taps use g[255], g[511], g[256], g[0];
        // hardware sums those to slightly under 2048.
        const double dc = raw[255] + raw[511] + raw[256] + raw[0];
        for (int i = 0; i < 512; ++i)
            g[i] = static_cast<int16_t>(raw[i] * (2042.0 / dc) + 0.5);
        init = true;
    }
    return g;
}

void SDsp::write(uint8_t addr, uint8_t v) {
    addr &= 0x7F;
    const uint8_t old_kon = regs_[0x4C];
    regs_[addr] = v;
    if (addr == 0x4C) {  // KON
        for (int i = 0; i < 8; ++i)
            if (v & (1 << i)) {
                key_on(i);
                regs_[0x7C] &= static_cast<uint8_t>(~(1 << i));  // clear ENDX bit
            }
        regs_[0x4C] = old_kon;  // KON reads back as written momentarily; keep simple
    }
    if (addr == 0x5C) {  // KOF -> release
        for (int i = 0; i < 8; ++i)
            if (v & (1 << i) && voices_[i].active)
                voices_[i].phase = Voice::Release;
    }
    if (addr == 0x6C && (v & 0x80)) {  // FLG soft reset: silence everything
        for (int i = 0; i < 8; ++i) {
            voices_[i].active = false;
            voices_[i].phase = Voice::Off;
            voices_[i].env = 0;
        }
        regs_[0x5C] = 0xFF;
    }
    if (addr == 0x7C) regs_[0x7C] = 0;  // any ENDX write clears it
}

void SDsp::noise_step() {
    const int rate = regs_[0x6C] & 0x1F;
    if (rate == 0 || kRateTable[rate] == 0) return;
    if (++noise_counter_ >= kRateTable[rate]) {
        noise_counter_ = 0;
        const uint16_t fb = ((noise_lfsr_ ^ (noise_lfsr_ >> 1)) & 1);
        noise_lfsr_ = static_cast<uint16_t>((noise_lfsr_ >> 1) | (fb << 14));
    }
}

void SDsp::key_on(int vi) {
    Voice& vc = voices_[vi];
    const uint16_t dir = static_cast<uint16_t>(regs_[0x5D]) << 8;
    const uint8_t srcn = regs_[(vi << 4) | 0x04];
    const uint16_t entry = static_cast<uint16_t>(dir + srcn * 4);
    vc.brr_addr = static_cast<uint16_t>(aram_[entry] | (aram_[entry + 1] << 8));
    vc.brr_nibble = 0;
    vc.hist[0] = vc.hist[1] = 0;
    vc.decoded_valid = false;
    vc.frac = 0;
    vc.sample_pos = 0;
    vc.phase = Voice::Attack;
    vc.env = 0;
    vc.env_counter = 0;
    vc.active = true;
}

void SDsp::decode_block(Voice& vc) {
    const uint8_t hdr = aram_[vc.brr_addr];
    const int shift = hdr >> 4;
    const int filter = (hdr >> 2) & 3;
    for (int i = 0; i < 16; ++i) {
        uint8_t byte = aram_[(vc.brr_addr + 1 + (i >> 1)) & 0xFFFF];
        int nib = (i & 1) ? (byte & 0x0F) : (byte >> 4);
        if (nib >= 8) nib -= 16;
        int s;
        if (shift <= 12) s = (nib << shift) >> 1;
        else s = (nib < 0 ? -2048 : 0);  // invalid shift clamps
        const int p1 = vc.hist[0], p2 = vc.hist[1];
        switch (filter) {
            case 1: s += p1 - (p1 >> 4); break;
            case 2: s += (p1 << 1) - ((p1 * 3) >> 5) - p2 + (p2 >> 4); break;
            case 3: s += (p1 << 1) - ((p1 * 13) >> 6) - p2 + ((p2 * 3) >> 4); break;
            default: break;
        }
        if (s > 0x7FFF) s = 0x7FFF;
        if (s < -0x8000) s = -0x8000;
        s = static_cast<int16_t>(s << 1) >> 1;  // 15-bit wrap like hardware
        vc.decoded[i] = static_cast<int16_t>(s);
        vc.hist[1] = vc.hist[0];
        vc.hist[0] = static_cast<int16_t>(s);
    }
    vc.decoded_valid = true;
}

void SDsp::env_step(int vi) {
    Voice& vc = voices_[vi];
    const uint8_t adsr1 = regs_[(vi << 4) | 0x05];
    const uint8_t adsr2 = regs_[(vi << 4) | 0x06];
    auto ticked = [&](int rate) {
        if (rate == 0 || kRateTable[rate] == 0) return false;
        if (++vc.env_counter >= kRateTable[rate]) { vc.env_counter = 0; return true; }
        return false;
    };
    // GAIN mode (ADSR1 bit7 clear) — except Release, which always ramps down.
    if (!(adsr1 & 0x80) && vc.phase != Voice::Release && vc.phase != Voice::Off) {
        const uint8_t gain = regs_[(vi << 4) | 0x07];
        if (!(gain & 0x80)) {
            vc.env = (gain & 0x7F) << 4;               // direct
        } else {
            const int rate = gain & 0x1F;
            switch ((gain >> 5) & 3) {
                case 0:  // linear decrease
                    if (ticked(rate)) vc.env -= 32;
                    break;
                case 1:  // exponential decrease
                    if (ticked(rate)) vc.env -= ((vc.env - 1) >> 8) + 1;
                    break;
                case 2:  // linear increase
                    if (ticked(rate)) vc.env += 32;
                    break;
                default:  // bent increase: fast to 0x600, slow above
                    if (ticked(rate)) vc.env += (vc.env < 0x600) ? 32 : 8;
                    break;
            }
        }
        if (vc.env > 0x7FF) vc.env = 0x7FF;
        if (vc.env < 0) vc.env = 0;
        return;
    }
    switch (vc.phase) {
        case Voice::Attack: {
            const int rate = ((adsr1 & 0x0F) << 1) + 1;
            if (rate >= 31) { vc.env += 1024; }  // fast attack
            else if (ticked(rate)) vc.env += 32;
            if (vc.env >= 0x7FF) { vc.env = 0x7FF; vc.phase = Voice::Decay; }
            break;
        }
        case Voice::Decay: {
            const int sl = (adsr2 >> 5) & 7;
            const int target = (sl + 1) << 8;
            const int rate = ((adsr1 >> 4) & 7) * 2 + 16;
            if (ticked(rate)) vc.env -= ((vc.env - 1) >> 8) + 1;
            if (vc.env <= target) vc.phase = Voice::Sustain;
            break;
        }
        case Voice::Sustain: {
            const int rate = adsr2 & 0x1F;
            if (rate && ticked(rate)) vc.env -= ((vc.env - 1) >> 8) + 1;
            if (vc.env < 0) vc.env = 0;
            break;
        }
        case Voice::Release:
            vc.env -= 8;
            if (vc.env <= 0) { vc.env = 0; vc.phase = Voice::Off; vc.active = false; }
            break;
        default: break;
    }
    if (vc.env > 0x7FF) vc.env = 0x7FF;
    if (vc.env < 0) vc.env = 0;
}

int SDsp::voice_sample(int vi) {
    Voice& vc = voices_[vi];
    if (!vc.active) { voice_out_[vi] = 0; return 0; }
    if (!vc.decoded_valid) decode_block(vc);

    int sample;
    if (regs_[0x3D] & (1 << vi)) {
        // NON: this voice plays the noise LFSR (15-bit, sign-extended).
        sample = static_cast<int16_t>(noise_lfsr_ << 1) >> 1;
    } else {
        // 4-tap Gaussian: fwd/rev table lookups around the 8-bit fraction,
        // partial sums >>11 like hardware.
        const int16_t* g = gauss_table();
        const int off = (vc.frac >> 4) & 0xFF;
        int s = (g[255 - off] * vc.taps[0]) >> 11;
        s += (g[511 - off] * vc.taps[1]) >> 11;
        s += (g[256 + off] * vc.taps[2]) >> 11;
        s += (g[off] * vc.taps[3]) >> 11;
        if (s > 0x7FFF) s = 0x7FFF;
        if (s < -0x8000) s = -0x8000;
        sample = s;
    }

    // advance by pitch, with PMON modulation from the previous voice's out
    uint32_t pitch = static_cast<uint16_t>(
        regs_[(vi << 4) | 0x02] | ((regs_[(vi << 4) | 0x03] & 0x3F) << 8));
    if (vi > 0 && (regs_[0x2D] & (1 << vi))) {
        pitch = static_cast<uint32_t>(
            static_cast<int>(pitch) +
            (((voice_out_[vi - 1] >> 5) * static_cast<int>(pitch)) >> 10));
        if (static_cast<int>(pitch) < 0) pitch = 0;
        if (pitch > 0x3FFF) pitch = 0x3FFF;
    }
    vc.frac += pitch;
    while (vc.frac >= 0x1000) {
        vc.frac -= 0x1000;
        // slide the interpolation window by one source sample
        vc.taps[0] = vc.taps[1];
        vc.taps[1] = vc.taps[2];
        vc.taps[2] = vc.taps[3];
        vc.taps[3] = vc.decoded[vc.sample_pos];
        if (++vc.sample_pos >= 16) {
            vc.sample_pos = 0;
            const uint8_t hdr = aram_[vc.brr_addr];
            if (hdr & 0x01) {  // END
                regs_[0x7C] |= static_cast<uint8_t>(1 << vi);  // ENDX
                if (hdr & 0x02) {  // loop
                    const uint16_t dir = static_cast<uint16_t>(regs_[0x5D]) << 8;
                    const uint8_t srcn = regs_[(vi << 4) | 0x04];
                    const uint16_t entry = static_cast<uint16_t>(dir + srcn * 4);
                    vc.brr_addr = static_cast<uint16_t>(aram_[entry + 2] |
                                                        (aram_[entry + 3] << 8));
                } else {
                    vc.phase = Voice::Release;  // one-shot end
                    vc.brr_addr += 9;
                }
            } else {
                vc.brr_addr += 9;
            }
            vc.decoded_valid = false;
        }
    }
    if (!vc.decoded_valid) decode_block(vc);

    env_step(vi);
    const int out = (sample * vc.env) >> 11;
    voice_out_[vi] = static_cast<int16_t>(out);
    regs_[(vi << 4) | 0x08] = static_cast<uint8_t>(out >> 8);       // OUTX
    regs_[(vi << 4) | 0x09] = static_cast<uint8_t>(vc.env >> 4);    // ENVX
    return out;
}

void SDsp::render(int samples) {
    const uint8_t flg = regs_[0x6C];
    for (int n = 0; n < samples; ++n) {
        noise_step();
        int l = 0, r = 0, echo_l = 0, echo_r = 0;
        if (!(flg & 0x40)) {  // not muted
            for (int v = 0; v < 8; ++v) {
                const int s = voice_sample(v);
                if (!s && !voices_[v].active) continue;
                const int voll = static_cast<int8_t>(regs_[(v << 4) | 0x00]);
                const int volr = static_cast<int8_t>(regs_[(v << 4) | 0x01]);
                const int sl = (s * voll) >> 7;
                const int sr = (s * volr) >> 7;
                l += sl;
                r += sr;
                if (regs_[0x4D] & (1 << v)) { echo_l += sl; echo_r += sr; }
            }
        }
        // Echo: ring in ARAM at ESA<<8, length EDL*2048 bytes (4/sample).
        int out_l = (l * static_cast<int8_t>(regs_[0x0C])) >> 7;
        int out_r = (r * static_cast<int8_t>(regs_[0x1C])) >> 7;
        if (!(flg & 0x20)) {  // echo write enabled
            const int esa = regs_[0x6D] << 8;
            const int edl = (regs_[0x7D] & 0x0F);
            const int bytes = edl ? edl * 2048 : 4;
            // read echo head
            const int idx = esa + echo_pos_;
            int16_t eh_l = static_cast<int16_t>(aram_[idx & 0xFFFF] |
                                                (aram_[(idx + 1) & 0xFFFF] << 8));
            int16_t eh_r = static_cast<int16_t>(aram_[(idx + 2) & 0xFFFF] |
                                                (aram_[(idx + 3) & 0xFFFF] << 8));
            // FIR (KORA: tap0 only; all taps applied)
            fir_ring_[fir_idx_][0] = eh_l;
            fir_ring_[fir_idx_][1] = eh_r;
            int f_l = 0, f_r = 0;
            for (int t = 0; t < 8; ++t) {
                const int8_t c = static_cast<int8_t>(regs_[(t << 4) | 0x0F]);
                const int ri = (fir_idx_ - t) & 7;
                f_l += (fir_ring_[ri][0] * c) >> 6;
                f_r += (fir_ring_[ri][1] * c) >> 6;
            }
            fir_idx_ = (fir_idx_ + 1) & 7;
            out_l += (f_l * static_cast<int8_t>(regs_[0x2C])) >> 7;
            out_r += (f_r * static_cast<int8_t>(regs_[0x3C])) >> 7;
            // write echo tail: input + feedback
            int w_l = echo_l + ((f_l * static_cast<int8_t>(regs_[0x0D])) >> 7);
            int w_r = echo_r + ((f_r * static_cast<int8_t>(regs_[0x0D])) >> 7);
            if (w_l > 0x7FFF) w_l = 0x7FFF; if (w_l < -0x8000) w_l = -0x8000;
            if (w_r > 0x7FFF) w_r = 0x7FFF; if (w_r < -0x8000) w_r = -0x8000;
            aram_[idx & 0xFFFF] = static_cast<uint8_t>(w_l);
            aram_[(idx + 1) & 0xFFFF] = static_cast<uint8_t>(w_l >> 8);
            aram_[(idx + 2) & 0xFFFF] = static_cast<uint8_t>(w_r);
            aram_[(idx + 3) & 0xFFFF] = static_cast<uint8_t>(w_r >> 8);
            echo_pos_ = (echo_pos_ + 4) % bytes;
        }
        if (out_l > 0x7FFF) out_l = 0x7FFF; if (out_l < -0x8000) out_l = -0x8000;
        if (out_r > 0x7FFF) out_r = 0x7FFF; if (out_r < -0x8000) out_r = -0x8000;
        const int next = (wr_ + 1) % kBuf;
        if (next != rd_) {
            buf_[wr_ * 2] = static_cast<int16_t>(out_l);
            buf_[wr_ * 2 + 1] = static_cast<int16_t>(out_r);
            wr_ = next;
        }
    }
}

}  // namespace famemu::snes
