// famemu NES engine — 2A03 APU. Pulse x2 (sweep/envelope/length), triangle
// (linear counter), noise (LFSR), DMC (delta playback), frame counter
// (4/5-step, IRQ). Nonlinear mixer per NESdev. Clean-room from public docs.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::nes {

class Apu {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr double kCpuHz = 1789772.7272;  // NTSC 2A03

    // The DMC fetches sample bytes over the CPU bus.
    using DmcReader = uint8_t (*)(void* ctx, uint16_t addr);

    Apu() { reset(); }

    void reset() {
        DmcReader fn = dmc_read_;  // survive the wipe
        void* ctx = dmc_ctx_;
        std::memset(this, 0, sizeof *this);  // POD state; safe: no vtable
        dmc_read_ = fn;
        dmc_ctx_ = ctx;
        noise_.lfsr = 1;
        // Hardware power-up: $4017 = $00 — 4-step mode, frame IRQ ENABLED.
        // (Games that never write $4017 also never CLI, so it stays masked.)
        frame_irq_inhibit_ = false;
        frame_div_ = 7457;
        sample_acc_ = 0.0;
    }

    void set_dmc_reader(DmcReader fn, void* ctx) { dmc_read_ = fn; dmc_ctx_ = ctx; }

    void write(uint16_t addr, uint8_t v);
    uint8_t read_status();          // $4015
    void tick(int cpu_cycles);      // advance; generates samples internally

    // Drain buffered stereo s16 frames (mono duplicated). Returns frames.
    size_t read_samples(int16_t* out, size_t max_frames) {
        size_t n = 0;
        while (n < max_frames && rd_ != wr_) {
            int16_t s = buf_[rd_];
            rd_ = (rd_ + 1) % kBufSize;
            out[n * 2 + 0] = s;
            out[n * 2 + 1] = s;
            ++n;
        }
        return n;
    }

    bool irq_pending() const { return frame_irq_ || dmc_irq_; }

private:
    // ---- channels -------------------------------------------------------
    struct Envelope {
        bool start, loop, constant;
        uint8_t volume, divider, decay;
        void clock() {
            if (start) { start = false; decay = 15; divider = volume; return; }
            if (divider == 0) {
                divider = volume;
                if (decay) --decay;
                else if (loop) decay = 15;
            } else --divider;
        }
        uint8_t out() const { return constant ? volume : decay; }
    };

    struct Pulse {
        bool enabled;
        uint8_t duty, seq;
        uint16_t timer, counter;
        uint8_t length;
        bool length_halt;
        Envelope env;
        // sweep
        bool sweep_enable, sweep_negate, sweep_reload, is_pulse2;
        uint8_t sweep_period, sweep_shift, sweep_divider;

        uint16_t sweep_target() const {
            uint16_t change = timer >> sweep_shift;
            if (sweep_negate) return timer - change - (is_pulse2 ? 0 : 1);
            return timer + change;
        }
        bool muted() const { return timer < 8 || sweep_target() > 0x7FF; }
        void clock_sweep() {
            if (sweep_divider == 0 && sweep_enable && sweep_shift && !muted())
                timer = sweep_target() & 0x7FF;
            if (sweep_divider == 0 || sweep_reload) {
                sweep_divider = sweep_period;
                sweep_reload = false;
            } else --sweep_divider;
        }
        uint8_t out() const {
            static const uint8_t kDuty[4][8] = {{0,1,0,0,0,0,0,0},
                                                {0,1,1,0,0,0,0,0},
                                                {0,1,1,1,1,0,0,0},
                                                {1,0,0,1,1,1,1,1}};
            if (!enabled || length == 0 || muted()) return 0;
            return kDuty[duty][seq] ? env.out() : 0;
        }
    };

    struct Triangle {
        bool enabled;
        uint16_t timer, counter;
        uint8_t length, seq;
        uint8_t linear, linear_reload_val;
        bool linear_reload, control;
        uint8_t out() const {
            static const uint8_t kSeq[32] = {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
                                             0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
            return kSeq[seq];
        }
    };

    struct Noise {
        bool enabled, mode;
        uint16_t timer, counter, lfsr;
        uint8_t length;
        bool length_halt;
        Envelope env;
        uint8_t out() const {
            if (!enabled || length == 0 || (lfsr & 1)) return 0;
            return env.out();
        }
    };

    struct Dmc {
        bool enabled, loop, irq_enable;
        uint16_t timer, counter;
        uint8_t output, shift, bits;
        bool silence;
        uint16_t addr, cur_addr;
        uint16_t len, remaining;
        uint8_t sample_buf;
        bool sample_full;
    };

    Pulse p1_, p2_;
    Triangle tri_;
    Noise noise_;
    Dmc dmc_;

    // frame counter
    bool five_step_, frame_irq_inhibit_, frame_irq_, dmc_irq_;
    int frame_div_;      // CPU cycles until next quarter-frame step
    int frame_step_;
    bool odd_cycle_;
    int pending_4017_delay_;  // hardware applies $4017 3-4 cycles late
    uint8_t pending_4017_val_;

    // sample generation
    double sample_acc_;
    static constexpr int kBufSize = 16384;
    int16_t buf_[kBufSize];
    int rd_, wr_;

    DmcReader dmc_read_;
    void* dmc_ctx_;

    void quarter_frame();
    void half_frame();
    void clock_channels();      // one CPU cycle of channel timers
    void dmc_clock();
    void emit_sample();
};

}  // namespace famemu::nes
