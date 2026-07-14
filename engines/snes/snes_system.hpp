// famemu SNES engine — the machine: 65816 + S-PPU + WRAM + LoROM/HiROM +
// DMA/HDMA + APU-port IPL bridge + SPC700/S-DSP, general-purpose scope
// (docs/PLATFORM.md, NIGHTLOG).
//
// Timing model: master cycles (21.477 MHz). Each bus access costs its
// region's real speed (6 fast / 8 slow / 12 joypad, FastROM via $420D);
// internal CPU cycles cost 6. A scanline is 1364 master cycles, 262
// lines/frame (NTSC), NMI at the vblank line ($4210 RDNMI + $4200 NMITIMEN),
// H/V IRQ timers ($4207-$420A, $4211 TIMEUP), auto-joypad $4218-$421F.
// DMA/HDMA steal CPU time at 8 master cycles/byte.
//
// Mapping: LoROM and HiROM, scored from the ROM header ($7FC0/$FFC0).
// Coprocessors: SA-1 (sa1.hpp) and SuperFX/GSU (superfx.hpp), detected from
// the header chip byte and interleaved with the main CPU per scanline.
//
// Audio: $2140-43 implement the IPL ROM's transfer protocol on the main-CPU
// side, capturing the uploaded driver into real ARAM; the kickoff starts a
// real SPC700 (full instruction set, spc700.hpp) driving a real S-DSP (BRR
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

class Sa1;       // sa1.hpp
class SuperFx;   // superfx.hpp

class SnesSystem : public Bus16 {
public:
    static constexpr int kLineCycles = 1364;   // master cycles per scanline
    static constexpr int kLinesPerFrame = 262; // NTSC

    enum class Mapping : uint8_t { LoROM = 0, HiROM = 1 };

    SnesSystem();
    ~SnesSystem();

    bool load_rom(const uint8_t* data, size_t len);
    void power_on();

    void set_buttons(uint32_t famemu_core_buttons) { buttons_[0] = famemu_core_buttons; }
    void set_buttons2(uint32_t famemu_core_buttons) { buttons_[1] = famemu_core_buttons; }

    void run_frame();

    size_t read_audio(int16_t* out, size_t max_frames) {
        return dsp_.read_samples(out, max_frames);
    }

    const uint8_t* framebuffer() const { return ppu_.framebuffer(); }
    Cpu65816& cpu() { return cpu_; }
    SPpu& ppu() { return ppu_; }
    uint8_t wram_byte(uint32_t i) const { return wram_[i & 0x1FFFF]; }  // debug
    Mapping mapping() const { return mapping_; }
    bool has_sa1() const { return sa1_ != nullptr; }
    bool has_superfx() const { return sfx_ != nullptr; }

    // ---- save states (same writer/reader as the NES engine) -------------
    static constexpr uint32_t kStateMagic = 0x46534E32;  // "FSN2"

    size_t state_size() const;
    bool state_save(uint8_t* buf, size_t len);
    bool state_load(const uint8_t* buf, size_t len);
    uint8_t spc_song() const { return spc_.is_running() ? spc_dbg_port0_ : 0xFF; }  // debug

    // ---- Bus16 -----------------------------------------------------------
    uint8_t read(uint32_t a24) override;
    void write(uint32_t a24, uint8_t v) override;
    int speed(uint32_t a24) const override {
        const uint8_t bank = (a24 >> 16) & 0xFF;
        const uint16_t off = a24 & 0xFFFF;
        if (bank >= 0x40 && bank <= 0x7F) return 8;              // ROM/SRAM/WRAM
        if (bank >= 0xC0) return (memsel_ & 1) ? 6 : 8;          // HiROM upper
        // banks 00-3F / 80-BF
        if (off < 0x2000) return 8;                               // WRAM mirror
        if (off < 0x4000) return 6;                               // PPU/APU I/O
        if (off < 0x4200) return 12;                              // joypad serial
        if (off < 0x6000) return 6;                               // CPU I/O
        if (off < 0x8000) return 8;                               // expansion
        return ((bank & 0x80) && (memsel_ & 1)) ? 6 : 8;          // ROM
    }

    // ROM/SRAM access for coprocessors.
    uint8_t rom_linear(uint32_t i) const { return rom_.empty() ? 0 : rom_[i % rom_.size()]; }
    size_t rom_size() const { return rom_.size(); }
    uint8_t* sram_data() { return sram_; }
    uint32_t sram_mask() const { return sram_mask_; }

private:
    Cpu65816 cpu_{*this};
    SPpu ppu_;
    std::vector<uint8_t> rom_;
    uint8_t wram_[0x20000];
    uint8_t sram_[0x20000];        // up to 128 KB, masked by header size
    uint32_t sram_mask_ = 0x1FFF;
    uint8_t dma_[0x80];
    Mapping mapping_ = Mapping::LoROM;

    uint8_t nmitimen_ = 0, rdnmi_ = 0, timeup_ = 0, memsel_ = 0;
    uint8_t hdmaen_ = 0, hdma_term_ = 0, hdma_do_ = 0;
    bool in_vblank_ = false, irq_line_ = false;
    uint16_t htime_ = 0x1FF, vtime_ = 0x1FF;
    uint8_t wrmpya_ = 0xFF, wrmpyb_ = 0;
    uint16_t wrdiv_ = 0xFFFF;
    uint16_t rddiv_ = 0, rdmpy_ = 0;
    uint32_t wmadd_ = 0;           // $2181-83 WRAM data-port address (17-bit)
    uint16_t joy_[2] = {0, 0};     // auto-read results ($4218-421B)
    uint32_t buttons_[2] = {0, 0};
    bool joy_strobe_ = false;
    uint16_t joy_shift_[2] = {0, 0};
    int autojoy_busy_ = 0;         // lines remaining
    uint8_t open_bus_ = 0;
    int line_ = 0;
    uint64_t line_start_cyc_ = 0;

    // Audio subsystem + IPL transfer capture (see file header).
    Spc700 spc_;
    SDsp dsp_{spc_.aram};
    bool ipl_transfer_ = false;
    uint16_t ipl_addr_ = 0;
    uint8_t ipl_last_index_ = 0xFF;
    uint8_t ipl_ports_[4] = {0, 0, 0, 0};  // last main-CPU writes to $2140-43
    double sample_acc_ = 0.0;
    uint8_t spc_dbg_port0_ = 0;

    // Coprocessors (owned; null unless the header names them).
    Sa1* sa1_ = nullptr;
    SuperFx* sfx_ = nullptr;

    friend class Sa1;
    friend class SuperFx;

    static int score_header(const uint8_t* rom, size_t len, size_t hdr, bool hirom);
    void detect_mapping_and_chips();

    uint8_t rom_read(uint8_t bank7f, uint16_t off) const {
        size_t i;
        if (mapping_ == Mapping::HiROM) {
            i = (static_cast<size_t>(bank7f & 0x3F) << 16) | off;
        } else {
            i = static_cast<size_t>(bank7f) * 0x8000 + (off & 0x7FFF);
        }
        return rom_.empty() ? 0 : rom_[i % rom_.size()];
    }

    void latch_joypads();
    uint16_t pack_joy(uint32_t b) const;

    uint8_t apu_read(int port);
    void apu_write(int port, uint8_t v);

    void hdma_init();
    void hdma_load(int ch);
    void hdma_step();
    void run_dma(uint8_t enable);

    int ppu_dot() const {   // current dot within the line, from CPU progress
        const uint64_t d = (cpu_.cyc - line_start_cyc_) / 4;
        return d > 339 ? 339 : static_cast<int>(d);
    }

    void run_cpu_to(uint64_t target);

    template <class S>
    void serialize_all(S& s);
};

}  // namespace famemu::snes

#include "snes_system_impl.hpp"
