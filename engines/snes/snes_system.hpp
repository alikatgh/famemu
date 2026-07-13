// famemu SNES engine — the machine: 65816 + S-PPU + WRAM + LoROM + DMA +
// APU-port IPL stub, scoped to boot KORA (docs/PLATFORM.md, NIGHTLOG).
//
// Timing model: coarse. cpu.cyc ticks ~1/memory access; a scanline is
// budgeted at 190 such cycles (~1364 master / 6 with fudge), 262 lines/frame,
// NMI + auto-joypad at line 225. KORA's loop syncs on NMI, so frame-level
// behavior is right even though per-instruction timing is approximate.
//
// Audio: $2140-43 implement the IPL ROM's transfer protocol on the main-CPU
// side, capturing the uploaded driver into real ARAM; the kickoff starts a
// real SPC700 (driver-scoped core, spc700.hpp) driving a real S-DSP (BRR
// voices + ADSR + echo, sdsp.hpp) at 32 kHz.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "cpu65816.hpp"
#include "nes/state.hpp"
#include "sdsp.hpp"
#include "spc700.hpp"
#include "sppu.hpp"

namespace famemu::snes {

class SnesSystem : public Bus16 {
public:
    SnesSystem() : cpu_(*this) {}

    bool load_rom(const uint8_t* data, size_t len) {
        if (len < 0x8000) return false;
        if (len % 0x8000 == 512) { data += 512; len -= 512; }  // copier header
        rom_.assign(data, data + len);
        power_on();
        return true;
    }

    void power_on() {
        std::memset(wram_, 0, sizeof wram_);
        std::memset(sram_, 0, sizeof sram_);
        std::memset(dma_, 0, sizeof dma_);
        ppu_.reset();
        nmitimen_ = 0;
        rdnmi_ = 0;
        in_vblank_ = false;
        joy1_ = 0;
        buttons_ = 0;
        spc_.reset();
        dsp_.reset();
        spc_.set_dsp(&dsp_);
        ipl_transfer_ = false;
        ipl_addr_ = 0;
        ipl_last_index_ = 0xFF;
        ipl_ports_[0] = ipl_ports_[1] = ipl_ports_[2] = ipl_ports_[3] = 0;
        sample_acc_ = 0.0;
        line_ = 0;
        cpu_.reset();
    }

    void set_buttons(uint32_t famemu_core_buttons) { buttons_ = famemu_core_buttons; }

    void run_frame() {
        for (line_ = 0; line_ < 262; ++line_) {
            const uint64_t budget = cpu_.cyc + 190;
            if (line_ == 225) {
                in_vblank_ = true;
                rdnmi_ |= 0x80;
                latch_joypad();
                if (nmitimen_ & 0x80) cpu_.nmi();
            } else if (line_ == 0) {
                in_vblank_ = false;
            }
            while (cpu_.cyc < budget && !cpu_.stopped) cpu_.step();
            spc_.run(65);  // ~1.024 MHz / 15734 lines
            if (line_ < SPpu::kHeight) ppu_.render_line(line_);
        }
        // 32 kHz DSP output: 32000 / 60.0988 fps ≈ 532.5 samples per frame.
        sample_acc_ += 32000.0 / 60.0988;
        const int n = static_cast<int>(sample_acc_);
        sample_acc_ -= n;
        dsp_.render(n);
    }

    size_t read_audio(int16_t* out, size_t max_frames) {
        return dsp_.read_samples(out, max_frames);
    }

    const uint8_t* framebuffer() const { return ppu_.framebuffer(); }
    Cpu65816& cpu() { return cpu_; }
    SPpu& ppu() { return ppu_; }
    uint8_t wram_byte(uint32_t i) const { return wram_[i & 0x1FFFF]; }  // debug

    // ---- save states (same writer/reader as the NES engine) -------------
    static constexpr uint32_t kStateMagic = 0x46534E31;  // "FSN1"

    size_t state_size() const {
        return 8 + 64 + sizeof wram_ + sizeof sram_ + sizeof dma_ + 64 +
               (sizeof(SPpu)) + (sizeof(Spc700)) + (sizeof(SDsp)) + 256;
    }
    bool state_save(uint8_t* buf, size_t len) {
        famemu::nes::StateWriter w{buf, len};
        w.io(kStateMagic);
        serialize_all(w);
        return w.ok;
    }
    bool state_load(const uint8_t* buf, size_t len) {
        famemu::nes::StateReader r{buf, len};
        uint32_t magic = 0;
        r.io(magic);
        if (magic != kStateMagic) return false;
        serialize_all(r);
        if (r.ok) dsp_.post_load();
        return r.ok;
    }
    uint8_t spc_song() const { return spc_.is_running() ? spc_dbg_port0_ : 0xFF; }  // debug

    // ---- Bus16 -----------------------------------------------------------
    uint8_t read(uint32_t a24) override {
        const uint8_t bank = (a24 >> 16) & 0xFF;
        const uint16_t off = a24 & 0xFFFF;
        if (bank == 0x7E) return wram_[off];
        if (bank == 0x7F) return wram_[0x10000 + off];
        const uint8_t b = bank & 0x7F;
        if (b <= 0x3F) {
            if (off < 0x2000) return wram_[off];
            if (off >= 0x2140 && off <= 0x217F) return apu_read((off - 0x2140) & 3);
            if (off >= 0x2100 && off < 0x2140) return ppu_.read(off & 0xFF);
            if (off == 0x4210) { uint8_t r = rdnmi_ | 0x02; rdnmi_ &= 0x7F; return r; }
            if (off == 0x4212) return static_cast<uint8_t>(in_vblank_ ? 0x80 : 0x00);
            if (off == 0x4218) return static_cast<uint8_t>(joy1_);
            if (off == 0x4219) return static_cast<uint8_t>(joy1_ >> 8);
            if (off >= 0x8000) return rom_at(b, off);
            return 0;
        }
        if (b >= 0x70 && b <= 0x7D && off < 0x8000)
            return sram_[off & (sizeof sram_ - 1)];
        return rom_at(b, off >= 0x8000 ? off : static_cast<uint16_t>(off | 0x8000));
    }

    void write(uint32_t a24, uint8_t v) override {
        const uint8_t bank = (a24 >> 16) & 0xFF;
        const uint16_t off = a24 & 0xFFFF;
        if (bank == 0x7E) { wram_[off] = v; return; }
        if (bank == 0x7F) { wram_[0x10000 + off] = v; return; }
        const uint8_t b = bank & 0x7F;
        if (b <= 0x3F) {
            if (off < 0x2000) { wram_[off] = v; return; }
            if (off >= 0x2140 && off <= 0x217F) { apu_write((off - 0x2140) & 3, v); return; }
            if (off >= 0x2100 && off < 0x2140) { ppu_.write(off & 0xFF, v); return; }
            if (off == 0x4200) { nmitimen_ = v; return; }
            if (off == 0x420B) { run_dma(v); return; }
            if (off == 0x420C) { return; }  // HDMA: not used by KORA yet
            if (off >= 0x4300 && off < 0x4380) { dma_[off - 0x4300] = v; return; }
            return;
        }
        if (b >= 0x70 && b <= 0x7D && off < 0x8000)
            sram_[off & (sizeof sram_ - 1)] = v;
    }

private:
    Cpu65816 cpu_{*this};
    SPpu ppu_;
    std::vector<uint8_t> rom_;
    uint8_t wram_[0x20000];
    uint8_t sram_[0x2000];
    uint8_t dma_[0x80];

    uint8_t nmitimen_ = 0, rdnmi_ = 0;
    bool in_vblank_ = false;
    uint16_t joy1_ = 0;
    uint32_t buttons_ = 0;
    int line_ = 0;

    // Audio subsystem + IPL transfer capture (see file header).
    Spc700 spc_;
    SDsp dsp_{spc_.aram};
    bool ipl_transfer_ = false;
    uint16_t ipl_addr_ = 0;
    uint8_t ipl_last_index_ = 0xFF;
    uint8_t ipl_ports_[4] = {0, 0, 0, 0};  // last main-CPU writes to $2140-43
    double sample_acc_ = 0.0;
    uint8_t spc_dbg_port0_ = 0;

    uint8_t rom_at(uint8_t bank7f, uint16_t off) const {
        const size_t i = static_cast<size_t>(bank7f) * 0x8000 + (off - 0x8000);
        return rom_.empty() ? 0 : rom_[i % rom_.size()];
    }

    void latch_joypad() {
        // FamemuCoreButton -> SNES JOY1: $4219(hi)=B Y Sel St U D L R,
        // $4218(lo)=A X L R 0000.
        uint16_t j = 0;
        if (buttons_ & (1u << 1)) j |= 0x8000;   // B
        if (buttons_ & (1u << 9)) j |= 0x4000;   // Y
        if (buttons_ & (1u << 2)) j |= 0x2000;   // Select
        if (buttons_ & (1u << 3)) j |= 0x1000;   // Start
        if (buttons_ & (1u << 4)) j |= 0x0800;   // Up
        if (buttons_ & (1u << 5)) j |= 0x0400;   // Down
        if (buttons_ & (1u << 6)) j |= 0x0200;   // Left
        if (buttons_ & (1u << 7)) j |= 0x0100;   // Right
        if (buttons_ & (1u << 0)) j |= 0x0080;   // A
        if (buttons_ & (1u << 8)) j |= 0x0040;   // X
        if (buttons_ & (1u << 10)) j |= 0x0020;  // L
        if (buttons_ & (1u << 11)) j |= 0x0010;  // R
        joy1_ = j;
    }

    uint8_t apu_read(int port) {
        if (spc_.is_running()) return spc_.main_read_port(port);
        // IPL ROM boot state / transfer echo.
        if (!ipl_transfer_) return port == 0 ? 0xAA : (port == 1 ? 0xBB : 0);
        return ipl_ports_[port];  // echoes written below
    }

    void apu_write(int port, uint8_t v) {
        if (spc_.is_running()) {
            if (port == 0) spc_dbg_port0_ = v;
            spc_.main_write_port(port, v);
            return;
        }
        const uint8_t prev0 = ipl_ports_[0];
        if (port != 0) { ipl_ports_[port] = v; return; }
        // Port-0 writes drive the IPL transfer protocol.
        if (!ipl_transfer_) {
            if (v == 0xCC && ipl_ports_[1] != 0) {
                ipl_addr_ = static_cast<uint16_t>(ipl_ports_[2] |
                                                  (ipl_ports_[3] << 8));
                ipl_transfer_ = true;
                ipl_last_index_ = 0xFF;  // next data byte is index 0
            }
            ipl_ports_[0] = v;  // echo
            return;
        }
        const uint8_t expected = static_cast<uint8_t>(ipl_last_index_ + 1);
        if (v == expected) {  // next data byte (port 1 holds it)
            spc_.aram[ipl_addr_++] = ipl_ports_[1];
            ipl_last_index_ = v;
        } else if (v != prev0) {  // index jump: new block or execute
            const uint16_t target = static_cast<uint16_t>(ipl_ports_[2] |
                                                          (ipl_ports_[3] << 8));
            if (ipl_ports_[1] == 0) {
                spc_.start_at(target);  // kickoff: SPC700 begins execution
            } else {
                ipl_addr_ = target;     // additional block
                ipl_last_index_ = 0xFF;
            }
        }
        ipl_ports_[0] = v;  // echo
    }

    void run_dma(uint8_t enable) {
        for (int ch = 0; ch < 8; ++ch) {
            if (!(enable & (1 << ch))) continue;
            uint8_t* r = &dma_[ch * 0x10];
            const uint8_t ctrl = r[0];
            const uint8_t breg = r[1];
            uint32_t aaddr = static_cast<uint32_t>(r[2]) |
                             (static_cast<uint32_t>(r[3]) << 8) |
                             (static_cast<uint32_t>(r[4]) << 16);
            uint32_t count = static_cast<uint32_t>(r[5]) |
                             (static_cast<uint32_t>(r[6]) << 8);
            if (count == 0) count = 0x10000;
            const int mode = ctrl & 7;
            const bool fixed = ctrl & 0x08;
            const int step = (ctrl & 0x10) ? -1 : 1;
            uint32_t n = 0;
            while (count--) {
                uint8_t breg_off = 0;
                switch (mode) {
                    case 0: breg_off = 0; break;
                    case 1: breg_off = n & 1; break;            // l,h,l,h
                    case 2: breg_off = 0; break;                // l,l
                    case 3: breg_off = (n >> 1) & 1; break;     // l,l,h,h
                    case 4: breg_off = n & 3; break;
                    default: breg_off = n & 1; break;
                }
                if (ctrl & 0x80) {  // B -> A (rare; not used by KORA)
                    wr_cpu(aaddr, read(0x2100u | (breg + breg_off)));
                } else {
                    write(0x2100u | static_cast<uint8_t>(breg + breg_off),
                          read(aaddr));
                }
                if (!fixed) aaddr = (aaddr & 0xFF0000) |
                                    ((aaddr + step) & 0xFFFF);
                ++n;
            }
            r[5] = r[6] = 0;  // count drained
        }
    }
    void wr_cpu(uint32_t a, uint8_t v) { write(a, v); }

    template <class S>
    void serialize_all(S& s) {
        s.io(cpu_.a); s.io(cpu_.x); s.io(cpu_.y); s.io(cpu_.s); s.io(cpu_.d);
        s.io(cpu_.dbr); s.io(cpu_.pbr); s.io(cpu_.pc); s.io(cpu_.p);
        s.io(cpu_.e); s.io(cpu_.cyc); s.io(cpu_.waiting); s.io(cpu_.stopped);
        s.io(wram_); s.io(sram_); s.io(dma_);
        s.io(nmitimen_); s.io(rdnmi_); s.io(in_vblank_); s.io(joy1_);
        s.io(buttons_); s.io(line_);
        s.io(ipl_transfer_); s.io(ipl_addr_); s.io(ipl_last_index_);
        s.io(ipl_ports_); s.io(sample_acc_);
        ppu_.serialize(s);
        spc_.serialize(s);
        dsp_.serialize(s);
    }
};

}  // namespace famemu::snes
