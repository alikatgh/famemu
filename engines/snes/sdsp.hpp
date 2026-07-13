// famemu SNES engine — S-DSP: 8 BRR voices, ADSR/GAIN envelopes, echo with
// FIR (KORA uses tap 0 only, all 8 implemented), 32 kHz stereo output.
// Linear interpolation instead of the hardware's 4-tap Gaussian for now —
// audibly close for these synth instruments; noted for a fidelity pass.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::snes {

class SDsp {
public:
    static constexpr int kSampleRate = 32000;

    explicit SDsp(uint8_t* aram) : aram_(aram) { reset(); }

    void reset() {
        std::memset(regs_, 0, sizeof regs_);
        regs_[0x6C] = 0xE0;  // FLG: reset, mute, echo disabled
        std::memset(voices_, 0, sizeof voices_);
        rd_ = wr_ = 0;
        echo_pos_ = 0;
        std::memset(fir_ring_, 0, sizeof fir_ring_);
        fir_idx_ = 0;
    }

    uint8_t read(uint8_t addr) { return regs_[addr & 0x7F]; }
    void write(uint8_t addr, uint8_t v);

    void render(int samples);  // generate into the ring buffer

    size_t read_samples(int16_t* out, size_t max_frames) {
        size_t n = 0;
        while (n < max_frames && rd_ != wr_) {
            out[n * 2 + 0] = buf_[rd_ * 2];
            out[n * 2 + 1] = buf_[rd_ * 2 + 1];
            rd_ = (rd_ + 1) % kBuf;
            ++n;
        }
        return n;
    }

private:
    uint8_t* aram_;
    uint8_t regs_[0x80];

    struct Voice {
        bool active = false;
        uint16_t brr_addr = 0;      // current BRR block
        int brr_nibble = 0;         // 0..15 within block
        int16_t hist[2] = {0, 0};   // BRR filter history
        int16_t decoded[16] = {0};
        bool decoded_valid = false;
        uint32_t frac = 0;          // pitch accumulator (12.4-ish)
        int sample_pos = 0;         // 0..15 in decoded block
        // envelope
        enum Phase { Off, Attack, Decay, Sustain, Release } phase = Off;
        int env = 0;                // 0..0x7FF
        int env_counter = 0;
    };
    Voice voices_[8];

    static constexpr int kBuf = 32768;
    int16_t buf_[kBuf * 2];
    int rd_, wr_;

    int echo_pos_;
    int16_t fir_ring_[8][2];
    int fir_idx_;

    void key_on(int v);
    void decode_block(Voice& vc);
    int voice_sample(int vi);
    void env_step(int vi);
};

}  // namespace famemu::snes
