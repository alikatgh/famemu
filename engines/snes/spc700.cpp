// famemu SNES engine — SPC700 implementation. See spc700.hpp.
#include "spc700.hpp"

#include "sdsp.hpp"

namespace famemu::snes {

uint8_t Spc700::io_read(uint8_t addr) {
    switch (addr) {
        case 0xF2: return dsp_addr;
        case 0xF3: return dsp_ ? dsp_->read(dsp_addr) : 0;
        case 0xF4: case 0xF5: case 0xF6: case 0xF7:
            return ports_in[addr - 0xF4];
        case 0xFD: case 0xFE: case 0xFF: {
            uint8_t v = timer_counter[addr - 0xFD];
            timer_counter[addr - 0xFD] = 0;  // cleared on read
            return v;
        }
        default: return 0;
    }
}

void Spc700::io_write(uint8_t addr, uint8_t v) {
    switch (addr) {
        case 0xF1:
            for (int t = 0; t < 3; ++t) {
                const bool was = timer_enable & (1 << t);
                const bool now = v & (1 << t);
                if (now && !was) { timer_stage[t] = 0; timer_counter[t] = 0; }
            }
            timer_enable = v & 7;
            if (v & 0x10) { ports_in[0] = ports_in[1] = 0; }
            if (v & 0x20) { ports_in[2] = ports_in[3] = 0; }
            break;
        case 0xF2: dsp_addr = v & 0x7F; break;
        case 0xF3: if (dsp_) dsp_->write(dsp_addr, v); break;
        case 0xF4: case 0xF5: case 0xF6: case 0xF7:
            ports_out[addr - 0xF4] = v;
            break;
        case 0xFA: case 0xFB: case 0xFC:
            timer_target[addr - 0xFA] = v;
            break;
        default: break;
    }
}

void Spc700::step() {
    const uint8_t op = fetch();
    switch (op) {
        case 0x8F: {  // MOV dp,#imm  (operands: imm, dp)
            uint8_t imm = fetch(), dp = fetch();
            wr(dp, imm);
            break;
        }
        case 0xE4: a = rd(fetch()); setZN(a); break;              // MOV A,dp
        case 0xC4: wr(fetch(), a); break;                         // MOV dp,A
        case 0xE8: a = fetch(); setZN(a); break;                  // MOV A,#imm
        case 0x8D: y = fetch(); setZN(y); break;                  // MOV Y,#imm
        case 0xFD: y = a; setZN(y); break;                        // MOV Y,A
        case 0xF7: {  // MOV A,[dp]+Y
            uint8_t dp = fetch();
            uint16_t ptr = static_cast<uint16_t>(rd(dp) | (rd((dp + 1) & 0xFF) << 8));
            a = rd(static_cast<uint16_t>(ptr + y));
            setZN(a);
            break;
        }
        case 0xF6: {  // MOV A,!abs+Y
            uint16_t abs = fetch();
            abs |= static_cast<uint16_t>(fetch()) << 8;
            a = rd(static_cast<uint16_t>(abs + y));
            setZN(a);
            break;
        }
        case 0x68: {  // CMP A,#imm
            uint8_t v = fetch();
            psw = (psw & ~C) | (a >= v ? C : 0);
            setZN(static_cast<uint8_t>(a - v));
            break;
        }
        case 0x64: {  // CMP A,dp
            uint8_t v = rd(fetch());
            psw = (psw & ~C) | (a >= v ? C : 0);
            setZN(static_cast<uint8_t>(a - v));
            break;
        }
        case 0x88: {  // ADC A,#imm
            uint8_t v = fetch();
            int sum = a + v + (psw & C ? 1 : 0);
            psw = (psw & ~C) | (sum > 0xFF ? C : 0);
            a = static_cast<uint8_t>(sum);
            setZN(a);
            break;
        }
        case 0x60: psw &= ~C; break;                              // CLRC
        case 0x1C:                                                // ASL A
            psw = (psw & ~C) | ((a & 0x80) ? C : 0);
            a <<= 1;
            setZN(a);
            break;
        case 0xFC: ++y; setZN(y); break;                          // INC Y
        case 0xDC: --y; setZN(y); break;                          // DEC Y
        case 0x9C: --a; setZN(a); break;                          // DEC A
        case 0x3A: {  // INCW dp
            uint8_t dp = fetch();
            uint16_t w = static_cast<uint16_t>(rd(dp) | (rd((dp + 1) & 0xFF) << 8));
            ++w;
            wr(dp, static_cast<uint8_t>(w));
            wr((dp + 1) & 0xFF, static_cast<uint8_t>(w >> 8));
            psw = (psw & ~(Z | N)) | (w ? 0 : Z) | ((w >> 8) & 0x80);
            break;
        }
        case 0xF0: { int8_t o = static_cast<int8_t>(fetch()); if (psw & Z) pc = static_cast<uint16_t>(pc + o); break; }
        case 0xD0: { int8_t o = static_cast<int8_t>(fetch()); if (!(psw & Z)) pc = static_cast<uint16_t>(pc + o); break; }
        case 0xB0: { int8_t o = static_cast<int8_t>(fetch()); if (psw & C) pc = static_cast<uint16_t>(pc + o); break; }
        case 0x2F: { int8_t o = static_cast<int8_t>(fetch()); pc = static_cast<uint16_t>(pc + o); break; }
        case 0x5F: { uint16_t t = fetch(); t |= static_cast<uint16_t>(fetch()) << 8; pc = t; break; }
        case 0x00: break;                                          // NOP
        default: {
            static int logged = 0;
            if (logged < 8) {
                std::fprintf(stderr,
                             "spc700: UNIMPLEMENTED opcode %02X at %04X — "
                             "extend the core (driver grew?)\n",
                             op, static_cast<unsigned>(pc - 1));
                ++logged;
            }
            break;
        }
    }
}

}  // namespace famemu::snes
