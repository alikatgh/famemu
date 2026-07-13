// famemu NES engine — the machine: CPU + PPU + RAM + controllers + OAM DMA
// on one bus, stepped in lockstep (3 PPU dots per CPU cycle).
//
// APU audio is the next gate; $4015 reads return 0 and APU writes are ignored,
// which is silent but harmless for video parity.
#pragma once

#include <cstdint>
#include <cstring>

#include "cart.hpp"
#include "cpu.hpp"
#include "ppu.hpp"

namespace famemu::nes {

class NesSystem : public Bus {
public:
    NesSystem() : ppu_(cart_), cpu_(*this) {}

    bool load_rom(const uint8_t* data, size_t len) {
        if (!cart_.load(data, len)) return false;
        power_on();
        return true;
    }

    void power_on() {
        std::memset(ram_, 0, sizeof ram_);
        std::memset(sram_, 0, sizeof sram_);
        pad_state_[0] = pad_state_[1] = 0;
        pad_shift_[0] = pad_shift_[1] = 0;
        strobe_ = false;
        ppu_.reset();
        cpu_.reset();
    }

    void set_buttons(int port, uint8_t buttons) {
        if (port == 0 || port == 1) pad_state_[port] = buttons;
    }

    // Run until the PPU completes the current frame (vblank start).
    void run_frame() {
        const uint64_t target = ppu_.frame_count() + 1;
        while (ppu_.frame_count() < target) step_instruction();
    }

    const uint8_t* framebuffer() const { return ppu_.framebuffer(); }  // 256x240 indices
    Ppu& ppu() { return ppu_; }
    Cpu6502& cpu() { return cpu_; }

    // ---- Bus ----------------------------------------------------------
    uint8_t read(uint16_t a) override {
        if (a < 0x2000) return ram_[a & 0x7FF];
        if (a < 0x4000) return ppu_.read_reg(a);
        if (a == 0x4016) return read_pad(0);
        if (a == 0x4017) return read_pad(1);
        if (a < 0x4020) return 0;  // APU/IO (APU is the next gate)
        if (a >= 0x8000) return cart_.cpu_read(a);
        if (a >= 0x6000) return sram_[a - 0x6000];
        return 0;
    }

    void write(uint16_t a, uint8_t v) override {
        if (a < 0x2000) {
            ram_[a & 0x7FF] = v;
        } else if (a < 0x4000) {
            ppu_.write_reg(a, v);
        } else if (a == 0x4014) {
            oam_dma(v);
        } else if (a == 0x4016) {
            strobe_ = v & 1;
            if (strobe_) {
                pad_shift_[0] = pad_state_[0];
                pad_shift_[1] = pad_state_[1];
            }
        } else if (a < 0x4020) {
            // APU registers: accepted, sound comes with the APU gate.
        } else if (a >= 0x8000) {
            cart_.cpu_write(a, v);
        } else if (a >= 0x6000) {
            sram_[a - 0x6000] = v;
        }
    }

private:
    Cart cart_;
    Ppu ppu_;
    Cpu6502 cpu_;
    uint8_t ram_[0x800];
    uint8_t sram_[0x2000];
    uint8_t pad_state_[2], pad_shift_[2];
    bool strobe_ = false;

    void step_instruction() {
        const uint64_t before = cpu_.cyc;
        cpu_.step();
        tick_ppu(cpu_.cyc - before);
        if (ppu_.take_nmi()) {
            const uint64_t b2 = cpu_.cyc;
            cpu_.nmi();
            tick_ppu(cpu_.cyc - b2);
        }
    }

    void tick_ppu(uint64_t cpu_cycles) {
        for (uint64_t i = 0; i < cpu_cycles * 3; ++i) ppu_.tick();
    }

    void oam_dma(uint8_t page) {
        const uint16_t base = static_cast<uint16_t>(page) << 8;
        for (int i = 0; i < 256; ++i) ppu_.oam_dma_write(static_cast<uint8_t>(i), read(static_cast<uint16_t>(base + i)));
        // 513 CPU cycles (+1 on odd cycle); the PPU keeps running underneath.
        const uint64_t stall = 513 + (cpu_.cyc & 1);
        cpu_.cyc += stall;
        tick_ppu(stall);
    }

    uint8_t read_pad(int port) {
        if (strobe_) pad_shift_[port] = pad_state_[port];
        uint8_t r = pad_shift_[port] & 1;
        if (!strobe_) pad_shift_[port] = static_cast<uint8_t>(0x80 | (pad_shift_[port] >> 1));
        return static_cast<uint8_t>(0x40 | r);  // open-bus upper bits
    }
};

}  // namespace famemu::nes
