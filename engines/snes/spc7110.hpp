// famemu SNES engine — SPC7110 (Far East of Eden Zero, Momotarou Dentetsu
// Happy, Super Power League 4): the context-modeled arithmetic DECOMPRESSOR,
// data-ROM pointer/offset reads, ROM banking, and the RTC register interface
// (host-settable clock).
//
// The decompressor is a clean port of the public SPC7110 emulator by byuu &
// neviksti (2008, ISC-style permissive license — reproduced in
// spc7110_decomp.hpp), which reverse-engineered the chip. It is not a dumped
// ROM; the evolution/context tables and morton de-interleave are the
// documented algorithm. This closes the last engine gap in the chip lineup.
#pragma once

#include <cstdint>
#include <cstring>

#include "spc7110_decomp.hpp"

namespace famemu::snes {

class Spc7110 {
public:
    Spc7110() { reset(); }

    void reset() {
        std::memset(r_, 0, sizeof r_);
        r_[0x31] = 1; r_[0x32] = 2; r_[0x33] = 3;   // $4831-33 ROM banks
        rtc_index_ = 0;
        std::memset(rtc_, 0, sizeof rtc_);
        dr_adj_ = 0;
        dcu_.reset();
    }

    void set_rtc(const uint8_t* bcd15) { std::memcpy(rtc_, bcd15, 15); }

    uint8_t read_io(uint16_t off) {
        const uint8_t r = static_cast<uint8_t>(off - 0x4800);
        switch (off) {
            case 0x4800: {                             // decompression data port
                uint16_t counter = static_cast<uint16_t>(r_[0x09] | (r_[0x0A] << 8));
                --counter;
                r_[0x09] = counter & 0xFF; r_[0x0A] = counter >> 8;
                return dcu_.read();
            }
            case 0x480C: {                             // decomp status (bit7)
                const uint8_t s = r_[0x0C];
                r_[0x0C] &= 0x7F;
                return s;
            }
            case 0x4810: {                             // data-ROM read port
                const uint32_t base =
                    r_[0x11] | (r_[0x12] << 8) | (r_[0x13] << 16);
                const uint32_t ofs =
                    r_[0x14] | (r_[0x15] << 8) | (r_[0x16] << 16);
                const uint8_t v = data_rom_ ? data_rom_[(base + ofs + dr_adj_) %
                                                        (data_size_ ? data_size_ : 1)]
                                            : 0;
                ++dr_adj_;                              // auto-increment model
                return v;
            }
            case 0x4842: return 0x80;                  // RTC status: ready
            case 0x4841: {                             // RTC data (BCD digits)
                const uint8_t v = rtc_[rtc_index_ % 15];
                rtc_index_ = (rtc_index_ + 1) % 15;
                return v;
            }
            default:
                if (r < sizeof r_) return r_[r];
                return 0;
        }
    }

    void write_io(uint16_t off, uint8_t v) {
        const uint8_t r = static_cast<uint8_t>(off - 0x4800);
        if (off == 0x4840) { rtc_index_ = 0; return; } // RTC command/reset
        if (off >= 0x4811 && off <= 0x4817) dr_adj_ = 0;
        if (r < sizeof r_) r_[r] = v;
        if (off == 0x4806) trigger_decompression();
    }

    // Program-ROM banking for banks $D0-$FF ($4831-$4833 select 1MB slices
    // of the program area; bank $C0-CF is fixed to slice 0).
    uint32_t map_rom(uint8_t bank, uint16_t off) const {
        const int area = (bank >> 4) - 0x0C;           // 0-3
        const uint8_t slice = (area == 0) ? 0 : r_[0x30 + area];
        return (static_cast<uint32_t>(slice & 7) << 20) |
               (static_cast<uint32_t>(bank & 0x0F) << 16) | off;
    }

    void set_data_rom(const uint8_t* p, uint32_t n) {
        data_rom_ = p; data_size_ = n;
        dcu_.set_rom(p, n);
    }

    // Directly drive the decompressor (used by the unit test / tooling).
    void dcu_init(unsigned mode, unsigned offset, unsigned index) {
        dcu_.init(mode, offset, index);
    }
    uint8_t dcu_read() { return dcu_.read(); }

    template <class S>
    void serialize(S& s) {
        s.io(r_); s.io(rtc_); s.io(rtc_index_); s.io(dr_adj_);
        dcu_.serialize(s);
    }

private:
    uint8_t r_[0x60];
    uint8_t rtc_[15];       // BCD: sec lo/hi, min lo/hi, ... (host-settable)
    int rtc_index_ = 0;
    uint32_t dr_adj_ = 0;
    const uint8_t* data_rom_ = nullptr;
    uint32_t data_size_ = 0;
    Spc7110Decomp dcu_;

    // $4806 write: look up the directory entry (mode + offset) in the data
    // ROM and (re)start the decompression unit at the requested output index.
    void trigger_decompression() {
        const uint32_t table = r_[0x01] | (r_[0x02] << 8) | (r_[0x03] << 16);
        const uint32_t index = static_cast<uint32_t>(r_[0x04]) << 2;
        const uint32_t addr = dcu_.datarom_addr(table + index);
        const unsigned mode = dcu_.rom_at(addr);
        const unsigned offset = (dcu_.rom_at(addr + 1) << 16) |
                                (dcu_.rom_at(addr + 2) << 8) |
                                (dcu_.rom_at(addr + 3));
        const unsigned target = static_cast<unsigned>(r_[0x05] | (r_[0x06] << 8)) << mode;
        dcu_.init(mode, offset, target);
        r_[0x0C] = 0x80;                                // status: decoded
    }
};

}  // namespace famemu::snes
