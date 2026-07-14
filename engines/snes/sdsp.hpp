// famemu SNES engine — S-DSP: 8 BRR voices, ADSR + GAIN envelopes, noise
// (NON, 15-bit LFSR at the FLG rate), pitch modulation (PMON), 4-tap
// Gaussian interpolation (hardware table structure; kernel computed, not
// dumped), echo with 8-tap FIR, ENDX/OUTX/ENVX readback, 32 kHz stereo.
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
        noise_lfsr_ = 0x4000;
        noise_counter_ = 0;
        std::memset(voice_out_, 0, sizeof voice_out_);
    }

    uint8_t read(uint8_t addr) { return regs_[addr & 0x7F]; }
    void write(uint8_t addr, uint8_t v);

    void render(int samples);  // generate into the ring buffer

    template <class S>
    void serialize(S& s) {
        s.io(regs_); s.io(voices_); s.io(echo_pos_); s.io(fir_ring_); s.io(fir_idx_);
        s.io(noise_lfsr_); s.io(noise_counter_); s.io(voice_out_);
    }
    void post_load() { rd_ = wr_ = 0; }

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
        int16_t taps[4] = {0,0,0,0};  // sliding window for 4-tap interp
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
    uint16_t noise_lfsr_;
    int noise_counter_;
    int16_t voice_out_[8];        // post-envelope outputs, for PMON/OUTX

    void key_on(int v);
    void decode_block(Voice& vc);
    int voice_sample(int vi);
    void env_step(int vi);
    void noise_step();
};

}  // namespace famemu::snes
