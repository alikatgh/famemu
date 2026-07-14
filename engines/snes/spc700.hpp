// famemu SNES engine — SPC700 audio CPU (+ timers, DSP register bridge).
//
// Clean-room implementation of the full 256-opcode instruction set with
// per-instruction cycle counts, so commercial sound drivers run, not just
// KORA's generated one. Timing: ~1.024 MHz; timers 0/1 at 8 kHz stages,
// timer 2 at 64 kHz.
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

    // Run n SPC cycles (per-instruction cycle counts via step()'s return).
    void run(int cycles) {
        if (!running) return;
        tick_timers(cycles);
        for (int budget = cycles; budget > 0;) budget -= step();
    }

    // registers (public for save states)
    uint8_t a, x, y, psw, sp;
    uint16_t pc;
    bool running;

    template <class S>
    void serialize(S& s) {
        s.io(aram);
        s.io(a); s.io(x); s.io(y); s.io(psw); s.io(sp); s.io(pc); s.io(running);
        s.io(ports_in); s.io(ports_out);
        s.io(timer_target); s.io(timer_counter); s.io(timer_stage);
        s.io(timer_enable); s.io(dsp_addr); s.io(stage_acc); s.io(stage16);
    }

private:
    static constexpr uint8_t C = 0x01, Z = 0x02, I = 0x04, H = 0x08,
                             B = 0x10, P = 0x20, V = 0x40, N = 0x80;

    SDsp* dsp_ = nullptr;
    uint8_t ports_in[4], ports_out[4];    // in: main->spc, out: spc->main
    uint8_t timer_target[3], timer_counter[3], timer_stage[3];
    uint8_t timer_enable;
    uint8_t dsp_addr;
    int stage_acc;
    uint8_t stage16 = 0;

    void tick_timers(int cycles) {
        // Base stage clock is 16 cycles (timer 2, 64 kHz); timers 0/1 tick
        // every 8th stage (128 cycles, 8 kHz).
        stage_acc += cycles;
        while (stage_acc >= 16) {
            stage_acc -= 16;
            tick_one(2);
            if (++stage16 == 8) {
                stage16 = 0;
                tick_one(0);
                tick_one(1);
            }
        }
    }
    void tick_one(int t) {
        if (!(timer_enable & (1 << t)) || !timer_target[t]) return;
        if (++timer_stage[t] >= timer_target[t]) {
            timer_stage[t] = 0;
            timer_counter[t] = (timer_counter[t] + 1) & 0x0F;
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
    void setZN16(uint16_t v) {
        psw = static_cast<uint8_t>((psw & ~(Z | N)) | (v ? 0 : Z) | ((v >> 8) & 0x80));
    }
    void setFlag(uint8_t f, bool on) { psw = on ? (psw | f) : (psw & ~f); }
    uint16_t dp(uint8_t a) const {
        return static_cast<uint16_t>(((psw & P) ? 0x100 : 0) | a);
    }
    void push(uint8_t v) { aram[0x100 | sp--] = v; }
    uint8_t pull() { return aram[0x100 | ++sp]; }

    int step();  // returns cycles consumed
};

}  // namespace famemu::snes
