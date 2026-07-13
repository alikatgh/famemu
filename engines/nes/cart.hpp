// famemu NES engine — iNES cartridge with the homebrew-coverage mapper set
// (docs/PLATFORM.md): 0 NROM, 1 MMC1, 2 UNROM, 3 CNROM, 4 MMC3 (+ scanline
// IRQ). Switch-based dispatch, no virtuals; state is save-state friendly.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace famemu::nes {

enum class Mirroring { Horizontal, Vertical, FourScreen, SingleLow, SingleHigh };

class Cart {
public:
    bool load(const uint8_t* data, size_t len) {
        if (len < 16 || std::memcmp(data, "NES\x1A", 4) != 0) return false;
        const int prg16 = data[4], chr8 = data[5];
        const uint8_t f6 = data[6], f7 = data[7];
        mapper_ = static_cast<uint8_t>((f7 & 0xF0) | (f6 >> 4));
        base_mirroring_ = (f6 & 0x08) ? Mirroring::FourScreen
                        : (f6 & 0x01) ? Mirroring::Vertical
                                      : Mirroring::Horizontal;
        mirroring_ = base_mirroring_;
        size_t off = 16 + ((f6 & 0x04) ? 512 : 0);  // skip trainer
        const size_t prg_size = static_cast<size_t>(prg16) * 16384;
        const size_t chr_size = static_cast<size_t>(chr8) * 8192;
        if (off + prg_size + chr_size > len || prg16 == 0) return false;
        prg_.assign(data + off, data + off + prg_size);
        if (chr_size) {
            chr_.assign(data + off + prg_size, data + off + prg_size + chr_size);
            chr_ram_ = false;
        } else {
            chr_.assign(8192, 0);
            chr_ram_ = true;
        }
        prg_banks16_ = prg16;

        // Power-on banking state per mapper.
        switch (mapper_) {
            case 0: case 2: case 3:
                break;
            case 1:
                mmc1_control_ = 0x0C;  // PRG mode 3: fix last bank at $C000
                break;
            case 4:
                std::memset(mmc3_regs_, 0, sizeof mmc3_regs_);
                mmc3_regs_[6] = 0;
                mmc3_regs_[7] = 1;
                break;
            default:
                return false;  // unsupported mapper
        }
        return true;
    }

    // ---- CPU $8000-$FFFF ------------------------------------------------
    uint8_t cpu_read(uint16_t addr) const {
        switch (mapper_) {
            case 0:
                return prg_[(addr - 0x8000) & (prg_.size() - 1)];
            case 1: {
                const int mode = (mmc1_control_ >> 2) & 3;
                int bank;
                if (mode <= 1) {  // 32k switch
                    bank = (mmc1_prg_ & 0x0E) >> 1;
                    return prg_at(bank * 0x8000 + (addr - 0x8000));
                }
                if (addr < 0xC000) {  // $8000 window
                    bank = (mode == 2) ? 0 : (mmc1_prg_ & 0x0F);
                    return prg_at(bank * 0x4000 + (addr - 0x8000));
                }
                bank = (mode == 2) ? (mmc1_prg_ & 0x0F) : (prg_banks16_ - 1);
                return prg_at(bank * 0x4000 + (addr - 0xC000));
            }
            case 2:
                if (addr < 0xC000)
                    return prg_at((unrom_bank_ % prg_banks16_) * 0x4000 + (addr - 0x8000));
                return prg_at((prg_banks16_ - 1) * 0x4000 + (addr - 0xC000));
            case 3:
                return prg_[(addr - 0x8000) & (prg_.size() - 1)];
            case 4: {
                const int banks8 = prg_banks16_ * 2;
                const bool swap = mmc3_bank_select_ & 0x40;
                int bank8;
                if (addr < 0xA000)      bank8 = swap ? banks8 - 2 : (mmc3_regs_[6] & 0x3F);
                else if (addr < 0xC000) bank8 = mmc3_regs_[7] & 0x3F;
                else if (addr < 0xE000) bank8 = swap ? (mmc3_regs_[6] & 0x3F) : banks8 - 2;
                else                    bank8 = banks8 - 1;
                return prg_at((bank8 % banks8) * 0x2000 + (addr & 0x1FFF));
            }
        }
        return 0;
    }

    void cpu_write(uint16_t addr, uint8_t v) {
        switch (mapper_) {
            case 1: mmc1_write(addr, v); break;
            case 2: unrom_bank_ = v; break;
            case 3: cnrom_bank_ = v & 0x03; break;
            case 4: mmc3_write(addr, v); break;
            default: break;  // NROM: ROM
        }
    }

    // ---- PPU $0000-$1FFF ---------------------------------------------------
    uint8_t chr_read(uint16_t addr) const {
        switch (mapper_) {
            case 0: case 2:
                return chr_[addr & 0x1FFF];
            case 1: {
                if (mmc1_control_ & 0x10) {  // two 4k banks
                    const int bank = (addr < 0x1000) ? mmc1_chr0_ : mmc1_chr1_;
                    return chr_at(bank * 0x1000 + (addr & 0x0FFF));
                }
                return chr_at((mmc1_chr0_ & 0x1E) * 0x1000 + (addr & 0x1FFF));
            }
            case 3:
                return chr_at(cnrom_bank_ * 0x2000 + (addr & 0x1FFF));
            case 4: {
                const bool inv = mmc3_bank_select_ & 0x80;
                uint16_t a = inv ? (addr ^ 0x1000) : addr;
                int bank1k;
                if (a < 0x0800)      bank1k = (mmc3_regs_[0] & 0xFE) + ((a >> 10) & 1);
                else if (a < 0x1000) bank1k = (mmc3_regs_[1] & 0xFE) + ((a >> 10) & 1);
                else                 bank1k = mmc3_regs_[2 + ((a - 0x1000) >> 10)];
                return chr_at(bank1k * 0x400 + (a & 0x3FF));
            }
        }
        return chr_[addr & 0x1FFF];
    }

    void chr_write(uint16_t addr, uint8_t v) {
        if (chr_ram_) chr_[addr & 0x1FFF] = v;  // banked CHR RAM: homebrew uses 8k
    }

    // ---- IRQ (MMC3) — clocked once per rendered scanline by the PPU ----------
    void ppu_scanline() {
        if (mapper_ != 4) return;
        if (mmc3_irq_counter_ == 0 || mmc3_irq_reload_) {
            mmc3_irq_counter_ = mmc3_irq_latch_;
            mmc3_irq_reload_ = false;
        } else {
            --mmc3_irq_counter_;
        }
        if (mmc3_irq_counter_ == 0 && mmc3_irq_enable_) mmc3_irq_pending_ = true;
    }
    bool irq_pending() const { return mmc3_irq_pending_; }

    Mirroring mirroring() const { return mirroring_; }
    uint8_t mapper() const { return mapper_; }

    // Save states: banking + IRQ state (+ CHR contents when it's RAM).
    template <class S>
    void serialize(S& s) {
        s.io(mirroring_);
        s.io(mmc1_shift_); s.io(mmc1_count_); s.io(mmc1_control_);
        s.io(mmc1_chr0_); s.io(mmc1_chr1_); s.io(mmc1_prg_);
        s.io(unrom_bank_); s.io(cnrom_bank_);
        s.io(mmc3_bank_select_); s.io(mmc3_regs_);
        s.io(mmc3_irq_latch_); s.io(mmc3_irq_counter_);
        s.io(mmc3_irq_enable_); s.io(mmc3_irq_reload_); s.io(mmc3_irq_pending_);
        if (chr_ram_) s.io_raw(chr_.data(), chr_.size());
    }
    size_t state_extra() const { return chr_ram_ ? chr_.size() : 0; }

private:
    std::vector<uint8_t> prg_, chr_;
    bool chr_ram_ = false;
    uint8_t mapper_ = 0;
    Mirroring base_mirroring_ = Mirroring::Horizontal;
    Mirroring mirroring_ = Mirroring::Horizontal;
    int prg_banks16_ = 1;

    uint8_t prg_at(size_t i) const { return prg_[i % prg_.size()]; }
    uint8_t chr_at(size_t i) const { return chr_[i % chr_.size()]; }

    // MMC1
    uint8_t mmc1_shift_ = 0, mmc1_count_ = 0;
    uint8_t mmc1_control_ = 0x0C, mmc1_chr0_ = 0, mmc1_chr1_ = 0, mmc1_prg_ = 0;
    void mmc1_write(uint16_t addr, uint8_t v) {
        if (v & 0x80) {  // reset shift; fix last PRG bank
            mmc1_shift_ = 0;
            mmc1_count_ = 0;
            mmc1_control_ |= 0x0C;
            return;
        }
        mmc1_shift_ = static_cast<uint8_t>((mmc1_shift_ >> 1) | ((v & 1) << 4));
        if (++mmc1_count_ < 5) return;
        const uint8_t val = mmc1_shift_;
        mmc1_shift_ = 0;
        mmc1_count_ = 0;
        switch ((addr >> 13) & 3) {
            case 0:
                mmc1_control_ = val;
                switch (val & 3) {
                    case 0: mirroring_ = Mirroring::SingleLow; break;
                    case 1: mirroring_ = Mirroring::SingleHigh; break;
                    case 2: mirroring_ = Mirroring::Vertical; break;
                    case 3: mirroring_ = Mirroring::Horizontal; break;
                }
                break;
            case 1: mmc1_chr0_ = val; break;
            case 2: mmc1_chr1_ = val; break;
            case 3: mmc1_prg_ = val; break;
        }
    }

    // UNROM / CNROM
    uint8_t unrom_bank_ = 0, cnrom_bank_ = 0;

    // MMC3
    uint8_t mmc3_bank_select_ = 0;
    uint8_t mmc3_regs_[8] = {0};
    uint8_t mmc3_irq_latch_ = 0, mmc3_irq_counter_ = 0;
    bool mmc3_irq_enable_ = false, mmc3_irq_reload_ = false, mmc3_irq_pending_ = false;
    void mmc3_write(uint16_t addr, uint8_t v) {
        switch (addr & 0xE001) {
            case 0x8000: mmc3_bank_select_ = v; break;
            case 0x8001: mmc3_regs_[mmc3_bank_select_ & 7] = v; break;
            case 0xA000:
                if (base_mirroring_ != Mirroring::FourScreen)
                    mirroring_ = (v & 1) ? Mirroring::Horizontal : Mirroring::Vertical;
                break;
            case 0xA001: break;  // PRG RAM protect: not enforced
            case 0xC000: mmc3_irq_latch_ = v; break;
            case 0xC001: mmc3_irq_reload_ = true; break;
            case 0xE000: mmc3_irq_enable_ = false; mmc3_irq_pending_ = false; break;
            case 0xE001: mmc3_irq_enable_ = true; break;
        }
    }
};

}  // namespace famemu::nes
