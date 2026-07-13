// famemu SNES engine — 65816 CPU. Clean-room from public documentation
// (WDC datasheet, SNESdev wiki). Runtime M/X width handling (no templates);
// cycle counts are instruction-level approximations refined against KORA's
// frame loop and public test ROMs as the engine matures.
#pragma once

#include <cstdint>

namespace famemu::snes {

struct Bus16 {
    virtual uint8_t read(uint32_t addr24) = 0;          // full 24-bit bus
    virtual void write(uint32_t addr24, uint8_t v) = 0;
    virtual ~Bus16() = default;
};

class Cpu65816 {
public:
    // P flags. In emulation mode (e_), M and X are forced 1.
    static constexpr uint8_t C = 0x01, Z = 0x02, I = 0x04, D = 0x08;
    static constexpr uint8_t X = 0x10, M = 0x20, V = 0x40, N = 0x80;

    explicit Cpu65816(Bus16& bus) : bus_(bus) {}

    void reset();
    void step();            // one instruction
    void nmi();
    void irq();

    // Registers (public: tracing, save states).
    uint16_t a = 0, x = 0, y = 0;   // C accumulator / index registers
    uint16_t s = 0x01FF, d = 0;     // stack pointer, direct page
    uint8_t dbr = 0, pbr = 0;       // data / program bank
    uint16_t pc = 0;
    uint8_t p = M | X | I;
    bool e = true;                  // emulation mode
    uint64_t cyc = 0;
    bool waiting = false;           // WAI
    bool stopped = false;           // STP

private:
    Bus16& bus_;

    bool m8() const { return e || (p & M); }   // accumulator is 8-bit
    bool x8() const { return e || (p & X); }   // index regs are 8-bit

    // -- bus helpers -----------------------------------------------------
    uint8_t rd(uint32_t a24) { ++cyc; return bus_.read(a24 & 0xFFFFFF); }
    void wr(uint32_t a24, uint8_t v) { ++cyc; bus_.write(a24 & 0xFFFFFF, v); }
    uint16_t rd16(uint32_t a24) {
        return static_cast<uint16_t>(rd(a24) | (rd(a24 + 1) << 8));
    }
    void wr16(uint32_t a24, uint16_t v) {
        wr(a24, static_cast<uint8_t>(v));
        wr(a24 + 1, static_cast<uint8_t>(v >> 8));
    }
    uint8_t fetch() { return rd((static_cast<uint32_t>(pbr) << 16) | pc++); }
    uint16_t fetch16() {
        uint16_t v = fetch();
        return static_cast<uint16_t>(v | (fetch() << 8));
    }
    uint32_t fetch24() {
        uint32_t v = fetch16();
        return v | (static_cast<uint32_t>(fetch()) << 16);
    }

    // -- stack (native: 16-bit S anywhere in bank 0; emulation: page 1) ---
    void push8(uint8_t v) {
        wr(s, v);
        --s;
        if (e) s = static_cast<uint16_t>(0x0100 | (s & 0xFF));
    }
    uint8_t pull8() {
        ++s;
        if (e) s = static_cast<uint16_t>(0x0100 | (s & 0xFF));
        return rd(s);
    }
    void push16(uint16_t v) { push8(static_cast<uint8_t>(v >> 8)); push8(static_cast<uint8_t>(v)); }
    uint16_t pull16() {
        uint16_t lo = pull8();
        return static_cast<uint16_t>(lo | (pull8() << 8));
    }

    // -- flags -------------------------------------------------------------
    void setZN8(uint8_t v) { p = (p & ~(Z | N)) | (v ? 0 : Z) | (v & 0x80); }
    void setZN16(uint16_t v) {
        p = (p & ~(Z | N)) | (v ? 0 : Z) | ((v >> 8) & 0x80);
    }
    void setZN(uint16_t v, bool is8) { is8 ? setZN8(static_cast<uint8_t>(v)) : setZN16(v); }
    void setFlag(uint8_t f, bool on) { p = on ? (p | f) : (p & ~f); }
    void force_widths() {   // after any P change / mode switch
        if (e) p |= M | X;
        if (p & X) { x &= 0xFF; y &= 0xFF; }
    }

    // -- addressing: return 24-bit effective address ------------------------
    uint32_t am_dp() { return (d + fetch()) & 0xFFFF; }              // bank 0
    uint32_t am_dpx() { return (d + fetch() + (x8() ? (x & 0xFF) : x)) & 0xFFFF; }
    uint32_t am_dpy() { return (d + fetch() + (x8() ? (y & 0xFF) : y)) & 0xFFFF; }
    uint32_t am_abs() { return (static_cast<uint32_t>(dbr) << 16) | fetch16(); }
    uint32_t am_absx() {
        return ((static_cast<uint32_t>(dbr) << 16) + fetch16() +
                (x8() ? (x & 0xFF) : x)) & 0xFFFFFF;
    }
    uint32_t am_absy() {
        return ((static_cast<uint32_t>(dbr) << 16) + fetch16() +
                (x8() ? (y & 0xFF) : y)) & 0xFFFFFF;
    }
    uint32_t am_long() { return fetch24(); }
    uint32_t am_longx() {
        return (fetch24() + (x8() ? (x & 0xFF) : x)) & 0xFFFFFF;
    }
    uint32_t am_ind_dp() {          // (dp)
        uint16_t ptr = static_cast<uint16_t>((d + fetch()) & 0xFFFF);
        return (static_cast<uint32_t>(dbr) << 16) | rd16(ptr);
    }
    uint32_t am_ind_dpx() {         // (dp,X)
        uint16_t ptr = static_cast<uint16_t>(
            (d + fetch() + (x8() ? (x & 0xFF) : x)) & 0xFFFF);
        return (static_cast<uint32_t>(dbr) << 16) | rd16(ptr);
    }
    uint32_t am_ind_dpy() {         // (dp),Y
        uint16_t ptr = static_cast<uint16_t>((d + fetch()) & 0xFFFF);
        return ((static_cast<uint32_t>(dbr) << 16) + rd16(ptr) +
                (x8() ? (y & 0xFF) : y)) & 0xFFFFFF;
    }
    uint32_t am_indl_dp() {         // [dp]
        uint16_t ptr = static_cast<uint16_t>((d + fetch()) & 0xFFFF);
        uint32_t lo = rd16(ptr);
        return lo | (static_cast<uint32_t>(rd(static_cast<uint16_t>(ptr + 2))) << 16);
    }
    uint32_t am_indl_dpy() {        // [dp],Y
        return (am_indl_dp() + (x8() ? (y & 0xFF) : y)) & 0xFFFFFF;
    }
    uint32_t am_sr() { return (s + fetch()) & 0xFFFF; }              // sr,S
    uint32_t am_sr_y() {            // (sr,S),Y
        uint16_t ptr = static_cast<uint16_t>((s + fetch()) & 0xFFFF);
        return ((static_cast<uint32_t>(dbr) << 16) + rd16(ptr) +
                (x8() ? (y & 0xFF) : y)) & 0xFFFFFF;
    }

    // -- data helpers honoring M ---------------------------------------------
    uint16_t load_m(uint32_t ea) {
        return m8() ? rd(ea) : rd16(ea);
    }
    void store_m(uint32_t ea, uint16_t v) {
        if (m8()) wr(ea, static_cast<uint8_t>(v));
        else wr16(ea, v);
    }
    uint16_t load_x(uint32_t ea) { return x8() ? rd(ea) : rd16(ea); }

    // ALU
    void adc(uint16_t v);
    void sbc(uint16_t v);
    void cmp_gen(uint16_t reg, uint16_t v, bool is8);
    void branch(bool cond);
    void interrupt(uint16_t native_vec, uint16_t emu_vec);
};

}  // namespace famemu::snes
