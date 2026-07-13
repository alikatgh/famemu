// famemu NES engine — the machine: CPU + PPU + RAM + controllers + OAM DMA
// on one bus, stepped in lockstep (3 PPU dots per CPU cycle).
//
// APU audio is the next gate; $4015 reads return 0 and APU writes are ignored,
// which is silent but harmless for video parity.
#pragma once

#include <cstdint>
#include <cstring>

#include "apu.hpp"
#include "cart.hpp"
#include "cpu.hpp"
#include "ppu.hpp"
#include "state.hpp"

namespace famemu::nes {

class NesSystem : public Bus {
public:
    NesSystem() : ppu_(cart_), cpu_(*this) {
        apu_.set_dmc_reader(
            [](void* ctx, uint16_t a) {
                return static_cast<NesSystem*>(ctx)->read(a);
            },
            this);
    }

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
        apu_.reset();
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
    Apu& apu() { return apu_; }

    // ---- save states ----------------------------------------------------
    static constexpr uint32_t kStateMagic = 0x46414D31;  // "FAM1"

    size_t state_size() const {
        // Generous fixed bound: header + cpu + ram/sram/pads + ppu (vram,
        // palette, oam, fb, pipeline) + apu + cart regs + chr-ram extra.
        return 8 + 32 + sizeof ram_ + sizeof sram_ + 8 +
               (0x800 + 32 + 256 + 256 * 240 + 256) + sizeof(Apu) + 256 +
               cart_state_extra();
    }

    bool state_save(uint8_t* buf, size_t len) {
        StateWriter w{buf, len};
        w.io(kStateMagic);
        serialize_all(w);
        return w.ok;
    }

    bool state_load(const uint8_t* buf, size_t len) {
        StateReader r{buf, len};
        uint32_t magic = 0;
        r.io(magic);
        if (magic != kStateMagic) return false;
        serialize_all(r);
        if (r.ok) apu_.post_load();
        return r.ok;
    }

    // ---- Bus ----------------------------------------------------------
    // NOTE on timing model: the PPU catches up AFTER each instruction
    // (instruction-granular). Sub-instruction $2002/$2000 dot alignment
    // (blargg ppu_vbl_nmi 02/05-08/10) needs per-cycle microcode — tried a
    // tick-per-bus-access model overnight; it traded failures rather than
    // fixing them and risked the 100% game parity, so it was reverted.
    uint8_t read(uint16_t a) override {
        if (a < 0x2000) return ram_[a & 0x7FF];
        if (a < 0x4000) return ppu_.read_reg(a);
        if (a == 0x4015) return apu_.read_status();
        if (a == 0x4016) return read_pad(0);
        if (a == 0x4017) return read_pad(1);
        if (a < 0x4020) return 0;
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
            apu_.write(a, v);  // $4000-$4013, $4015, $4017
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
    Apu apu_;
    uint8_t ram_[0x800];
    uint8_t sram_[0x2000];
    uint8_t pad_state_[2], pad_shift_[2];
    bool strobe_ = false;
    bool nmi_pending_ = false;

    void step_instruction() {
        const uint64_t before = cpu_.cyc;
        cpu_.step();
        tick_hw(cpu_.cyc - before);
        // NMI: delivered after the instruction FOLLOWING the one during
        // which the edge occurred (blargg 04-nmi_control).
        if (nmi_pending_) {
            nmi_pending_ = false;
            const uint64_t b2 = cpu_.cyc;
            cpu_.nmi();
            tick_hw(cpu_.cyc - b2);
        }
        if (ppu_.take_nmi()) nmi_pending_ = true;
        if (apu_.irq_pending() || cart_.irq_pending()) cpu_.irq();  // I-masked
    }

    void tick_hw(uint64_t cpu_cycles) {
        for (uint64_t i = 0; i < cpu_cycles * 3; ++i) ppu_.tick();
        apu_.tick(static_cast<int>(cpu_cycles));
    }

    void oam_dma(uint8_t page) {
        const uint16_t base = static_cast<uint16_t>(page) << 8;
        for (int i = 0; i < 256; ++i) ppu_.oam_dma_write(static_cast<uint8_t>(i), read(static_cast<uint16_t>(base + i)));
        // 513 CPU cycles (+1 on odd cycle); the PPU keeps running underneath.
        const uint64_t stall = 513 + (cpu_.cyc & 1);
        cpu_.cyc += stall;
        tick_hw(stall);
    }

    uint8_t read_pad(int port) {
        if (strobe_) pad_shift_[port] = pad_state_[port];
        uint8_t r = pad_shift_[port] & 1;
        if (!strobe_) pad_shift_[port] = static_cast<uint8_t>(0x80 | (pad_shift_[port] >> 1));
        return static_cast<uint8_t>(0x40 | r);  // open-bus upper bits
    }

    size_t cart_state_extra() const { return cart_.state_extra(); }

    template <class S>
    void serialize_all(S& s) {
        s.io(cpu_.pc); s.io(cpu_.a); s.io(cpu_.x); s.io(cpu_.y);
        s.io(cpu_.s); s.io(cpu_.p); s.io(cpu_.cyc);
        s.io(ram_); s.io(sram_);
        s.io(pad_state_); s.io(pad_shift_); s.io(strobe_); s.io(nmi_pending_);
        ppu_.serialize(s);
        apu_.serialize(s);
        cart_.serialize(s);
    }
};

}  // namespace famemu::nes
