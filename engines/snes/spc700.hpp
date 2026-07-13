// famemu SNES engine — SPC700 audio CPU (+ timers, DSP register bridge).
//
// Scoped clean-room implementation: the opcode set covers what KORA's
// generated driver (kora/snes/make_spc.py) executes, plus a loud stderr log
// on any unimplemented opcode so driver growth is caught immediately instead
// of silently misplaying. Timing: ~1.024 MHz, timers 0/1 at 8 kHz stages.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace famemu::snes {

class SDsp;  // sdsp.hpp

class Spc700 {
public:
    uint8_t aram[0x10000];

    Spc700() { reset(); }

    void reset() {
        std::memset(aram, 0, sizeof aram);
        a = x = y = psw = 0;
        sp = 0xFF;
        pc = 0xFFC0;
        running = false;
        std::memset(ports_in, 0, sizeof ports_in);
        std::memset(ports_out, 0, sizeof ports_out);
        std::memset(timer_target, 0, sizeof timer_target);
        std::memset(timer_counter, 0, sizeof timer_counter);
        std::memset(timer_stage, 0, sizeof timer_stage);
        timer_enable = 0;
        dsp_addr = 0;
        stage_acc = 0;
    }

    void set_dsp(SDsp* d) { dsp_ = d; }

    // Main-CPU side of the mailbox ports ($2140-43).
    void main_write_port(int i, uint8_t v) { ports_in[i & 3] = v; }
    uint8_t main_read_port(int i) const { return ports_out[i & 3]; }

    void start_at(uint16_t entry) { pc = entry; running = true; }
    bool is_running() const { return running; }

    // Run n SPC cycles (approximate: 2 cycles/op average is fine for this
    // driver; the sequencer is timer-paced, not cycle-paced).
    void run(int cycles) {
        if (!running) return;
        tick_timers(cycles);
        for (int budget = cycles; budget > 0; budget -= 2) step();
    }

    // registers (public for save states later)
    uint8_t a, x, y, psw, sp;
    uint16_t pc;
    bool running;

private:
    static constexpr uint8_t C = 0x01, Z = 0x02, N = 0x80;

    SDsp* dsp_ = nullptr;
    uint8_t ports_in[4], ports_out[4];    // in: main->spc, out: spc->main
    uint8_t timer_target[3], timer_counter[3], timer_stage[3];
    uint8_t timer_enable;
    uint8_t dsp_addr;
    int stage_acc;

    void tick_timers(int cycles) {
        // Timers 0/1: 8 kHz stage clock = every 128 cycles (timer 2: 64 kHz,
        // unused by the driver).
        stage_acc += cycles;
        while (stage_acc >= 128) {
            stage_acc -= 128;
            for (int t = 0; t < 2; ++t) {
                if (!(timer_enable & (1 << t))) continue;
                if (++timer_stage[t] >= (timer_target[t] ? timer_target[t] : 0)) {
                    if (timer_target[t]) {
                        timer_stage[t] = 0;
                        timer_counter[t] = (timer_counter[t] + 1) & 0x0F;
                    }
                }
            }
        }
    }

    uint8_t io_read(uint8_t addr);
    void io_write(uint8_t addr, uint8_t v);

    uint8_t rd(uint16_t addr) {
        if ((addr & 0xFFF0) == 0x00F0) return io_read(addr & 0xFF);
        return aram[addr];
    }
    void wr(uint16_t addr, uint8_t v) {
        if ((addr & 0xFFF0) == 0x00F0) { io_write(addr & 0xFF, v); return; }
        aram[addr] = v;
    }
    uint8_t fetch() { return rd(pc++); }
    void setZN(uint8_t v) { psw = (psw & ~(Z | N)) | (v ? 0 : Z) | (v & 0x80); }

    void step();
};

}  // namespace famemu::snes
