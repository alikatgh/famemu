// famemu SNES engine — SA-1 method bodies that need the full SnesSystem
// type. Included from snes_system_impl.hpp after both classes are defined.
#pragma once

namespace famemu::snes {

inline uint8_t Sa1::rom_byte(uint32_t linear) const {
    return sys_.rom_linear(linear);
}
inline uint8_t* Sa1::bwram() const { return sys_.sram_data(); }
inline uint32_t Sa1::bwram_mask() const {
    const uint32_t m = sys_.sram_mask();
    return m ? m : 0x7FFF;
}

// ---- S-CPU view ------------------------------------------------------------

inline int Sa1::snes_read(uint32_t a24) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    if (bank >= 0xC0) return mmc_rom(bank, off);
    if (bank >= 0x40 && bank <= 0x4F)
        return bwram()[(((bank - 0x40) << 16) | off) & bwram_mask()];
    const uint8_t b = bank & 0x7F;
    if (b > 0x3F) return -1;
    if (off >= 0x2200 && off < 0x2400) return io_read_scpu(off);
    if (off >= 0x3000 && off < 0x3800) return iram_[off & 0x7FF];
    if (off >= 0x6000 && off < 0x8000)
        return bwram()[((static_cast<uint32_t>(bmaps_ & 0x1F) * 0x2000) +
                        (off - 0x6000)) & bwram_mask()];
    if (off >= 0x8000) return mmc_rom(bank, off);
    return -1;
}

inline bool Sa1::snes_write(uint32_t a24, uint8_t v) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    if (bank >= 0xC0) return true;                     // ROM: ignore
    if (bank >= 0x40 && bank <= 0x4F) {
        bwram()[(((bank - 0x40) << 16) | off) & bwram_mask()] = v;
        return true;
    }
    const uint8_t b = bank & 0x7F;
    if (b > 0x3F) return false;
    if (off >= 0x2200 && off < 0x2400) { io_write(off, v, false); return true; }
    if (off >= 0x3000 && off < 0x3800) { iram_[off & 0x7FF] = v; return true; }
    if (off >= 0x6000 && off < 0x8000) {
        bwram()[((static_cast<uint32_t>(bmaps_ & 0x1F) * 0x2000) +
                 (off - 0x6000)) & bwram_mask()] = v;
        return true;
    }
    if (off >= 0x8000) return true;                    // ROM: ignore
    return false;
}

// ---- SA-1 side bus -----------------------------------------------------------

inline uint8_t Sa1::read(uint32_t a24) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    const uint8_t b = bank & 0x7F;
    if (b <= 0x3F) {
        // Interrupt/reset vectors come from the CRV/CNV/CIV registers.
        if (off >= 0xFFEA) {
            switch (off) {
                case 0xFFEA: case 0xFFFA: return static_cast<uint8_t>(cnv_);
                case 0xFFEB: case 0xFFFB: return static_cast<uint8_t>(cnv_ >> 8);
                case 0xFFEE: case 0xFFFE: return static_cast<uint8_t>(civ_);
                case 0xFFEF: case 0xFFFF: return static_cast<uint8_t>(civ_ >> 8);
                case 0xFFFC: return static_cast<uint8_t>(crv_);
                case 0xFFFD: return static_cast<uint8_t>(crv_ >> 8);
                default: break;
            }
        }
        if (off < 0x0800) return iram_[off];
        if (off >= 0x3000 && off < 0x3800) return iram_[off & 0x7FF];
        if (off >= 0x2200 && off < 0x2400) return io_read_scpu(off);
        if (off >= 0x6000 && off < 0x8000) {
            if (bmap_ & 0x80) log_once("bitmap-projected $6000 window");
            return bwram()[((static_cast<uint32_t>(bmap_ & 0x7F) * 0x2000) +
                            (off - 0x6000)) & bwram_mask()];
        }
        if (off >= 0x8000) return mmc_rom(bank, off);
        return 0;
    }
    if (bank >= 0x40 && bank <= 0x4F)
        return bwram()[(((bank - 0x40) << 16) | off) & bwram_mask()];
    if (bank >= 0x50 && bank <= 0x6F) { log_once("bitmap BW-RAM banks 50-6F"); return 0; }
    if (bank >= 0xC0) return mmc_rom(bank, off);
    return 0;
}

inline void Sa1::write(uint32_t a24, uint8_t v) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    const uint8_t b = bank & 0x7F;
    if (b <= 0x3F) {
        if (off < 0x0800) { iram_[off] = v; return; }
        if (off >= 0x3000 && off < 0x3800) { iram_[off & 0x7FF] = v; return; }
        if (off >= 0x2200 && off < 0x2400) { io_write(off, v, true); return; }
        if (off >= 0x6000 && off < 0x8000) {
            bwram()[((static_cast<uint32_t>(bmap_ & 0x7F) * 0x2000) +
                     (off - 0x6000)) & bwram_mask()] = v;
            return;
        }
        return;
    }
    if (bank >= 0x40 && bank <= 0x4F)
        bwram()[(((bank - 0x40) << 16) | off) & bwram_mask()] = v;
}

// ---- registers ---------------------------------------------------------------

inline void Sa1::io_write(uint16_t off, uint8_t v, bool from_sa1) {
    (void)from_sa1;
    switch (off) {
        case 0x2200:  // CCNT: SA-1 control from the S-CPU
            if (!(v & 0x20) && (ccnt_ & 0x20)) {       // release reset
                cpu_.reset();                          // fetches CRV via bus
                cpu_.stopped = false;
                running_ = true;
            } else if ((v & 0x20) && !(ccnt_ & 0x20)) {
                cpu_.stopped = true;
                running_ = false;
            }
            if (v & 0x80) cfr_flags_ |= 0x80;          // IRQ to SA-1
            if (v & 0x10) cfr_flags_ |= 0x10;          // NMI to SA-1
            ccnt_ = v;
            break;
        case 0x2201: sie_ = v; break;
        case 0x2202: sfr_flags_ &= static_cast<uint8_t>(~(v & 0xA0)); break;
        case 0x2203: crv_ = static_cast<uint16_t>((crv_ & 0xFF00) | v); break;
        case 0x2204: crv_ = static_cast<uint16_t>((v << 8) | (crv_ & 0xFF)); break;
        case 0x2205: cnv_ = static_cast<uint16_t>((cnv_ & 0xFF00) | v); break;
        case 0x2206: cnv_ = static_cast<uint16_t>((v << 8) | (cnv_ & 0xFF)); break;
        case 0x2207: civ_ = static_cast<uint16_t>((civ_ & 0xFF00) | v); break;
        case 0x2208: civ_ = static_cast<uint16_t>((v << 8) | (civ_ & 0xFF)); break;
        case 0x2209:  // SCNT: message + IRQ to the S-CPU (written by SA-1)
            scnt_ = v;
            if (v & 0x80) sfr_flags_ |= 0x80;
            break;
        case 0x220A: cie_ = v; break;
        case 0x220B: cfr_flags_ &= static_cast<uint8_t>(~(v & 0xB0)); break;
        case 0x2220: case 0x2221: case 0x2222: case 0x2223:
            mmc_[off - 0x2220] = v;
            break;
        case 0x2224: bmaps_ = v; break;
        case 0x2225: bmap_ = v; break;
        case 0x2226: case 0x2227: case 0x2228: case 0x2229: case 0x222A:
            break;                                     // write protects: allow all
        case 0x2230: dcnt_ = v; break;
        case 0x2231: cdma_ = v; break;
        case 0x2232: sda_ = (sda_ & 0xFFFF00) | v; break;
        case 0x2233: sda_ = (sda_ & 0xFF00FF) | (static_cast<uint32_t>(v) << 8); break;
        case 0x2234: sda_ = (sda_ & 0x00FFFF) | (static_cast<uint32_t>(v) << 16); break;
        case 0x2235: dda_ = (dda_ & 0xFFFF00) | v; break;
        case 0x2236:
            dda_ = (dda_ & 0xFF00FF) | (static_cast<uint32_t>(v) << 8);
            if ((dcnt_ & 0x80) && !(dcnt_ & 0x04)) do_dma();   // IRAM dest
            break;
        case 0x2237:
            dda_ = (dda_ & 0x00FFFF) | (static_cast<uint32_t>(v) << 16);
            if ((dcnt_ & 0x80) && (dcnt_ & 0x04)) do_dma();    // BW-RAM dest
            break;
        case 0x2238: dtc_ = static_cast<uint16_t>((dtc_ & 0xFF00) | v); break;
        case 0x2239: dtc_ = static_cast<uint16_t>((v << 8) | (dtc_ & 0xFF)); break;
        case 0x2250:
            mcnt_ = v;
            if (v & 0x02) { mr_ = 0; overflow_ = false; }
            break;
        case 0x2251: ma_ = static_cast<uint16_t>((ma_ & 0xFF00) | v); break;
        case 0x2252: ma_ = static_cast<uint16_t>((v << 8) | (ma_ & 0xFF)); break;
        case 0x2253: mb_ = static_cast<uint16_t>((mb_ & 0xFF00) | v); break;
        case 0x2254:
            mb_ = static_cast<uint16_t>((v << 8) | (mb_ & 0xFF));
            arith_start_mul_div();
            break;
        case 0x2258:
            vbd_ = v;
            if (!(v & 0x80)) {                          // fixed mode: advance now
                const uint32_t len = (v & 0x0F) ? (v & 0x0F) : 16;
                vbit_ += len;
            }
            break;
        case 0x2259: vda_ = (vda_ & 0xFFFF00) | v; break;
        case 0x225A: vda_ = (vda_ & 0xFF00FF) | (static_cast<uint32_t>(v) << 8); break;
        case 0x225B:
            vda_ = (vda_ & 0x00FFFF) | (static_cast<uint32_t>(v) << 16);
            vbit_ = 0;
            break;
        default:
            io_[off & 15] = v;
            break;
    }
}

inline void Sa1::run_line() {
    if (!running_ || (ccnt_ & 0x20)) return;
    const uint64_t budget = cpu_.cyc + SnesSystem::kLineCycles;
    while (cpu_.cyc < budget && !cpu_.stopped) {
        if ((cfr_flags_ & 0x10) && (cie_ & 0x10)) {    // NMI (edge)
            cfr_flags_ &= static_cast<uint8_t>(~0x10);
            cpu_.nmi();
        }
        if (((cfr_flags_ & 0x80) && (cie_ & 0x80)) ||  // IRQ from S-CPU
            ((cfr_flags_ & 0x20) && (cie_ & 0x20)))    // DMA IRQ
            cpu_.irq();                                // no-op while I set
        if (cpu_.waiting) { cpu_.cyc = budget; return; }
        cpu_.step();
    }
    if (cpu_.stopped && cpu_.cyc < budget) cpu_.cyc = budget;
}

}  // namespace famemu::snes
