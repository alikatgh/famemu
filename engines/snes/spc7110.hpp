// famemu SNES engine — SPC7110 (Far East of Eden Zero, Momotarou Dentetsu
// Happy, Super Power League 4): data-ROM pointer/offset reads, ROM banking,
// and the RTC register interface (host-settable clock). The context-modeled
// decompressor is NOT implemented yet — reads of the decompression port log
// once and return zero, which is the one documented gap left in the chip
// lineup (three Japan-only games).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace famemu::snes {

class Spc7110 {
public:
    Spc7110() { reset(); }

    void reset() {
        std::memset(r_, 0, sizeof r_);
        r_[0x31] = 1; r_[0x32] = 2; r_[0x33] = 3;   // $4831-33 ROM banks
        rtc_index_ = 0;
        std::memset(rtc_, 0, sizeof rtc_);
    }

    void set_rtc(const uint8_t* bcd15) { std::memcpy(rtc_, bcd15, 15); }

    uint8_t read_io(uint16_t off) {
        const uint8_t r = static_cast<uint8_t>(off - 0x4800);
        switch (off) {
            case 0x4800:  // decompression data port
                log_once("decompression port read");
                return 0;
            case 0x480C: return 0x80;                  // decomp status: done
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
        if (off == 0x4801 || off == 0x4802 || off == 0x4803)
            log_once("decompression start");
    }

    // Program-ROM banking for banks $D0-$FF ($4831-$4833 select 1MB slices
    // of the program area; bank $C0-CF is fixed to slice 0).
    uint32_t map_rom(uint8_t bank, uint16_t off) const {
        const int area = (bank >> 4) - 0x0C;           // 0-3
        const uint8_t slice = (area == 0) ? 0 : r_[0x30 + area];
        return (static_cast<uint32_t>(slice & 7) << 20) |
               (static_cast<uint32_t>(bank & 0x0F) << 16) | off;
    }

    void set_data_rom(const uint8_t* p, uint32_t n) { data_rom_ = p; data_size_ = n; }

    template <class S>
    void serialize(S& s) {
        s.io(r_); s.io(rtc_); s.io(rtc_index_); s.io(dr_adj_);
    }

private:
    uint8_t r_[0x60];
    uint8_t rtc_[15];       // BCD: sec lo/hi, min lo/hi, ... (host-settable)
    int rtc_index_ = 0;
    uint32_t dr_adj_ = 0;
    const uint8_t* data_rom_ = nullptr;
    uint32_t data_size_ = 0;

    static void log_once(const char* what) {
        static int n = 0;
        if (n < 4) { std::fprintf(stderr, "spc7110: unimplemented %s\n", what); ++n; }
    }
};

}  // namespace famemu::snes
