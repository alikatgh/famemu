// famemu NES engine — iNES cartridge. Mapper 0 (NROM) now; the mapper set
// grows per docs/PLATFORM.md (0/1/2/3/4 = homebrew coverage).
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace famemu::nes {

enum class Mirroring { Horizontal, Vertical, FourScreen };

class Cart {
public:
    bool load(const uint8_t* data, size_t len) {
        if (len < 16 || std::memcmp(data, "NES\x1A", 4) != 0) return false;
        const int prg16 = data[4], chr8 = data[5];
        const uint8_t f6 = data[6], f7 = data[7];
        mapper_ = static_cast<uint8_t>((f7 & 0xF0) | (f6 >> 4));
        mirroring_ = (f6 & 0x08) ? Mirroring::FourScreen
                   : (f6 & 0x01) ? Mirroring::Vertical
                                 : Mirroring::Horizontal;
        size_t off = 16 + ((f6 & 0x04) ? 512 : 0);  // skip trainer
        const size_t prg_size = static_cast<size_t>(prg16) * 16384;
        const size_t chr_size = static_cast<size_t>(chr8) * 8192;
        if (off + prg_size + chr_size > len || prg16 == 0) return false;
        prg_.assign(data + off, data + off + prg_size);
        if (chr_size) {
            chr_.assign(data + off + prg_size, data + off + prg_size + chr_size);
            chr_ram_ = false;
        } else {
            chr_.assign(8192, 0);  // CHR RAM
            chr_ram_ = true;
        }
        return mapper_ == 0;  // NROM only, for now
    }

    // CPU $8000-$FFFF
    uint8_t cpu_read(uint16_t addr) const {
        return prg_[(addr - 0x8000) & (prg_.size() - 1)];  // 16k mirrors to 32k
    }
    void cpu_write(uint16_t, uint8_t) {}  // NROM: PRG is ROM

    // PPU $0000-$1FFF
    uint8_t chr_read(uint16_t addr) const { return chr_[addr & 0x1FFF]; }
    void chr_write(uint16_t addr, uint8_t v) {
        if (chr_ram_) chr_[addr & 0x1FFF] = v;
    }

    Mirroring mirroring() const { return mirroring_; }
    uint8_t mapper() const { return mapper_; }

private:
    std::vector<uint8_t> prg_, chr_;
    bool chr_ram_ = false;
    uint8_t mapper_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
};

}  // namespace famemu::nes
