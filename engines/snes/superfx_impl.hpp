// famemu SNES engine — SuperFX/GSU method bodies that need the full
// SnesSystem type. Included from snes_system_impl.hpp.
#pragma once

namespace famemu::snes {

inline uint8_t SuperFx::mem_read(uint8_t bank, uint16_t a) const {
    bank &= 0x7F;
    if (bank <= 0x3F)
        return sys_.rom_linear(static_cast<uint32_t>(bank) * 0x8000 + (a & 0x7FFF));
    if (bank <= 0x5F)
        return sys_.rom_linear((static_cast<uint32_t>(bank - 0x40) << 16) | a);
    if (bank >= 0x70 && bank <= 0x71)
        return ram()[(((static_cast<uint32_t>(bank) - 0x70) << 16) | a) & ram_mask()];
    return 0;
}

inline uint8_t* SuperFx::ram() const { return sys_.sram_data(); }
inline uint32_t SuperFx::ram_mask() const {
    const uint32_t m = sys_.sram_mask();
    return m ? m : 0xFFFF;
}

// ---- S-CPU view --------------------------------------------------------------

inline int SuperFx::snes_read(uint32_t a24) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    if (bank == 0x70 || bank == 0x71)
        return ram()[(((static_cast<uint32_t>(bank) - 0x70) << 16) | off) & ram_mask()];
    const uint8_t b = bank & 0x7F;
    if (b > 0x3F) return -1;
    if (off >= 0x6000 && off < 0x8000)
        return ram()[(off - 0x6000) & ram_mask()];
    if (off < 0x3000 || off > 0x32FF) return -1;
    if (off < 0x3020) {
        const int idx = (off - 0x3000) >> 1;
        return (off & 1) ? (r_[idx] >> 8) : (r_[idx] & 0xFF);
    }
    switch (off) {
        case 0x3030: return sfr_ & 0xFE;
        case 0x3031: {
            uint8_t v = static_cast<uint8_t>((sfr_ >> 8) & 0x7F);
            if (irq_) v |= 0x80;
            irq_ = false;                       // reading SFR clears the IRQ
            return v;
        }
        case 0x3033: return bramr_;
        case 0x3034: return pbr_;
        case 0x3036: return rombr_;
        case 0x3037: return cfgr_;
        case 0x3038: return scbr_;
        case 0x3039: return clsr_;
        case 0x303A: return scmr_;
        case 0x303B: return 0x04;               // VCR: version
        case 0x303C: return rambr_;
        case 0x303E: return cbr_ & 0xFF;
        case 0x303F: return cbr_ >> 8;
        default: return 0;
    }
}

inline bool SuperFx::snes_write(uint32_t a24, uint8_t v) {
    const uint8_t bank = (a24 >> 16) & 0xFF;
    const uint16_t off = a24 & 0xFFFF;
    if (bank == 0x70 || bank == 0x71) {
        ram()[(((static_cast<uint32_t>(bank) - 0x70) << 16) | off) & ram_mask()] = v;
        return true;
    }
    const uint8_t b = bank & 0x7F;
    if (b > 0x3F) return false;
    if (off >= 0x6000 && off < 0x8000) {
        ram()[(off - 0x6000) & ram_mask()] = v;
        return true;
    }
    if (off < 0x3000 || off > 0x32FF) return false;
    if (off < 0x3020) {
        const int idx = (off - 0x3000) >> 1;
        if (off & 1) r_[idx] = static_cast<uint16_t>((r_[idx] & 0x00FF) | (v << 8));
        else r_[idx] = static_cast<uint16_t>((r_[idx] & 0xFF00) | v);
        if (idx == 14) rombuf_update();
        if (off == 0x301F) {                    // writing R15 MSB starts the GSU
            sfr_ |= GO;
            pipe_ = 0x01;                       // pipe primes on the first step
        }
        return true;
    }
    switch (off) {
        case 0x3030: {
            const bool was_go = sfr_ & GO;
            sfr_ = static_cast<uint16_t>((sfr_ & 0xFF00) | (v & 0xFE));
            if (was_go && !(sfr_ & GO)) cbr_ = 0;
            break;
        }
        case 0x3031: sfr_ = static_cast<uint16_t>(((v & 0x7F) << 8) | (sfr_ & 0xFF)); break;
        case 0x3033: bramr_ = v; break;
        case 0x3034: pbr_ = v & 0x7F; break;
        case 0x3037: cfgr_ = v; break;
        case 0x3038: scbr_ = v; break;
        case 0x3039: clsr_ = v & 1; break;
        case 0x303A: scmr_ = v; break;
        default: break;
    }
    return true;
}

inline void SuperFx::run_line() {
    if (!(sfr_ & GO)) return;
    // ~10.7 MHz (21.4 with CLSR set) against 15.7 kHz lines, a few cycles
    // per instruction: several hundred instructions per scanline.
    const int budget = (clsr_ & 1) ? 700 : 350;
    for (int i = 0; i < budget && (sfr_ & GO); ++i) step();
}

// ---- the instruction set --------------------------------------------------------

inline void SuperFx::step() {
    const uint8_t op = pipe();
    const int m = alt();
    const uint8_t n = op & 15;

    auto write_r = [&](int i, uint16_t v) {
        r_[i] = v;
        if (i == 14) rombuf_update();
    };
    auto branch = [&](bool take) {
        const int8_t e = static_cast<int8_t>(pipe());
        if (take) r_[15] = static_cast<uint16_t>(r_[15] + e);
    };
    auto ram_word_rd = [&](uint16_t a) -> uint16_t {
        ramaddr_ = a;
        return static_cast<uint16_t>(ram_rd(a) | (ram_rd(a ^ 1) << 8));
    };
    auto ram_word_wr = [&](uint16_t a, uint16_t v) {
        ramaddr_ = a;
        ram_wr(a, static_cast<uint8_t>(v));
        ram_wr(a ^ 1, static_cast<uint8_t>(v >> 8));
    };

    switch (op >> 4) {
        case 0x0:
            switch (op) {
                case 0x00:  // STOP
                    sfr_ &= static_cast<uint16_t>(~GO);
                    cbr_ = 0;
                    irq_ = true;
                    regs_reset();
                    break;
                case 0x01: regs_reset(); break;                    // NOP
                case 0x02: cbr_ = r_[15] & 0xFFF0; regs_reset(); break;  // CACHE
                case 0x03: {  // LSR
                    const uint16_t a = sr();
                    sfr_ = static_cast<uint16_t>((sfr_ & ~CY) | ((a & 1) ? CY : 0));
                    const uint16_t v = a >> 1;
                    flags_zs(v);
                    set_dreg(v);
                    regs_reset();
                    break;
                }
                case 0x04: {  // ROL
                    const uint16_t a = sr();
                    const bool c = sfr_ & CY;
                    sfr_ = static_cast<uint16_t>((sfr_ & ~CY) | ((a & 0x8000) ? CY : 0));
                    const uint16_t v = static_cast<uint16_t>((a << 1) | (c ? 1 : 0));
                    flags_zs(v);
                    set_dreg(v);
                    regs_reset();
                    break;
                }
                case 0x05: branch(true); break;                    // BRA
                case 0x06: branch(((sfr_ & S) != 0) == ((sfr_ & OV) != 0)); break;  // BGE
                case 0x07: branch(((sfr_ & S) != 0) != ((sfr_ & OV) != 0)); break;  // BLT
                case 0x08: branch(!(sfr_ & Z)); break;             // BNE
                case 0x09: branch(sfr_ & Z); break;                // BEQ
                case 0x0A: branch(!(sfr_ & S)); break;             // BPL
                case 0x0B: branch(sfr_ & S); break;                // BMI
                case 0x0C: branch(!(sfr_ & CY)); break;            // BCC
                case 0x0D: branch(sfr_ & CY); break;               // BCS
                case 0x0E: branch(!(sfr_ & OV)); break;            // BVC
                default:   branch(sfr_ & OV); break;               // BVS
            }
            break;

        case 0x1:  // TO Rn / MOVE Rn,Rs
            if (sfr_ & B) {
                write_r(n, sr());
                regs_reset();
            } else {
                dreg_ = n;
            }
            break;

        case 0x2:  // WITH Rn
            sreg_ = dreg_ = n;
            sfr_ |= B;
            break;

        case 0x3:
            if (op < 0x3C) {  // STW/STB (Rm), m = 0-11
                ramaddr_ = r_[n];
                if (m & 1) { ram_wr(r_[n], static_cast<uint8_t>(sr())); }
                else ram_word_wr(r_[n], sr());
                regs_reset();
            } else if (op == 0x3C) {  // LOOP
                r_[12] = static_cast<uint16_t>(r_[12] - 1);
                flags_zs(r_[12]);
                if (r_[12]) r_[15] = r_[13];
                regs_reset();
            } else if (op == 0x3D) { sfr_ |= ALT1; }
            else if (op == 0x3E) { sfr_ |= ALT2; }
            else { sfr_ |= ALT1 | ALT2; }
            break;

        case 0x4:
            if (op < 0x4C) {  // LDW/LDB (Rm)
                ramaddr_ = r_[n];
                uint16_t v;
                if (m & 1) v = ram_rd(r_[n]);
                else v = ram_word_rd(r_[n]);
                set_dreg(v);
                regs_reset();
            } else if (op == 0x4C) {
                if (m & 1) {  // RPIX
                    const uint8_t v = read_pixel(r_[1] & 0xFF, r_[2] & 0xFF);
                    flags_zs(v);
                    set_dreg(v);
                } else {      // PLOT
                    uint8_t c = colr_;
                    const int bpp = screen_bpp();
                    if (bpp < 8) c &= static_cast<uint8_t>((1 << bpp) - 1);
                    const bool transparent = !(por_ & 0x01) && c == 0;
                    if (!transparent)
                        plot_pixel(r_[1] & 0xFF, r_[2] & 0xFF, c);
                    r_[1] = static_cast<uint16_t>(r_[1] + 1);
                }
                regs_reset();
            } else if (op == 0x4D) {  // SWAP
                const uint16_t v = static_cast<uint16_t>((sr() >> 8) | (sr() << 8));
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            } else if (op == 0x4E) {
                if (m & 1) por_ = sr() & 0x1F;             // CMODE
                else colr_ = static_cast<uint8_t>(sr());   // COLOR
                regs_reset();
            } else {  // NOT
                const uint16_t v = static_cast<uint16_t>(~sr());
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            }
            break;

        case 0x5: {  // ADD/ADC/ADD#/ADC#
            const int32_t a = sr();
            const int32_t b = (m & 2) ? n : r_[n];
            const int32_t c = (m & 1) ? ((sfr_ & CY) ? 1 : 0) : 0;
            const int32_t t = a + b + c;
            sfr_ = static_cast<uint16_t>((sfr_ & ~(OV | CY)) |
                    (((~(a ^ b) & (a ^ t)) & 0x8000) ? OV : 0) |
                    ((t > 0xFFFF) ? CY : 0));
            flags_zs(static_cast<uint16_t>(t));
            set_dreg(static_cast<uint16_t>(t));
            regs_reset();
            break;
        }

        case 0x6: {  // SUB/SBC/SUB#/CMP
            const int32_t a = sr();
            const int32_t b = (m == 2) ? n : r_[n];
            const int32_t c = (m == 1) ? ((sfr_ & CY) ? 0 : 1) : 0;
            const int32_t t = a - b - c;
            sfr_ = static_cast<uint16_t>((sfr_ & ~(OV | CY)) |
                    ((((a ^ b) & (a ^ t)) & 0x8000) ? OV : 0) |
                    ((t >= 0) ? CY : 0));
            flags_zs(static_cast<uint16_t>(t));
            if (m != 3) set_dreg(static_cast<uint16_t>(t));
            regs_reset();
            break;
        }

        case 0x7:
            if (op == 0x70) {  // MERGE
                const uint16_t v = static_cast<uint16_t>((r_[7] & 0xFF00) |
                                                         (r_[8] >> 8));
                sfr_ = static_cast<uint16_t>((sfr_ & ~(OV | S | CY | Z)) |
                        ((v & 0xC0C0) ? OV : 0) | ((v & 0x8080) ? S : 0) |
                        ((v & 0xE0E0) ? CY : 0) | ((v & 0xF0F0) ? Z : 0));
                set_dreg(v);
                regs_reset();
            } else {  // AND/BIC/AND#/BIC#
                const uint16_t b = (m & 2) ? n : r_[n];
                const uint16_t v = (m & 1) ? (sr() & ~b) : (sr() & b);
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            }
            break;

        case 0x8: {  // MULT/UMULT/MULT#/UMULT#
            const uint16_t b = (m & 2) ? n : r_[n];
            const uint16_t v = (m & 1)
                ? static_cast<uint16_t>(static_cast<uint8_t>(sr()) *
                                        static_cast<uint8_t>(b))
                : static_cast<uint16_t>(static_cast<int8_t>(sr()) *
                                        static_cast<int8_t>(b));
            flags_zs(v);
            set_dreg(v);
            regs_reset();
            break;
        }

        case 0x9:
            if (op == 0x90) {  // SBK
                ram_word_wr(ramaddr_, sr());
                regs_reset();
            } else if (op <= 0x94) {  // LINK #1-4
                r_[11] = static_cast<uint16_t>(r_[15] + (op - 0x90));
                regs_reset();
            } else if (op == 0x95) {  // SEX
                const uint16_t v = static_cast<uint16_t>(
                    static_cast<int16_t>(static_cast<int8_t>(sr())));
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            } else if (op == 0x96) {  // ASR / DIV2
                int16_t a = static_cast<int16_t>(sr());
                sfr_ = static_cast<uint16_t>((sfr_ & ~CY) | ((a & 1) ? CY : 0));
                int16_t v = static_cast<int16_t>(a >> 1);
                if ((m & 1) && a == -1) v = 0;             // DIV2 rounds -1 to 0
                flags_zs(static_cast<uint16_t>(v));
                set_dreg(static_cast<uint16_t>(v));
                regs_reset();
            } else if (op == 0x97) {  // ROR
                const uint16_t a = sr();
                const bool c = sfr_ & CY;
                sfr_ = static_cast<uint16_t>((sfr_ & ~CY) | ((a & 1) ? CY : 0));
                const uint16_t v = static_cast<uint16_t>((a >> 1) | (c ? 0x8000 : 0));
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            } else if (op <= 0x9D) {  // JMP Rn / LJMP Rn
                if (m & 1) {                               // LJMP
                    pbr_ = static_cast<uint8_t>(r_[n] & 0x7F);
                    r_[15] = sr();
                    cbr_ = r_[15] & 0xFFF0;
                } else {
                    r_[15] = r_[n];
                }
                regs_reset();
            } else if (op == 0x9E) {  // LOB
                const uint16_t v = sr() & 0xFF;
                sfr_ = static_cast<uint16_t>((sfr_ & ~(Z | S)) | (v ? 0 : Z) |
                                             ((v & 0x80) ? S : 0));
                set_dreg(v);
                regs_reset();
            } else {  // FMULT / LMULT
                const int32_t prod = static_cast<int16_t>(sr()) *
                                     static_cast<int16_t>(r_[6]);
                if (m & 1) r_[4] = static_cast<uint16_t>(prod);   // LMULT
                const uint16_t v = static_cast<uint16_t>(prod >> 16);
                sfr_ = static_cast<uint16_t>((sfr_ & ~CY) |
                                             ((prod & 0x8000) ? CY : 0));
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            }
            break;

        case 0xA:  // IBT / LMS / SMS
            if (m == 1) {            // LMS Rn,(yy*2)
                const uint16_t a = static_cast<uint16_t>(pipe() << 1);
                write_r(n, ram_word_rd(a));
            } else if (m == 2) {     // SMS (yy*2),Rn
                const uint16_t a = static_cast<uint16_t>(pipe() << 1);
                ram_word_wr(a, r_[n]);
            } else {                 // IBT Rn,#sex8
                write_r(n, static_cast<uint16_t>(
                    static_cast<int16_t>(static_cast<int8_t>(pipe()))));
            }
            regs_reset();
            break;

        case 0xB:  // FROM Rn / MOVES
            if (sfr_ & B) {
                const uint16_t v = r_[n];
                sfr_ = static_cast<uint16_t>((sfr_ & ~(OV | S | Z)) |
                        ((v & 0x80) ? OV : 0) | ((v & 0x8000) ? S : 0) |
                        (v ? 0 : Z));
                set_dreg(v);
                regs_reset();
            } else {
                sreg_ = n;
            }
            break;

        case 0xC:
            if (op == 0xC0) {  // HIB
                const uint16_t v = sr() >> 8;
                sfr_ = static_cast<uint16_t>((sfr_ & ~(Z | S)) | (v ? 0 : Z) |
                                             ((v & 0x80) ? S : 0));
                set_dreg(v);
                regs_reset();
            } else {  // OR/XOR/OR#/XOR#
                const uint16_t b = (m & 2) ? n : r_[n];
                const uint16_t v = (m & 1) ? (sr() ^ b) : (sr() | b);
                flags_zs(v);
                set_dreg(v);
                regs_reset();
            }
            break;

        case 0xD:
            if (op == 0xDF) {
                if (m == 2) rambr_ = sr() & 1;             // RAMB
                else if (m == 3) { rombr_ = static_cast<uint8_t>(sr() & 0x7F); rombuf_update(); }  // ROMB
                else colr_ = romdr_;                       // GETC
                regs_reset();
            } else {  // INC Rn
                write_r(n, static_cast<uint16_t>(r_[n] + 1));
                flags_zs(r_[n]);
                regs_reset();
            }
            break;

        default:
            if (op == 0xEF) {  // GETB / GETBH / GETBL / GETBS
                uint16_t v;
                switch (m) {
                    case 1: v = static_cast<uint16_t>((romdr_ << 8) | (sr() & 0xFF)); break;
                    case 2: v = static_cast<uint16_t>((sr() & 0xFF00) | romdr_); break;
                    case 3: v = static_cast<uint16_t>(static_cast<int16_t>(
                                static_cast<int8_t>(romdr_))); break;
                    default: v = romdr_; break;
                }
                set_dreg(v);
                regs_reset();
            } else if (op < 0xEF) {  // DEC Rn
                write_r(n, static_cast<uint16_t>(r_[n] - 1));
                flags_zs(r_[n]);
                regs_reset();
            } else {  // F0-FF: IWT / LM / SM
                if (m == 1) {          // LM Rn,(xxxx)
                    uint16_t a = pipe();
                    a |= static_cast<uint16_t>(pipe() << 8);
                    write_r(n, ram_word_rd(a));
                } else if (m == 2) {   // SM (xxxx),Rn
                    uint16_t a = pipe();
                    a |= static_cast<uint16_t>(pipe() << 8);
                    ram_word_wr(a, r_[n]);
                } else {               // IWT Rn,#xxxx
                    uint16_t v = pipe();
                    v |= static_cast<uint16_t>(pipe() << 8);
                    write_r(n, v);
                }
                regs_reset();
            }
            break;
    }
}

}  // namespace famemu::snes
