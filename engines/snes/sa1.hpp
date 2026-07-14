// famemu SNES engine — SA-1 coprocessor: a second 65816 (~10.74 MHz) on its
// own bus with 2 KB IRAM, BW-RAM (cart RAM), Super-MMC ROM banking shared
// with the S-CPU, mailbox/IRQ ports, the arithmetic unit (mul/div/
// cumulative), normal DMA, and variable-length bit reads.
//
// Clean-room from public documentation (fullsnes). Not yet implemented:
// character-conversion DMA types 1/2, H/V timers, bitmap-projected BW-RAM
// views (banks 50-6F) — each logs once on first use.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cpu65816.hpp"

namespace famemu::snes {

class SnesSystem;

class Sa1 : public Bus16 {
public:
    static constexpr size_t kStateBytes = 0x800 + 256 + 64;

    explicit Sa1(SnesSystem& sys) : sys_(sys), cpu_(*this) {}

    void reset() {
        std::memset(iram_, 0, sizeof iram_);
        std::memset(io_, 0, sizeof io_);
        ccnt_ = 0x20;              // SA-1 held in reset until the game starts it
        sie_ = sic_ = scnt_ = cie_ = cic_ = 0;
        crv_ = cnv_ = civ_ = 0;
        mmc_[0] = 0; mmc_[1] = 1; mmc_[2] = 2; mmc_[3] = 3;
        bmaps_ = bmap_ = 0;
        mcnt_ = 0;
        ma_ = mb_ = 0;
        mr_ = 0;
        overflow_ = false;
        dcnt_ = cdma_ = 0;
        sda_ = dda_ = 0;
        dtc_ = 0;
        vbd_ = 0x10;
        vda_ = 0;
        vbit_ = 0;
        sfr_flags_ = cfr_flags_ = 0;
        running_ = false;
        cpu_.reset();               // vectors read via this bus (CRV etc.)
        cpu_.stopped = true;        // held until CCNT bit5 clears
    }

    // ---- S-CPU visibility -------------------------------------------------
    // Returns -1 when the address is not SA-1-mapped (caller falls through).
    int snes_read(uint32_t a24);
    bool snes_write(uint32_t a24, uint8_t v);
    bool scpu_irq_pending() const {
        return ((sfr_flags_ & 0x80) && (sie_ & 0x80)) ||
               ((sfr_flags_ & 0x20) && (sie_ & 0x20));
    }

    void run_line();

    // ---- SA-1 side bus (Bus16) ---------------------------------------------
    uint8_t read(uint32_t a24) override;
    void write(uint32_t a24, uint8_t v) override;
    int speed(uint32_t) const override { return 2; }  // ~10.74 MHz

    template <class S>
    void serialize(S& s) {
        s.io(iram_);
        s.io(cpu_.a); s.io(cpu_.x); s.io(cpu_.y); s.io(cpu_.s); s.io(cpu_.d);
        s.io(cpu_.dbr); s.io(cpu_.pbr); s.io(cpu_.pc); s.io(cpu_.p);
        s.io(cpu_.e); s.io(cpu_.cyc); s.io(cpu_.waiting); s.io(cpu_.stopped);
        s.io(ccnt_); s.io(sie_); s.io(sic_); s.io(scnt_); s.io(cie_); s.io(cic_);
        s.io(crv_); s.io(cnv_); s.io(civ_);
        s.io(mmc_); s.io(bmaps_); s.io(bmap_);
        s.io(mcnt_); s.io(ma_); s.io(mb_); s.io(mr_); s.io(overflow_);
        s.io(dcnt_); s.io(cdma_); s.io(sda_); s.io(dda_); s.io(dtc_);
        s.io(vbd_); s.io(vda_); s.io(vbit_);
        s.io(sfr_flags_); s.io(cfr_flags_); s.io(running_);
    }

private:
    SnesSystem& sys_;
    Cpu65816 cpu_;
    uint8_t iram_[0x800];
    uint8_t io_[16];               // scratch for unmodeled registers
    uint8_t ccnt_, sie_, sic_, scnt_, cie_, cic_;
    uint16_t crv_, cnv_, civ_;
    uint8_t mmc_[4];               // $2220-23 CXB/DXB/EXB/FXB
    uint8_t bmaps_, bmap_;         // $2224/$2225
    uint8_t mcnt_;
    uint16_t ma_, mb_;
    uint64_t mr_;                  // 40-bit arithmetic result
    bool overflow_;
    uint8_t dcnt_, cdma_;
    uint32_t sda_, dda_;
    uint16_t dtc_;
    uint8_t vbd_;
    uint32_t vda_;
    uint32_t vbit_;
    uint8_t sfr_flags_;            // S-CPU-visible IRQ flags ($2300)
    uint8_t cfr_flags_;            // SA-1-visible IRQ flags ($2301)
    bool running_;

    static void log_once(const char* what) {
        static int logged = 0;
        if (logged < 8) { std::fprintf(stderr, "sa1: unimplemented %s\n", what); ++logged; }
    }

    uint8_t rom_byte(uint32_t linear) const;  // via SnesSystem (defined below)
    uint8_t* bwram() const;
    uint32_t bwram_mask() const;

    // Super-MMC: ROM as seen by both CPUs.
    uint8_t mmc_rom(uint8_t bank, uint16_t off) const {
        if (bank >= 0xC0) {                             // direct 1MB areas
            const int slot = (bank >> 4) - 0x0C;
            const uint32_t base = static_cast<uint32_t>(mmc_[slot] & 7) << 20;
            return rom_byte(base + ((static_cast<uint32_t>(bank & 0x0F) << 16) | off));
        }
        // LoROM projection: banks 00-1F/20-3F/80-9F/A0-BF, $8000-FFFF.
        const uint8_t b7 = bank & 0x7F;
        const int slot = (bank & 0x80) ? ((b7 >= 0x20) ? 3 : 2)
                                       : ((b7 >= 0x20) ? 1 : 0);
        const uint32_t bank1m = (mmc_[slot] & 0x80)
            ? static_cast<uint32_t>(mmc_[slot] & 7)
            : static_cast<uint32_t>(slot);
        const uint32_t off1m =
            (static_cast<uint32_t>(b7 & 0x1F) << 15) | (off & 0x7FFF);
        return rom_byte((bank1m << 20) + off1m);
    }

    void arith_start_mul_div() {
        const uint8_t op = mcnt_ & 3;
        if (op == 0) {                                  // signed multiply
            mr_ = static_cast<uint64_t>(static_cast<int64_t>(
                static_cast<int16_t>(ma_) * static_cast<int16_t>(mb_))) &
                  0xFFFFFFFFFFull;
            mb_ = 0;
        } else if (op == 1) {                           // divide (floor, r >= 0)
            if (mb_ == 0) { mr_ = 0; }
            else {
                const int32_t num = static_cast<int16_t>(ma_);
                const int32_t den = mb_;                // divisor is unsigned
                int32_t q = num / den, r = num % den;
                if (r < 0) { r += den; q -= 1; }
                mr_ = (static_cast<uint32_t>(q) & 0xFFFF) |
                      (static_cast<uint64_t>(static_cast<uint32_t>(r) & 0xFFFF) << 16);
            }
            ma_ = mb_ = 0;
        } else {                                        // cumulative sum
            const int64_t add = static_cast<int64_t>(static_cast<int16_t>(ma_)) *
                                static_cast<int16_t>(mb_);
            uint64_t sum = (mr_ + static_cast<uint64_t>(add)) & 0xFFFFFFFFFFull;
            overflow_ = sum >> 39;                       // coarse OF
            mr_ = sum;
            mb_ = 0;
        }
    }

    void do_dma() {
        const bool src_rom = (dcnt_ & 3) == 0;
        const bool src_bw = (dcnt_ & 3) == 1;
        const bool dst_bw = dcnt_ & 0x04;
        if (dcnt_ & 0x20) { log_once("character-conversion DMA"); return; }
        for (uint32_t i = 0; i < dtc_; ++i) {
            uint8_t v;
            const uint32_t s = sda_ + i;
            if (src_rom) v = mmc_rom(static_cast<uint8_t>(s >> 16),
                                     static_cast<uint16_t>(s));
            else if (src_bw) v = bwram()[s & bwram_mask()];
            else v = iram_[s & 0x7FF];
            const uint32_t d = dda_ + i;
            if (dst_bw) bwram()[d & bwram_mask()] = v;
            else iram_[d & 0x7FF] = v;
        }
        sfr_flags_ |= 0x20;                              // DMA done flag
        cfr_flags_ |= 0x20;
    }

    uint8_t vbr_byte(uint32_t a) const {
        return mmc_rom(static_cast<uint8_t>(a >> 16), static_cast<uint16_t>(a));
    }
    uint16_t vbr_window() const {                        // 16 bits at vbit_
        const uint32_t bitpos = vbit_;
        const uint32_t byte = vda_ + (bitpos >> 3);
        const uint32_t b0 = vbr_byte(byte), b1 = vbr_byte(byte + 1),
                       b2 = vbr_byte(byte + 2);
        const uint32_t w = b0 | (b1 << 8) | (b2 << 16);
        return static_cast<uint16_t>(w >> (bitpos & 7));
    }

    uint8_t io_read_scpu(uint16_t off) {
        switch (off) {
            case 0x2300:  // SFR: IRQ-from-SA-1 flag + message + DMA flag
                return static_cast<uint8_t>((sfr_flags_ & 0xA0) | (scnt_ & 0x0F));
            case 0x2301:
                return static_cast<uint8_t>((cfr_flags_ & 0xB0) | (ccnt_ & 0x0F));
            case 0x2306: return static_cast<uint8_t>(mr_);
            case 0x2307: return static_cast<uint8_t>(mr_ >> 8);
            case 0x2308: return static_cast<uint8_t>(mr_ >> 16);
            case 0x2309: return static_cast<uint8_t>(mr_ >> 24);
            case 0x230A: return static_cast<uint8_t>(mr_ >> 32);
            case 0x230B: return overflow_ ? 0x80 : 0x00;
            case 0x230C: return static_cast<uint8_t>(vbr_window());
            case 0x230D: {
                const uint8_t v = static_cast<uint8_t>(vbr_window() >> 8);
                if (vbd_ & 0x80) {                       // auto mode: advance
                    const uint32_t len = (vbd_ & 0x0F) ? (vbd_ & 0x0F) : 16;
                    vbit_ += len;
                }
                return v;
            }
            default: return io_[off & 15];
        }
    }

    void io_write(uint16_t off, uint8_t v, bool from_sa1);
};

}  // namespace famemu::snes

// Implementation bits that need the full SnesSystem type live in
// sa1_impl.hpp, included from snes_system.hpp after both classes exist.
