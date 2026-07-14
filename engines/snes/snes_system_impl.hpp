// famemu SNES engine — SnesSystem method bodies (split from snes_system.hpp
// for readability; this file is only included from there).
#pragma once

#include "sa1.hpp"
#include "superfx.hpp"

namespace famemu::snes {

inline SnesSystem::SnesSystem() : cpu_(*this) {}

inline SnesSystem::~SnesSystem() {
    delete sa1_;
    delete sfx_;
}

// Score a candidate header location: checksum pair, map-mode nibble matching
// the location, and a sane reset vector. Highest score wins (snes9x-style).
inline int SnesSystem::score_header(const uint8_t* rom, size_t len, size_t hdr,
                                    bool hirom) {
    if (len < hdr + 0x40) return -1;
    const uint8_t* h = rom + hdr;                       // $xxC0
    int score = 0;
    const uint16_t cmpl = static_cast<uint16_t>(h[0x1C] | (h[0x1D] << 8));
    const uint16_t csum = static_cast<uint16_t>(h[0x1E] | (h[0x1F] << 8));
    if ((cmpl ^ csum) == 0xFFFF && csum != 0) score += 4;
    const uint8_t map = h[0x15];
    if ((map & 0x0F) == (hirom ? 1 : 0)) score += 2;
    if ((map & 0x0F) == 3 && !hirom) score += 2;        // SA-1 carts header at $7FC0
    const uint16_t reset = static_cast<uint16_t>(h[0x3C] | (h[0x3D] << 8));
    if (reset >= 0x8000) score += 1;
    // Title bytes should be printable-ish.
    int printable = 0;
    for (int i = 0; i < 21; ++i)
        if (h[i] == 0x20 || (h[i] >= 0x20 && h[i] < 0x7F)) ++printable;
    if (printable >= 18) score += 1;
    return score;
}

inline void SnesSystem::detect_mapping_and_chips() {
    delete sa1_; sa1_ = nullptr;
    delete sfx_; sfx_ = nullptr;
    const int lo = score_header(rom_.data(), rom_.size(), 0x7FC0, false);
    const int hi = score_header(rom_.data(), rom_.size(), 0xFFC0, true);
    mapping_ = (hi > lo) ? Mapping::HiROM : Mapping::LoROM;
    const size_t hdr = (mapping_ == Mapping::HiROM) ? 0xFFC0 : 0x7FC0;
    if (rom_.size() < hdr + 0x40) { sram_mask_ = 0x1FFF; return; }
    const uint8_t map = rom_[hdr + 0x15];
    const uint8_t chip = rom_[hdr + 0x16];
    uint8_t sram_kb_log = rom_[hdr + 0x18];
    uint32_t sram_bytes = sram_kb_log ? (1024u << sram_kb_log) : 0;
    if ((map & 0x0F) == 3 || chip == 0x34 || chip == 0x35) {
        sa1_ = new Sa1(*this);
        if (sram_bytes < 0x8000) sram_bytes = 0x8000;   // BW-RAM is >= 32 KB
    } else if (chip >= 0x13 && chip <= 0x1A) {
        sfx_ = new SuperFx(*this);
        // Expansion-RAM size byte lives at $xxBD on SuperFX carts.
        const uint8_t xram = rom_[hdr - 0x10 + 0x0D];
        sram_bytes = xram ? (1024u << xram) : 0x10000;  // default 64 KB
    }
    if (sram_bytes > sizeof sram_) sram_bytes = sizeof sram_;
    sram_mask_ = sram_bytes ? (sram_bytes - 1) : 0;     // 0 = no SRAM
}

inline bool SnesSystem::load_rom(const uint8_t* data, size_t len) {
    if (len < 0x8000) return false;
    if (len % 0x8000 == 512) { data += 512; len -= 512; }  // copier header
    rom_.assign(data, data + len);
    detect_mapping_and_chips();
    power_on();
    return true;
}

inline void SnesSystem::power_on() {
    std::memset(wram_, 0, sizeof wram_);
    std::memset(sram_, 0, sizeof sram_);
    std::memset(dma_, 0, sizeof dma_);
    ppu_.reset();
    nmitimen_ = 0;
    rdnmi_ = timeup_ = memsel_ = 0;
    hdmaen_ = hdma_term_ = hdma_do_ = 0;
    in_vblank_ = false;
    irq_line_ = false;
    htime_ = vtime_ = 0x1FF;
    wrmpya_ = 0xFF; wrmpyb_ = 0;
    wrdiv_ = 0xFFFF;
    rddiv_ = rdmpy_ = 0;
    wmadd_ = 0;
    joy_[0] = joy_[1] = 0;
    buttons_[0] = buttons_[1] = 0;
    joy_strobe_ = false;
    joy_shift_[0] = joy_shift_[1] = 0;
    autojoy_busy_ = 0;
    open_bus_ = 0;
    spc_.reset();
    dsp_.reset();
    spc_.set_dsp(&dsp_);
    ipl_transfer_ = false;
    ipl_addr_ = 0;
    ipl_last_index_ = 0xFF;
    ipl_ports_[0] = ipl_ports_[1] = ipl_ports_[2] = ipl_ports_[3] = 0;
    sample_acc_ = 0.0;
    line_ = 0;
    if (sa1_) sa1_->reset();
    if (sfx_) sfx_->reset();
    cpu_.reset();
    line_start_cyc_ = cpu_.cyc;
}

inline void SnesSystem::run_cpu_to(uint64_t target) {
    while (cpu_.cyc < target && !cpu_.stopped) {
        if (irq_line_) cpu_.irq();          // level-held; no-op while I set
        if (cpu_.waiting) { cpu_.cyc = target; return; }
        cpu_.step();
    }
    if (cpu_.stopped && cpu_.cyc < target) cpu_.cyc = target;
}

inline void SnesSystem::run_frame() {
    const int vblank_start = ppu_.overscan() ? 240 : 225;
    const uint64_t frame_start = cpu_.cyc;
    for (line_ = 0; line_ < kLinesPerFrame; ++line_) {
        line_start_cyc_ = cpu_.cyc;
        const uint64_t line_end =
            frame_start + static_cast<uint64_t>(line_ + 1) * kLineCycles;
        if (line_ == 0) {
            in_vblank_ = false;
            rdnmi_ &= 0x7F;
            ppu_.frame_start();
            // Init only: the first transfer lands with fb row 0's step
            // below. (snes9x's visible-row mapping: row y = entry line
            // y+1 of the table; hardware's extra scanline-0 slot is not
            // modeled — revisit if we ever golden against hardware.)
            hdma_init();
        } else if (line_ == vblank_start) {
            in_vblank_ = true;
            rdnmi_ |= 0x80;
            ppu_.vblank_begin();
            if (nmitimen_ & 0x01) { latch_joypads(); autojoy_busy_ = 3; }
            if (nmitimen_ & 0x80) cpu_.nmi();
        }
        if (autojoy_busy_ > 0) --autojoy_busy_;
        cpu_.cyc += 40;                     // WRAM refresh dead time

        // H/V IRQ timers ($4200 bits 4/5, $4207-$420A).
        const bool hirq = nmitimen_ & 0x10, virq = nmitimen_ & 0x20;
        uint64_t irq_at = UINT64_MAX;
        if (virq && !hirq) {
            if (line_ == (vtime_ & 0x1FF)) irq_at = line_start_cyc_ + 10;
        } else if (hirq && (!virq || line_ == (vtime_ & 0x1FF))) {
            irq_at = line_start_cyc_ + 14 +
                     static_cast<uint64_t>(htime_ & 0x1FF) * 4;
        }
        if (irq_at != UINT64_MAX && irq_at < line_end) {
            run_cpu_to(irq_at);
            timeup_ |= 0x80;
            irq_line_ = true;
        }
        run_cpu_to(line_end);

        if (sa1_) sa1_->run_line();
        if (sfx_) sfx_->run_line();
        spc_.run(65);  // ~1.024 MHz / 15734 lines
        if (line_ < SPpu::kHeight) {
            hdma_step();       // fb row y = scanline y+1
            ppu_.render_line(line_);
        }
    }
    // 32 kHz DSP output: 32000 / 60.0988 fps ≈ 532.5 samples per frame.
    sample_acc_ += 32000.0 / 60.0988;
    const int n = static_cast<int>(sample_acc_);
    sample_acc_ -= n;
    dsp_.render(n);
}

inline uint16_t SnesSystem::pack_joy(uint32_t b) const {
    // FamemuCoreButton -> SNES JOY1: hi = B Y Sel St U D L R, lo = A X L R 0000.
    uint16_t j = 0;
    if (b & (1u << 1)) j |= 0x8000;   // B
    if (b & (1u << 9)) j |= 0x4000;   // Y
    if (b & (1u << 2)) j |= 0x2000;   // Select
    if (b & (1u << 3)) j |= 0x1000;   // Start
    if (b & (1u << 4)) j |= 0x0800;   // Up
    if (b & (1u << 5)) j |= 0x0400;   // Down
    if (b & (1u << 6)) j |= 0x0200;   // Left
    if (b & (1u << 7)) j |= 0x0100;   // Right
    if (b & (1u << 0)) j |= 0x0080;   // A
    if (b & (1u << 8)) j |= 0x0040;   // X
    if (b & (1u << 10)) j |= 0x0020;  // L
    if (b & (1u << 11)) j |= 0x0010;  // R
    return j;
}

inline void SnesSystem::latch_joypads() {
    joy_[0] = pack_joy(buttons_[0]);
    joy_[1] = pack_joy(buttons_[1]);
    joy_shift_[0] = joy_[0];
    joy_shift_[1] = joy_[1];
}

inline uint8_t SnesSystem::read(uint32_t a24) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    if (bank == 0x7E) return open_bus_ = wram_[off];
    if (bank == 0x7F) return open_bus_ = wram_[0x10000 + off];

    // Coprocessors get first crack (MMC-mapped ROM, IRAM, BW-RAM, GSU regs).
    if (sa1_) { const int r = sa1_->snes_read(a24); if (r >= 0) return open_bus_ = static_cast<uint8_t>(r); }
    if (sfx_) { const int r = sfx_->snes_read(a24); if (r >= 0) return open_bus_ = static_cast<uint8_t>(r); }

    const uint8_t b = bank & 0x7F;
    if (b <= 0x3F) {
        if (off < 0x2000) return open_bus_ = wram_[off];
        if (off >= 0x2140 && off <= 0x217F) return open_bus_ = apu_read((off - 0x2140) & 3);
        if (off == 0x2137) {   // SLHV: latch the beam position, open-bus data
            ppu_.latch_counters(ppu_dot(), line_);
            return open_bus_;
        }
        if (off >= 0x2100 && off < 0x2140)
            return open_bus_ = ppu_.read(off & 0xFF, open_bus_);
        if (off == 0x2180) {
            const uint8_t v = wram_[wmadd_];
            wmadd_ = (wmadd_ + 1) & 0x1FFFF;
            return open_bus_ = v;
        }
        if (off == 0x4016 || off == 0x4017) {
            const int pad = off & 1;
            uint8_t bit;
            if (joy_strobe_) {
                bit = static_cast<uint8_t>(pack_joy(buttons_[pad]) >> 15);
            } else {
                bit = (joy_shift_[pad] & 0x8000) ? 1 : 0;
                joy_shift_[pad] = static_cast<uint16_t>((joy_shift_[pad] << 1) | 1);
            }
            return open_bus_ = static_cast<uint8_t>(bit | (pad ? 0x1C : 0));
        }
        if (off >= 0x4200 && off < 0x4380) {
            switch (off) {
                case 0x4210: { const uint8_t r = static_cast<uint8_t>(rdnmi_ | 0x02); rdnmi_ &= 0x7F; return open_bus_ = r; }
                case 0x4211: { const uint8_t r = timeup_; timeup_ = 0; irq_line_ = false; return open_bus_ = r; }
                case 0x4212: {
                    const uint64_t pos = cpu_.cyc - line_start_cyc_;
                    uint8_t r = 0;
                    if (in_vblank_) r |= 0x80;
                    if (pos >= 1096 || pos < 40) r |= 0x40;   // hblank
                    if (autojoy_busy_ > 0) r |= 0x01;
                    return open_bus_ = r;
                }
                case 0x4214: return open_bus_ = static_cast<uint8_t>(rddiv_);
                case 0x4215: return open_bus_ = static_cast<uint8_t>(rddiv_ >> 8);
                case 0x4216: return open_bus_ = static_cast<uint8_t>(rdmpy_);
                case 0x4217: return open_bus_ = static_cast<uint8_t>(rdmpy_ >> 8);
                case 0x4218: return open_bus_ = static_cast<uint8_t>(joy_[0]);
                case 0x4219: return open_bus_ = static_cast<uint8_t>(joy_[0] >> 8);
                case 0x421A: return open_bus_ = static_cast<uint8_t>(joy_[1]);
                case 0x421B: return open_bus_ = static_cast<uint8_t>(joy_[1] >> 8);
                case 0x421C: case 0x421D: case 0x421E: case 0x421F: return open_bus_ = 0;
                default:
                    if (off >= 0x4300) return open_bus_ = dma_[(off - 0x4300) & 0x7F];
                    return open_bus_;
            }
        }
        if (off >= 0x8000) return open_bus_ = rom_read(b, off);
        if (mapping_ == Mapping::HiROM && b >= 0x20 && off >= 0x6000 && sram_mask_)
            return open_bus_ = sram_[(((b & 0x1F) * 0x2000) + (off - 0x6000)) & sram_mask_];
        return open_bus_;
    }
    if (mapping_ == Mapping::LoROM) {
        if (b >= 0x70 && b <= 0x7D && off < 0x8000 && sram_mask_)
            return open_bus_ = sram_[(((b - 0x70) * 0x8000) + off) & sram_mask_];
        return open_bus_ = rom_read(b, off >= 0x8000 ? off
                                                     : static_cast<uint16_t>(off | 0x8000));
    }
    // HiROM: banks 40-7D / C0-FF map the ROM linearly.
    return open_bus_ = rom_read(b, off);
}

inline void SnesSystem::write(uint32_t a24, uint8_t v) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    open_bus_ = v;
    if (bank == 0x7E) { wram_[off] = v; return; }
    if (bank == 0x7F) { wram_[0x10000 + off] = v; return; }

    if (sa1_ && sa1_->snes_write(a24, v)) return;
    if (sfx_ && sfx_->snes_write(a24, v)) return;

    const uint8_t b = bank & 0x7F;
    if (b <= 0x3F) {
        if (off < 0x2000) { wram_[off] = v; return; }
        if (off >= 0x2140 && off <= 0x217F) { apu_write((off - 0x2140) & 3, v); return; }
        if (off >= 0x2100 && off < 0x2140) { ppu_.write(off & 0xFF, v); return; }
        switch (off) {
            case 0x2180:
                wram_[wmadd_] = v;
                wmadd_ = (wmadd_ + 1) & 0x1FFFF;
                return;
            case 0x2181: wmadd_ = (wmadd_ & 0x1FF00) | v; return;
            case 0x2182: wmadd_ = (wmadd_ & 0x100FF) | (static_cast<uint32_t>(v) << 8); return;
            case 0x2183: wmadd_ = (wmadd_ & 0x0FFFF) | (static_cast<uint32_t>(v & 1) << 16); return;
            case 0x4016: {
                const bool s = v & 1;
                if (joy_strobe_ && !s) {
                    joy_shift_[0] = pack_joy(buttons_[0]);
                    joy_shift_[1] = pack_joy(buttons_[1]);
                }
                joy_strobe_ = s;
                return;
            }
            case 0x4200:
                if ((v & 0x80) && !(nmitimen_ & 0x80) && (rdnmi_ & 0x80)) cpu_.nmi();
                nmitimen_ = v;
                return;
            case 0x4202: wrmpya_ = v; return;
            case 0x4203: wrmpyb_ = v; rdmpy_ = static_cast<uint16_t>(wrmpya_ * v); return;
            case 0x4204: wrdiv_ = (wrdiv_ & 0xFF00) | v; return;
            case 0x4205: wrdiv_ = static_cast<uint16_t>((v << 8) | (wrdiv_ & 0xFF)); return;
            case 0x4206:
                if (v) { rddiv_ = wrdiv_ / v; rdmpy_ = wrdiv_ % v; }
                else   { rddiv_ = 0xFFFF; rdmpy_ = wrdiv_; }
                return;
            case 0x4207: htime_ = (htime_ & 0x100) | v; return;
            case 0x4208: htime_ = static_cast<uint16_t>(((v & 1) << 8) | (htime_ & 0xFF)); return;
            case 0x4209: vtime_ = (vtime_ & 0x100) | v; return;
            case 0x420A: vtime_ = static_cast<uint16_t>(((v & 1) << 8) | (vtime_ & 0xFF)); return;
            case 0x420B: run_dma(v); return;
            case 0x420C: hdmaen_ = v; return;
            case 0x420D: memsel_ = v; return;
            default:
                if (off >= 0x4300 && off < 0x4380) { dma_[off - 0x4300] = v; return; }
                if (mapping_ == Mapping::HiROM && b >= 0x20 && off >= 0x6000 &&
                    off < 0x8000 && sram_mask_) {
                    sram_[(((b & 0x1F) * 0x2000) + (off - 0x6000)) & sram_mask_] = v;
                }
                return;
        }
    }
    if (mapping_ == Mapping::LoROM && b >= 0x70 && b <= 0x7D && off < 0x8000 &&
        sram_mask_)
        sram_[(((b - 0x70) * 0x8000) + off) & sram_mask_] = v;
}

inline uint8_t SnesSystem::apu_read(int port) {
    if (spc_.is_running()) return spc_.main_read_port(port);
    // IPL ROM boot state / transfer echo.
    if (!ipl_transfer_) return port == 0 ? 0xAA : (port == 1 ? 0xBB : 0);
    return ipl_ports_[port];  // echoes written below
}

inline void SnesSystem::apu_write(int port, uint8_t v) {
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

// ---- HDMA: per-scanline table-driven transfers ($420C + $43xx) --------
// Table format per entry: line-count byte (bit7 = repeat), then either
// the unit data inline (direct) or a 2-byte pointer (indirect, DMAP bit6).
inline void SnesSystem::hdma_init() {
    hdma_term_ = hdma_do_ = 0;
    for (int ch = 0; ch < 8; ++ch) {
        if (!(hdmaen_ & (1 << ch))) continue;
        uint8_t* r = &dma_[ch * 0x10];
        r[8] = r[2]; r[9] = r[3];          // A2A = table base A1T
        hdma_load(ch);
        cpu_.cyc += 18;
    }
}

inline void SnesSystem::hdma_load(int ch) {   // fetch the next table entry
    uint8_t* r = &dma_[ch * 0x10];
    const uint32_t bank = static_cast<uint32_t>(r[4]) << 16;
    uint16_t a2a = static_cast<uint16_t>(r[8] | (r[9] << 8));
    const uint8_t cnt = read(bank | a2a++);
    r[0xA] = cnt;
    if (cnt == 0) {
        hdma_term_ |= static_cast<uint8_t>(1 << ch);
    } else {
        if (r[0] & 0x40) {                 // indirect: fetch data pointer
            r[5] = read(bank | a2a++);
            r[6] = read(bank | a2a++);
        }
        hdma_do_ |= static_cast<uint8_t>(1 << ch);
    }
    r[8] = a2a & 0xFF; r[9] = a2a >> 8;
}

inline void SnesSystem::hdma_step() {          // one scanline for all channels
    static const uint8_t kOff[8][4] = {{0, 0, 0, 0}, {0, 1, 0, 0},
                                       {0, 0, 0, 0}, {0, 0, 1, 1},
                                       {0, 1, 2, 3}, {0, 1, 0, 1},
                                       {0, 0, 0, 0}, {0, 0, 1, 1}};
    static const int kLen[8] = {1, 2, 2, 4, 4, 4, 2, 4};
    for (int ch = 0; ch < 8; ++ch) {
        const uint8_t bit = static_cast<uint8_t>(1 << ch);
        if (!(hdmaen_ & bit) || (hdma_term_ & bit)) continue;
        uint8_t* r = &dma_[ch * 0x10];
        if (hdma_do_ & bit) {
            const int mode = r[0] & 7;
            const bool ind = r[0] & 0x40;
            const uint32_t bank = static_cast<uint32_t>(ind ? r[7] : r[4]) << 16;
            uint16_t a = ind ? static_cast<uint16_t>(r[5] | (r[6] << 8))
                             : static_cast<uint16_t>(r[8] | (r[9] << 8));
            for (int i = 0; i < kLen[mode]; ++i)
                write(0x2100u | static_cast<uint8_t>(r[1] + kOff[mode][i]),
                      read(bank | a++));
            cpu_.cyc += 8u * kLen[mode];
            if (ind) { r[5] = a & 0xFF; r[6] = a >> 8; }
            else     { r[8] = a & 0xFF; r[9] = a >> 8; }
        }
        cpu_.cyc += 8;
        const uint8_t cnt = --r[0xA];
        if ((cnt & 0x7F) == 0) {
            hdma_load(ch);                 // sets do/terminated
        } else if (cnt & 0x80) {
            hdma_do_ |= bit;               // repeat: transfer every line
        } else {
            hdma_do_ &= static_cast<uint8_t>(~bit);
        }
    }
}

inline void SnesSystem::run_dma(uint8_t enable) {
    cpu_.cyc += 12;                         // DMA-engine sync overhead
    for (int ch = 0; ch < 8; ++ch) {
        if (!(enable & (1 << ch))) continue;
        cpu_.cyc += 8;
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
        cpu_.cyc += 8u * count;             // 8 master cycles per byte
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
            if (ctrl & 0x80) {  // B -> A
                write(aaddr, read(0x2100u | static_cast<uint8_t>(breg + breg_off)));
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

inline size_t SnesSystem::state_size() const {
    return 8 + 64 + sizeof wram_ + sizeof sram_ + sizeof dma_ + 128 +
           sizeof(SPpu) + sizeof(Spc700) + sizeof(SDsp) +
           (sa1_ ? Sa1::kStateBytes : 0) + (sfx_ ? SuperFx::kStateBytes : 0) +
           256;
}

inline bool SnesSystem::state_save(uint8_t* buf, size_t len) {
    famemu::nes::StateWriter w{buf, len};
    w.io(kStateMagic);
    serialize_all(w);
    return w.ok;
}

inline bool SnesSystem::state_load(const uint8_t* buf, size_t len) {
    famemu::nes::StateReader r{buf, len};
    uint32_t magic = 0;
    r.io(magic);
    if (magic != kStateMagic) return false;
    serialize_all(r);
    if (r.ok) dsp_.post_load();
    return r.ok;
}

template <class S>
inline void SnesSystem::serialize_all(S& s) {
    s.io(cpu_.a); s.io(cpu_.x); s.io(cpu_.y); s.io(cpu_.s); s.io(cpu_.d);
    s.io(cpu_.dbr); s.io(cpu_.pbr); s.io(cpu_.pc); s.io(cpu_.p);
    s.io(cpu_.e); s.io(cpu_.cyc); s.io(cpu_.waiting); s.io(cpu_.stopped);
    s.io(wram_); s.io(sram_); s.io(dma_);
    s.io(nmitimen_); s.io(rdnmi_); s.io(timeup_); s.io(memsel_);
    s.io(in_vblank_); s.io(irq_line_);
    s.io(htime_); s.io(vtime_);
    s.io(wrmpya_); s.io(wrmpyb_); s.io(wrdiv_); s.io(rddiv_); s.io(rdmpy_);
    s.io(wmadd_);
    s.io(joy_); s.io(joy_shift_); s.io(joy_strobe_); s.io(autojoy_busy_);
    s.io(open_bus_);
    s.io(hdmaen_); s.io(hdma_term_); s.io(hdma_do_);
    s.io(buttons_); s.io(line_); s.io(line_start_cyc_);
    s.io(ipl_transfer_); s.io(ipl_addr_); s.io(ipl_last_index_);
    s.io(ipl_ports_); s.io(sample_acc_);
    ppu_.serialize(s);
    spc_.serialize(s);
    dsp_.serialize(s);
    if (sa1_) sa1_->serialize(s);
    if (sfx_) sfx_->serialize(s);
}

}  // namespace famemu::snes

#include "sa1_impl.hpp"
#include "superfx_impl.hpp"
