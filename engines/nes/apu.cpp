// famemu NES engine — 2A03 APU implementation. See apu.hpp.
#include "apu.hpp"

namespace famemu::nes {

static constexpr uint8_t kLength[32] = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

static constexpr uint16_t kNoisePeriod[16] = {  // NTSC, CPU cycles
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};

static constexpr uint16_t kDmcRate[16] = {  // NTSC, CPU cycles per bit
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

void Apu::write(uint16_t addr, uint8_t v) {
    switch (addr) {
        // ---- pulse 1 / pulse 2 ----
        case 0x4000: case 0x4004: {
            Pulse& p = (addr == 0x4000) ? p1_ : p2_;
            p.duty = v >> 6;
            p.length_halt = p.env.loop = v & 0x20;
            p.env.constant = v & 0x10;
            p.env.volume = v & 0x0F;
            break;
        }
        case 0x4001: case 0x4005: {
            Pulse& p = (addr == 0x4001) ? p1_ : p2_;
            p.sweep_enable = v & 0x80;
            p.sweep_period = (v >> 4) & 7;
            p.sweep_negate = v & 0x08;
            p.sweep_shift = v & 0x07;
            p.sweep_reload = true;
            break;
        }
        case 0x4002: case 0x4006: {
            Pulse& p = (addr == 0x4002) ? p1_ : p2_;
            p.timer = (p.timer & 0x0700) | v;
            break;
        }
        case 0x4003: case 0x4007: {
            Pulse& p = (addr == 0x4003) ? p1_ : p2_;
            p.timer = static_cast<uint16_t>((p.timer & 0x00FF) | ((v & 7) << 8));
            if (p.enabled) p.length = kLength[v >> 3];
            p.seq = 0;
            p.env.start = true;
            break;
        }
        // ---- triangle ----
        case 0x4008:
            tri_.control = v & 0x80;
            tri_.linear_reload_val = v & 0x7F;
            break;
        case 0x400A: tri_.timer = (tri_.timer & 0x0700) | v; break;
        case 0x400B:
            tri_.timer = static_cast<uint16_t>((tri_.timer & 0x00FF) | ((v & 7) << 8));
            if (tri_.enabled) tri_.length = kLength[v >> 3];
            tri_.linear_reload = true;
            break;
        // ---- noise ----
        case 0x400C:
            noise_.length_halt = noise_.env.loop = v & 0x20;
            noise_.env.constant = v & 0x10;
            noise_.env.volume = v & 0x0F;
            break;
        case 0x400E:
            noise_.mode = v & 0x80;
            noise_.timer = kNoisePeriod[v & 0x0F];
            break;
        case 0x400F:
            if (noise_.enabled) noise_.length = kLength[v >> 3];
            noise_.env.start = true;
            break;
        // ---- DMC ----
        case 0x4010:
            dmc_.irq_enable = v & 0x80;
            if (!dmc_.irq_enable) dmc_irq_ = false;
            dmc_.loop = v & 0x40;
            dmc_.timer = kDmcRate[v & 0x0F];
            break;
        case 0x4011: dmc_.output = v & 0x7F; break;
        case 0x4012: dmc_.addr = static_cast<uint16_t>(0xC000 + v * 64); break;
        case 0x4013: dmc_.len = static_cast<uint16_t>(v * 16 + 1); break;
        // ---- control ----
        case 0x4015:
            p1_.enabled = v & 0x01;
            p2_.enabled = v & 0x02;
            tri_.enabled = v & 0x04;
            noise_.enabled = v & 0x08;
            if (!p1_.enabled) p1_.length = 0;
            if (!p2_.enabled) p2_.length = 0;
            if (!tri_.enabled) tri_.length = 0;
            if (!noise_.enabled) noise_.length = 0;
            if (v & 0x10) {
                if (dmc_.remaining == 0) {
                    dmc_.cur_addr = dmc_.addr;
                    dmc_.remaining = dmc_.len;
                }
            } else {
                dmc_.remaining = 0;
            }
            dmc_irq_ = false;
            break;
        case 0x4017:
            // IRQ-inhibit applies immediately; the sequencer reset lands 2
            // CPU cycles later (3 when written on an odd cycle) — the value
            // blargg apu_test 4-jitter accepts with our write-at-instruction-
            // start timing model.
            frame_irq_inhibit_ = v & 0x40;
            if (frame_irq_inhibit_) frame_irq_ = false;
            pending_4017_val_ = v;
            pending_4017_delay_ = 2 + (odd_cycle_ ? 1 : 0);
            break;
        default: break;
    }
}

uint8_t Apu::read_status() {
    uint8_t r = 0;
    if (p1_.length) r |= 0x01;
    if (p2_.length) r |= 0x02;
    if (tri_.length) r |= 0x04;
    if (noise_.length) r |= 0x08;
    if (dmc_.remaining) r |= 0x10;
    if (frame_irq_) r |= 0x40;
    if (dmc_irq_) r |= 0x80;
    frame_irq_ = false;  // reading $4015 clears the frame IRQ flag
    return r;
}

void Apu::quarter_frame() {
    p1_.env.clock();
    p2_.env.clock();
    noise_.env.clock();
    if (tri_.linear_reload) tri_.linear = tri_.linear_reload_val;
    else if (tri_.linear) --tri_.linear;
    if (!tri_.control) tri_.linear_reload = false;
}

void Apu::half_frame() {
    auto clock_len = [](uint8_t& len, bool halt) {
        if (len && !halt) --len;
    };
    clock_len(p1_.length, p1_.length_halt);
    clock_len(p2_.length, p2_.length_halt);
    clock_len(tri_.length, tri_.control);
    clock_len(noise_.length, noise_.length_halt);
    p1_.clock_sweep();
    p2_.is_pulse2 = true;
    p2_.clock_sweep();
}

void Apu::dmc_clock() {
    if (dmc_.timer == 0) return;
    if (dmc_.counter) { --dmc_.counter; return; }
    dmc_.counter = dmc_.timer - 1;
    // output unit
    if (!dmc_.silence) {
        if (dmc_.shift & 1) {
            if (dmc_.output <= 125) dmc_.output += 2;
        } else if (dmc_.output >= 2) {
            dmc_.output -= 2;
        }
    }
    dmc_.shift >>= 1;
    if (dmc_.bits) --dmc_.bits;
    if (dmc_.bits == 0) {
        dmc_.bits = 8;
        if (dmc_.sample_full) {
            dmc_.silence = false;
            dmc_.shift = dmc_.sample_buf;
            dmc_.sample_full = false;
        } else {
            dmc_.silence = true;
        }
    }
    // memory reader keeps the buffer full
    if (!dmc_.sample_full && dmc_.remaining && dmc_read_) {
        dmc_.sample_buf = dmc_read_(dmc_ctx_, dmc_.cur_addr);
        dmc_.sample_full = true;
        dmc_.cur_addr = (dmc_.cur_addr == 0xFFFF) ? 0x8000 : dmc_.cur_addr + 1;
        if (--dmc_.remaining == 0) {
            if (dmc_.loop) {
                dmc_.cur_addr = dmc_.addr;
                dmc_.remaining = dmc_.len;
            } else if (dmc_.irq_enable) {
                dmc_irq_ = true;
            }
        }
    }
}

void Apu::clock_channels() {
    // Triangle: every CPU cycle.
    if (tri_.counter == 0) {
        tri_.counter = tri_.timer;
        if (tri_.length && tri_.linear && tri_.timer >= 2)  // ultrasonic guard
            tri_.seq = (tri_.seq + 1) & 31;
    } else --tri_.counter;

    // Pulses: every second CPU cycle (APU cycle).
    if (odd_cycle_) {
        auto clock_pulse = [](Pulse& p) {
            if (p.counter == 0) {
                p.counter = p.timer;
                p.seq = (p.seq + 1) & 7;
            } else --p.counter;
        };
        clock_pulse(p1_);
        clock_pulse(p2_);
    }
    odd_cycle_ = !odd_cycle_;

    // Noise: period table is in CPU cycles.
    if (noise_.counter == 0) {
        noise_.counter = noise_.timer ? noise_.timer - 1 : 0;
        uint16_t fb = (noise_.lfsr ^ (noise_.lfsr >> (noise_.mode ? 6 : 1))) & 1;
        noise_.lfsr = static_cast<uint16_t>((noise_.lfsr >> 1) | (fb << 14));
    } else --noise_.counter;

    dmc_clock();
}

void Apu::emit_sample() {
    const double p = p1_.out() + p2_.out();
    const double pulse_out = p > 0 ? 95.88 / (8128.0 / p + 100.0) : 0.0;
    const double tnd = tri_.out() / 8227.0 + noise_.out() / 12241.0 +
                       dmc_.output / 22638.0;
    const double tnd_out = tnd > 0 ? 159.79 / (1.0 / tnd + 100.0) : 0.0;
    // 0..~0.48 -> center and scale
    double s = (pulse_out + tnd_out - 0.24) * 2.0;
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    int next = (wr_ + 1) % kBufSize;
    if (next != rd_) {  // drop when full (headless runs don't drain)
        buf_[wr_] = static_cast<int16_t>(s * 30000);
        wr_ = next;
    }
}

void Apu::tick(int cpu_cycles) {
    for (int i = 0; i < cpu_cycles; ++i) {
        // delayed $4017 sequencer reset
        if (pending_4017_delay_ > 0 && --pending_4017_delay_ == 0) {
            five_step_ = pending_4017_val_ & 0x80;
            frame_step_ = 0;
            frame_div_ = 7457;
            if (five_step_) { quarter_frame(); half_frame(); }
        }
        // frame counter
        if (--frame_div_ <= 0) {
            if (!five_step_) {
                // Steps at CPU cycles 7457/14913/22371/29829; period 29830.
                static const int kStep4[4] = {7457, 7456, 7458, 7459};
                quarter_frame();
                if (frame_step_ == 1 || frame_step_ == 3) half_frame();
                if (frame_step_ == 3 && !frame_irq_inhibit_) frame_irq_ = true;
                frame_step_ = (frame_step_ + 1) & 3;
                frame_div_ = kStep4[frame_step_];
            } else {
                static const int kStep5[5] = {7457, 7456, 7458, 7458, 7452};
                if (frame_step_ != 3) quarter_frame();  // step 4 of 5 is silent
                if (frame_step_ == 1 || frame_step_ == 4) half_frame();
                frame_step_ = (frame_step_ + 1) % 5;
                frame_div_ = kStep5[frame_step_];
            }
        }
        clock_channels();
        sample_acc_ += kSampleRate / kCpuHz;
        if (sample_acc_ >= 1.0) {
            sample_acc_ -= 1.0;
            emit_sample();
        }
    }
}

}  // namespace famemu::nes
