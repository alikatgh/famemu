// famemu NES engine — 6502 CPU implementation. See cpu.hpp.
#include "cpu.hpp"

namespace famemu::nes {

// Base cycles per opcode. Page-cross (+1) and branch (+1/+2) penalties are
// added in step() for the opcodes they apply to.
static constexpr uint8_t kCycles[256] = {
    //  x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF
    /*0x*/ 7, 6, 0, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
    /*1x*/ 2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*2x*/ 6, 6, 0, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
    /*3x*/ 2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*4x*/ 6, 6, 0, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
    /*5x*/ 2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*6x*/ 6, 6, 0, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
    /*7x*/ 2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*8x*/ 2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    /*9x*/ 2, 6, 0, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    /*Ax*/ 2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    /*Bx*/ 2, 5, 0, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    /*Cx*/ 2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    /*Dx*/ 2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    /*Ex*/ 2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    /*Fx*/ 2, 5, 0, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
};

void Cpu6502::reset() {
    pc = rd16(0xFFFC);
    s = 0xFD;
    p = I | U;
    a = x = y = 0;
    cyc = 7;
}

void Cpu6502::nmi() {
    push(static_cast<uint8_t>(pc >> 8));
    push(static_cast<uint8_t>(pc));
    push((p & ~B) | U);
    setFlag(I, true);
    pc = rd16(0xFFFA);
    cyc += 7;
}

void Cpu6502::irq() {
    if (p & I) return;
    push(static_cast<uint8_t>(pc >> 8));
    push(static_cast<uint8_t>(pc));
    push((p & ~B) | U);
    setFlag(I, true);
    pc = rd16(0xFFFE);
    cyc += 7;
}

void Cpu6502::adc(uint8_t v) {
    uint16_t sum = static_cast<uint16_t>(a) + v + ((p & C) ? 1 : 0);
    uint8_t r = static_cast<uint8_t>(sum);
    setFlag(C, sum > 0xFF);
    setFlag(V, ((~(a ^ v)) & (a ^ r) & 0x80) != 0);  // same-sign operands, sign flipped
    a = r;
    setZN(a);
}

void Cpu6502::cmp(uint8_t reg, uint8_t v) {
    setFlag(C, reg >= v);
    setZN(static_cast<uint8_t>(reg - v));
}

uint8_t Cpu6502::asl(uint8_t v) {
    setFlag(C, v & 0x80);
    v <<= 1;
    setZN(v);
    return v;
}
uint8_t Cpu6502::lsr(uint8_t v) {
    setFlag(C, v & 0x01);
    v >>= 1;
    setZN(v);
    return v;
}
uint8_t Cpu6502::rol(uint8_t v) {
    bool c0 = p & C;
    setFlag(C, v & 0x80);
    v = static_cast<uint8_t>((v << 1) | (c0 ? 1 : 0));
    setZN(v);
    return v;
}
uint8_t Cpu6502::ror(uint8_t v) {
    bool c0 = p & C;
    setFlag(C, v & 0x01);
    v = static_cast<uint8_t>((v >> 1) | (c0 ? 0x80 : 0));
    setZN(v);
    return v;
}

void Cpu6502::branch(bool cond) {
    int8_t off = static_cast<int8_t>(fetch());
    if (!cond) return;
    ++cyc;
    uint16_t target = static_cast<uint16_t>(pc + off);
    if ((target & 0xFF00) != (pc & 0xFF00)) ++cyc;
    pc = target;
}

void Cpu6502::step() {
    const uint8_t op = fetch();
    cyc += kCycles[op];
    bool xc = false;  // page crossed by indexing (adds a cycle for read ops)

    // Read-modify-write with the 6502's write-back of the original value.
    auto rmw = [&](uint16_t ea, auto fn) {
        uint8_t v = rd(ea);
        wr(ea, v);
        v = fn(v);
        wr(ea, v);
        return v;
    };

    switch (op) {
        // ---- loads ----------------------------------------------------
        case 0xA9: a = fetch(); setZN(a); break;
        case 0xA5: a = rd(am_zp()); setZN(a); break;
        case 0xB5: a = rd(am_zpx()); setZN(a); break;
        case 0xAD: a = rd(am_abs()); setZN(a); break;
        case 0xBD: a = rd(am_abx(xc)); setZN(a); if (xc) ++cyc; break;
        case 0xB9: a = rd(am_aby(xc)); setZN(a); if (xc) ++cyc; break;
        case 0xA1: a = rd(am_izx()); setZN(a); break;
        case 0xB1: a = rd(am_izy(xc)); setZN(a); if (xc) ++cyc; break;

        case 0xA2: x = fetch(); setZN(x); break;
        case 0xA6: x = rd(am_zp()); setZN(x); break;
        case 0xB6: x = rd(am_zpy()); setZN(x); break;
        case 0xAE: x = rd(am_abs()); setZN(x); break;
        case 0xBE: x = rd(am_aby(xc)); setZN(x); if (xc) ++cyc; break;

        case 0xA0: y = fetch(); setZN(y); break;
        case 0xA4: y = rd(am_zp()); setZN(y); break;
        case 0xB4: y = rd(am_zpx()); setZN(y); break;
        case 0xAC: y = rd(am_abs()); setZN(y); break;
        case 0xBC: y = rd(am_abx(xc)); setZN(y); if (xc) ++cyc; break;

        // ---- stores ---------------------------------------------------
        case 0x85: wr(am_zp(), a); break;
        case 0x95: wr(am_zpx(), a); break;
        case 0x8D: wr(am_abs(), a); break;
        case 0x9D: wr(am_abx(xc), a); break;
        case 0x99: wr(am_aby(xc), a); break;
        case 0x81: wr(am_izx(), a); break;
        case 0x91: wr(am_izy(xc), a); break;
        case 0x86: wr(am_zp(), x); break;
        case 0x96: wr(am_zpy(), x); break;
        case 0x8E: wr(am_abs(), x); break;
        case 0x84: wr(am_zp(), y); break;
        case 0x94: wr(am_zpx(), y); break;
        case 0x8C: wr(am_abs(), y); break;

        // ---- transfers / stack ---------------------------------------
        case 0xAA: x = a; setZN(x); break;                        // TAX
        case 0xA8: y = a; setZN(y); break;                        // TAY
        case 0x8A: a = x; setZN(a); break;                        // TXA
        case 0x98: a = y; setZN(a); break;                        // TYA
        case 0xBA: x = s; setZN(x); break;                        // TSX
        case 0x9A: s = x; break;                                  // TXS
        case 0x48: push(a); break;                                // PHA
        case 0x08: push(p | B | U); break;                        // PHP
        case 0x68: a = pull(); setZN(a); break;                   // PLA
        case 0x28: p = (pull() & ~B) | U; break;                  // PLP

        // ---- logic ----------------------------------------------------
        case 0x29: a &= fetch(); setZN(a); break;
        case 0x25: a &= rd(am_zp()); setZN(a); break;
        case 0x35: a &= rd(am_zpx()); setZN(a); break;
        case 0x2D: a &= rd(am_abs()); setZN(a); break;
        case 0x3D: a &= rd(am_abx(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x39: a &= rd(am_aby(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x21: a &= rd(am_izx()); setZN(a); break;
        case 0x31: a &= rd(am_izy(xc)); setZN(a); if (xc) ++cyc; break;

        case 0x09: a |= fetch(); setZN(a); break;
        case 0x05: a |= rd(am_zp()); setZN(a); break;
        case 0x15: a |= rd(am_zpx()); setZN(a); break;
        case 0x0D: a |= rd(am_abs()); setZN(a); break;
        case 0x1D: a |= rd(am_abx(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x19: a |= rd(am_aby(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x01: a |= rd(am_izx()); setZN(a); break;
        case 0x11: a |= rd(am_izy(xc)); setZN(a); if (xc) ++cyc; break;

        case 0x49: a ^= fetch(); setZN(a); break;
        case 0x45: a ^= rd(am_zp()); setZN(a); break;
        case 0x55: a ^= rd(am_zpx()); setZN(a); break;
        case 0x4D: a ^= rd(am_abs()); setZN(a); break;
        case 0x5D: a ^= rd(am_abx(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x59: a ^= rd(am_aby(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x41: a ^= rd(am_izx()); setZN(a); break;
        case 0x51: a ^= rd(am_izy(xc)); setZN(a); if (xc) ++cyc; break;

        case 0x24: { uint8_t v = rd(am_zp());
            setFlag(Z, (a & v) == 0); setFlag(V, v & 0x40); setFlag(N, v & 0x80); break; }
        case 0x2C: { uint8_t v = rd(am_abs());
            setFlag(Z, (a & v) == 0); setFlag(V, v & 0x40); setFlag(N, v & 0x80); break; }

        // ---- arithmetic -----------------------------------------------
        case 0x69: adc(fetch()); break;
        case 0x65: adc(rd(am_zp())); break;
        case 0x75: adc(rd(am_zpx())); break;
        case 0x6D: adc(rd(am_abs())); break;
        case 0x7D: adc(rd(am_abx(xc))); if (xc) ++cyc; break;
        case 0x79: adc(rd(am_aby(xc))); if (xc) ++cyc; break;
        case 0x61: adc(rd(am_izx())); break;
        case 0x71: adc(rd(am_izy(xc))); if (xc) ++cyc; break;

        case 0xE9: case 0xEB: sbc(fetch()); break;   // EB = unofficial SBC imm
        case 0xE5: sbc(rd(am_zp())); break;
        case 0xF5: sbc(rd(am_zpx())); break;
        case 0xED: sbc(rd(am_abs())); break;
        case 0xFD: sbc(rd(am_abx(xc))); if (xc) ++cyc; break;
        case 0xF9: sbc(rd(am_aby(xc))); if (xc) ++cyc; break;
        case 0xE1: sbc(rd(am_izx())); break;
        case 0xF1: sbc(rd(am_izy(xc))); if (xc) ++cyc; break;

        case 0xC9: cmp(a, fetch()); break;
        case 0xC5: cmp(a, rd(am_zp())); break;
        case 0xD5: cmp(a, rd(am_zpx())); break;
        case 0xCD: cmp(a, rd(am_abs())); break;
        case 0xDD: cmp(a, rd(am_abx(xc))); if (xc) ++cyc; break;
        case 0xD9: cmp(a, rd(am_aby(xc))); if (xc) ++cyc; break;
        case 0xC1: cmp(a, rd(am_izx())); break;
        case 0xD1: cmp(a, rd(am_izy(xc))); if (xc) ++cyc; break;
        case 0xE0: cmp(x, fetch()); break;
        case 0xE4: cmp(x, rd(am_zp())); break;
        case 0xEC: cmp(x, rd(am_abs())); break;
        case 0xC0: cmp(y, fetch()); break;
        case 0xC4: cmp(y, rd(am_zp())); break;
        case 0xCC: cmp(y, rd(am_abs())); break;

        // ---- inc/dec ---------------------------------------------------
        case 0xE6: rmw(am_zp(),  [&](uint8_t v) { v++; setZN(v); return v; }); break;
        case 0xF6: rmw(am_zpx(), [&](uint8_t v) { v++; setZN(v); return v; }); break;
        case 0xEE: rmw(am_abs(), [&](uint8_t v) { v++; setZN(v); return v; }); break;
        case 0xFE: rmw(am_abx(xc), [&](uint8_t v) { v++; setZN(v); return v; }); break;
        case 0xC6: rmw(am_zp(),  [&](uint8_t v) { v--; setZN(v); return v; }); break;
        case 0xD6: rmw(am_zpx(), [&](uint8_t v) { v--; setZN(v); return v; }); break;
        case 0xCE: rmw(am_abs(), [&](uint8_t v) { v--; setZN(v); return v; }); break;
        case 0xDE: rmw(am_abx(xc), [&](uint8_t v) { v--; setZN(v); return v; }); break;
        case 0xE8: x++; setZN(x); break;
        case 0xC8: y++; setZN(y); break;
        case 0xCA: x--; setZN(x); break;
        case 0x88: y--; setZN(y); break;

        // ---- shifts / rotates -------------------------------------------
        case 0x0A: a = asl(a); break;
        case 0x06: rmw(am_zp(),  [&](uint8_t v) { return asl(v); }); break;
        case 0x16: rmw(am_zpx(), [&](uint8_t v) { return asl(v); }); break;
        case 0x0E: rmw(am_abs(), [&](uint8_t v) { return asl(v); }); break;
        case 0x1E: rmw(am_abx(xc), [&](uint8_t v) { return asl(v); }); break;
        case 0x4A: a = lsr(a); break;
        case 0x46: rmw(am_zp(),  [&](uint8_t v) { return lsr(v); }); break;
        case 0x56: rmw(am_zpx(), [&](uint8_t v) { return lsr(v); }); break;
        case 0x4E: rmw(am_abs(), [&](uint8_t v) { return lsr(v); }); break;
        case 0x5E: rmw(am_abx(xc), [&](uint8_t v) { return lsr(v); }); break;
        case 0x2A: a = rol(a); break;
        case 0x26: rmw(am_zp(),  [&](uint8_t v) { return rol(v); }); break;
        case 0x36: rmw(am_zpx(), [&](uint8_t v) { return rol(v); }); break;
        case 0x2E: rmw(am_abs(), [&](uint8_t v) { return rol(v); }); break;
        case 0x3E: rmw(am_abx(xc), [&](uint8_t v) { return rol(v); }); break;
        case 0x6A: a = ror(a); break;
        case 0x66: rmw(am_zp(),  [&](uint8_t v) { return ror(v); }); break;
        case 0x76: rmw(am_zpx(), [&](uint8_t v) { return ror(v); }); break;
        case 0x6E: rmw(am_abs(), [&](uint8_t v) { return ror(v); }); break;
        case 0x7E: rmw(am_abx(xc), [&](uint8_t v) { return ror(v); }); break;

        // ---- jumps / subroutines ----------------------------------------
        case 0x4C: pc = fetch16(); break;
        case 0x6C: {  // JMP (ind) with the 6502 page-wrap bug
            uint16_t ptr = fetch16();
            uint16_t lo = rd(ptr);
            uint16_t hi = rd(static_cast<uint16_t>((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
            pc = static_cast<uint16_t>(lo | (hi << 8));
            break;
        }
        case 0x20: {  // JSR: pushes address of the last operand byte
            uint16_t target = fetch16();
            uint16_t ret = static_cast<uint16_t>(pc - 1);
            push(static_cast<uint8_t>(ret >> 8));
            push(static_cast<uint8_t>(ret));
            pc = target;
            break;
        }
        case 0x60: {  // RTS
            uint16_t lo = pull(), hi = pull();
            pc = static_cast<uint16_t>((lo | (hi << 8)) + 1);
            break;
        }
        case 0x40: {  // RTI
            p = (pull() & ~B) | U;
            uint16_t lo = pull(), hi = pull();
            pc = static_cast<uint16_t>(lo | (hi << 8));
            break;
        }
        case 0x00: {  // BRK
            ++pc;  // padding byte
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc));
            push(p | B | U);
            setFlag(I, true);
            pc = rd16(0xFFFE);
            break;
        }

        // ---- branches ----------------------------------------------------
        case 0x90: branch(!(p & C)); break;  // BCC
        case 0xB0: branch(p & C); break;     // BCS
        case 0xD0: branch(!(p & Z)); break;  // BNE
        case 0xF0: branch(p & Z); break;     // BEQ
        case 0x10: branch(!(p & N)); break;  // BPL
        case 0x30: branch(p & N); break;     // BMI
        case 0x50: branch(!(p & V)); break;  // BVC
        case 0x70: branch(p & V); break;     // BVS

        // ---- flags --------------------------------------------------------
        case 0x18: setFlag(C, false); break;
        case 0x38: setFlag(C, true); break;
        case 0x58: setFlag(I, false); break;
        case 0x78: setFlag(I, true); break;
        case 0xD8: setFlag(D, false); break;
        case 0xF8: setFlag(D, true); break;
        case 0xB8: setFlag(V, false); break;

        // ---- NOPs (official + unofficial) ----------------------------------
        case 0xEA:
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
            break;
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
            fetch(); break;                                   // NOP imm
        case 0x04: case 0x44: case 0x64: am_zp(); break;      // NOP zp
        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
            am_zpx(); break;                                  // NOP zp,X
        case 0x0C: rd(am_abs()); break;                       // NOP abs
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
            rd(am_abx(xc)); if (xc) ++cyc; break;             // NOP abs,X

        // ---- unofficial: LAX / SAX -------------------------------------------
        case 0xA7: a = x = rd(am_zp()); setZN(a); break;
        case 0xB7: a = x = rd(am_zpy()); setZN(a); break;
        case 0xAF: a = x = rd(am_abs()); setZN(a); break;
        case 0xBF: a = x = rd(am_aby(xc)); setZN(a); if (xc) ++cyc; break;
        case 0xA3: a = x = rd(am_izx()); setZN(a); break;
        case 0xB3: a = x = rd(am_izy(xc)); setZN(a); if (xc) ++cyc; break;
        case 0x87: wr(am_zp(), a & x); break;
        case 0x97: wr(am_zpy(), a & x); break;
        case 0x8F: wr(am_abs(), a & x); break;
        case 0x83: wr(am_izx(), a & x); break;

        // ---- unofficial RMW combos --------------------------------------------
        // DCP = DEC then CMP
        case 0xC7: cmp(a, rmw(am_zp(),  [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        case 0xD7: cmp(a, rmw(am_zpx(), [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        case 0xCF: cmp(a, rmw(am_abs(), [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        case 0xDF: cmp(a, rmw(am_abx(xc), [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        case 0xDB: cmp(a, rmw(am_aby(xc), [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        case 0xC3: cmp(a, rmw(am_izx(),  [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        case 0xD3: cmp(a, rmw(am_izy(xc), [](uint8_t v) { return static_cast<uint8_t>(v - 1); })); break;
        // ISB (ISC) = INC then SBC
        case 0xE7: sbc(rmw(am_zp(),  [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        case 0xF7: sbc(rmw(am_zpx(), [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        case 0xEF: sbc(rmw(am_abs(), [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        case 0xFF: sbc(rmw(am_abx(xc), [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        case 0xFB: sbc(rmw(am_aby(xc), [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        case 0xE3: sbc(rmw(am_izx(),  [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        case 0xF3: sbc(rmw(am_izy(xc), [](uint8_t v) { return static_cast<uint8_t>(v + 1); })); break;
        // SLO = ASL then ORA
        case 0x07: a |= rmw(am_zp(),  [&](uint8_t v) { return asl(v); }); setZN(a); break;
        case 0x17: a |= rmw(am_zpx(), [&](uint8_t v) { return asl(v); }); setZN(a); break;
        case 0x0F: a |= rmw(am_abs(), [&](uint8_t v) { return asl(v); }); setZN(a); break;
        case 0x1F: a |= rmw(am_abx(xc), [&](uint8_t v) { return asl(v); }); setZN(a); break;
        case 0x1B: a |= rmw(am_aby(xc), [&](uint8_t v) { return asl(v); }); setZN(a); break;
        case 0x03: a |= rmw(am_izx(),  [&](uint8_t v) { return asl(v); }); setZN(a); break;
        case 0x13: a |= rmw(am_izy(xc), [&](uint8_t v) { return asl(v); }); setZN(a); break;
        // RLA = ROL then AND
        case 0x27: a &= rmw(am_zp(),  [&](uint8_t v) { return rol(v); }); setZN(a); break;
        case 0x37: a &= rmw(am_zpx(), [&](uint8_t v) { return rol(v); }); setZN(a); break;
        case 0x2F: a &= rmw(am_abs(), [&](uint8_t v) { return rol(v); }); setZN(a); break;
        case 0x3F: a &= rmw(am_abx(xc), [&](uint8_t v) { return rol(v); }); setZN(a); break;
        case 0x3B: a &= rmw(am_aby(xc), [&](uint8_t v) { return rol(v); }); setZN(a); break;
        case 0x23: a &= rmw(am_izx(),  [&](uint8_t v) { return rol(v); }); setZN(a); break;
        case 0x33: a &= rmw(am_izy(xc), [&](uint8_t v) { return rol(v); }); setZN(a); break;
        // SRE = LSR then EOR
        case 0x47: a ^= rmw(am_zp(),  [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        case 0x57: a ^= rmw(am_zpx(), [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        case 0x4F: a ^= rmw(am_abs(), [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        case 0x5F: a ^= rmw(am_abx(xc), [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        case 0x5B: a ^= rmw(am_aby(xc), [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        case 0x43: a ^= rmw(am_izx(),  [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        case 0x53: a ^= rmw(am_izy(xc), [&](uint8_t v) { return lsr(v); }); setZN(a); break;
        // RRA = ROR then ADC
        case 0x67: adc(rmw(am_zp(),  [&](uint8_t v) { return ror(v); })); break;
        case 0x77: adc(rmw(am_zpx(), [&](uint8_t v) { return ror(v); })); break;
        case 0x6F: adc(rmw(am_abs(), [&](uint8_t v) { return ror(v); })); break;
        case 0x7F: adc(rmw(am_abx(xc), [&](uint8_t v) { return ror(v); })); break;
        case 0x7B: adc(rmw(am_aby(xc), [&](uint8_t v) { return ror(v); })); break;
        case 0x63: adc(rmw(am_izx(),  [&](uint8_t v) { return ror(v); })); break;
        case 0x73: adc(rmw(am_izy(xc), [&](uint8_t v) { return ror(v); })); break;

        default:
            // Remaining unofficials (KIL, ANC, ALR, ARR, XAA, AHX, TAS, LAS,
            // AXS...) are not used by nestest or homebrew; treat as 2-cycle NOP
            // until a test ROM demands them.
            break;
    }
}

}  // namespace famemu::nes
