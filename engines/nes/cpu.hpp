// famemu NES engine — 6502 CPU (2A03 core: no BCD arithmetic).
//
// Clean-room implementation from public documentation (NESdev wiki, MOS
// datasheets, test-ROM behavior). Cycle-counted per instruction, including
// page-cross and branch penalties — verified instruction-for-instruction
// against the nestest golden log (see tests/nestest_main.cpp).
#pragma once

#include <cstdint>

namespace famemu::nes {

// The CPU's view of the machine. Implementations: nestest harness bus now,
// the full PPU/APU/cart bus as the engine grows.
struct Bus {
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t v) = 0;
    virtual ~Bus() = default;
};

class Cpu6502 {
public:
    // Status flag bits.
    static constexpr uint8_t C = 0x01, Z = 0x02, I = 0x04, D = 0x08;
    static constexpr uint8_t B = 0x10, U = 0x20, V = 0x40, N = 0x80;

    explicit Cpu6502(Bus& bus) : bus_(bus) {}

    void reset();          // PC <- [$FFFC], S -= 3, I set. 7 cycles.
    void step();           // execute one instruction
    void nmi();            // 7 cycles
    void irq();            // 7 cycles, masked by I

    // Registers are public: the tracer and save states read them directly.
    uint16_t pc = 0;
    uint8_t a = 0, x = 0, y = 0, s = 0xFD;
    uint8_t p = I | U;
    uint64_t cyc = 0;      // total cycles executed

private:
    Bus& bus_;

    // -- tiny helpers ---------------------------------------------------
    uint8_t rd(uint16_t addr) { return bus_.read(addr); }
    void wr(uint16_t addr, uint8_t v) { bus_.write(addr, v); }
    uint16_t rd16(uint16_t addr) {
        return static_cast<uint16_t>(rd(addr)) |
               (static_cast<uint16_t>(rd(static_cast<uint16_t>(addr + 1))) << 8);
    }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() { uint16_t v = rd16(pc); pc += 2; return v; }
    void push(uint8_t v) { wr(0x0100 | s--, v); }
    uint8_t pull() { return rd(0x0100 | ++s); }
    void setZN(uint8_t v) {
        p = (p & ~(Z | N)) | (v == 0 ? Z : 0) | (v & 0x80);
    }
    void setFlag(uint8_t f, bool on) { p = on ? (p | f) : (p & ~f); }

    // Addressing modes. Each returns the effective address; `crossed` is set
    // when an index carried into the high byte (the +1 cycle for read ops).
    uint16_t am_zp() { return fetch(); }
    uint16_t am_zpx() { return static_cast<uint8_t>(fetch() + x); }
    uint16_t am_zpy() { return static_cast<uint8_t>(fetch() + y); }
    uint16_t am_abs() { return fetch16(); }
    uint16_t am_abx(bool& crossed) {
        uint16_t b = fetch16(), ea = static_cast<uint16_t>(b + x);
        crossed = (b & 0xFF00) != (ea & 0xFF00);
        return ea;
    }
    uint16_t am_aby(bool& crossed) {
        uint16_t b = fetch16(), ea = static_cast<uint16_t>(b + y);
        crossed = (b & 0xFF00) != (ea & 0xFF00);
        return ea;
    }
    uint16_t am_izx() {
        uint8_t zp = static_cast<uint8_t>(fetch() + x);
        return static_cast<uint16_t>(rd(zp)) |
               (static_cast<uint16_t>(rd(static_cast<uint8_t>(zp + 1))) << 8);
    }
    uint16_t am_izy(bool& crossed) {
        uint8_t zp = fetch();
        uint16_t b = static_cast<uint16_t>(rd(zp)) |
                     (static_cast<uint16_t>(rd(static_cast<uint8_t>(zp + 1))) << 8);
        uint16_t ea = static_cast<uint16_t>(b + y);
        crossed = (b & 0xFF00) != (ea & 0xFF00);
        return ea;
    }

    // ALU building blocks (shared by official + unofficial opcodes).
    void adc(uint8_t v);
    void sbc(uint8_t v) { adc(static_cast<uint8_t>(~v)); }  // binary mode
    void cmp(uint8_t reg, uint8_t v);
    uint8_t asl(uint8_t v);
    uint8_t lsr(uint8_t v);
    uint8_t rol(uint8_t v);
    uint8_t ror(uint8_t v);
    void branch(bool cond);
};

}  // namespace famemu::nes
