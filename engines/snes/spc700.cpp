// famemu SNES engine — SPC700 implementation (full instruction set).
// See spc700.hpp.
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

namespace {
// Standard published SPC700 cycle counts; branches add 2 when taken.
const uint8_t kCycles[256] = {
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 6, 8,
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 4, 6,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 4, 5, 4,
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 6, 5, 2, 2, 3, 8,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 6, 6,
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 4, 5, 2, 2, 4, 3,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 4, 5, 5,
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3, 6,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 6, 5, 4, 5, 2, 4, 5,
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 12, 5,
    3, 8, 4, 5, 3, 4, 3, 6, 2, 6, 4, 4, 5, 2, 4, 4,
    2, 8, 4, 5, 4, 5, 5, 6, 5, 5, 5, 5, 2, 2, 3, 4,
    3, 8, 4, 5, 4, 5, 4, 7, 2, 5, 6, 4, 5, 2, 4, 9,
    2, 8, 4, 5, 5, 6, 6, 7, 4, 5, 5, 5, 2, 2, 6, 3,
    2, 8, 4, 5, 3, 4, 3, 6, 2, 4, 5, 3, 4, 3, 4, 3,
    2, 8, 4, 5, 4, 5, 5, 6, 3, 4, 5, 4, 2, 2, 4, 3,
};
}  // namespace

int Spc700::step() {
    const uint8_t op = fetch();
    int cycles = kCycles[op];

    // ---- operand helpers -------------------------------------------------
    auto ea_dp = [&] { return dp(fetch()); };
    auto ea_dpx = [&] { return dp(static_cast<uint8_t>(fetch() + x)); };
    auto ea_dpy = [&] { return dp(static_cast<uint8_t>(fetch() + y)); };
    auto ea_abs = [&] {
        uint16_t a16 = fetch();
        return static_cast<uint16_t>(a16 | (fetch() << 8));
    };
    auto rd_dp_word = [&](uint8_t d) {
        return static_cast<uint16_t>(rd(dp(d)) |
                                     (rd(dp(static_cast<uint8_t>(d + 1))) << 8));
    };
    auto ea_idpx = [&] {  // [dp+X]
        const uint8_t p = static_cast<uint8_t>(fetch() + x);
        return static_cast<uint16_t>(rd(dp(p)) |
                                     (rd(dp(static_cast<uint8_t>(p + 1))) << 8));
    };
    auto ea_idpy = [&] {  // [dp]+Y
        const uint8_t p = fetch();
        const uint16_t base = static_cast<uint16_t>(
            rd(dp(p)) | (rd(dp(static_cast<uint8_t>(p + 1))) << 8));
        return static_cast<uint16_t>(base + y);
    };

    // ---- ALU helpers --------------------------------------------------------
    auto adc8 = [&](uint8_t a0, uint8_t b) -> uint8_t {
        const int c = (psw & C) ? 1 : 0;
        const int sum = a0 + b + c;
        setFlag(C, sum > 0xFF);
        setFlag(H, ((a0 & 0xF) + (b & 0xF) + c) > 0xF);
        setFlag(V, ((~(a0 ^ b)) & (a0 ^ sum) & 0x80) != 0);
        const uint8_t r = static_cast<uint8_t>(sum);
        setZN(r);
        return r;
    };
    auto sbc8 = [&](uint8_t a0, uint8_t b) -> uint8_t {
        return adc8(a0, static_cast<uint8_t>(~b));
    };
    auto cmp8 = [&](uint8_t a0, uint8_t b) {
        setFlag(C, a0 >= b);
        setZN(static_cast<uint8_t>(a0 - b));
    };
    auto or8 = [&](uint8_t a0, uint8_t b) { const uint8_t r = a0 | b; setZN(r); return r; };
    auto and8 = [&](uint8_t a0, uint8_t b) { const uint8_t r = a0 & b; setZN(r); return r; };
    auto eor8 = [&](uint8_t a0, uint8_t b) { const uint8_t r = a0 ^ b; setZN(r); return r; };
    auto asl8 = [&](uint8_t v) { setFlag(C, v & 0x80); v <<= 1; setZN(v); return v; };
    auto rol8 = [&](uint8_t v) {
        const uint8_t c = (psw & C) ? 1 : 0;
        setFlag(C, v & 0x80);
        v = static_cast<uint8_t>((v << 1) | c);
        setZN(v);
        return v;
    };
    auto lsr8 = [&](uint8_t v) { setFlag(C, v & 1); v >>= 1; setZN(v); return v; };
    auto ror8 = [&](uint8_t v) {
        const uint8_t c = (psw & C) ? 0x80 : 0;
        setFlag(C, v & 1);
        v = static_cast<uint8_t>((v >> 1) | c);
        setZN(v);
        return v;
    };
    auto inc8 = [&](uint8_t v) { ++v; setZN(v); return v; };
    auto dec8 = [&](uint8_t v) { --v; setZN(v); return v; };
    auto rmw = [&](uint16_t addr, auto f) { wr(addr, f(rd(addr))); };
    auto branch = [&](bool cond) {
        const int8_t o = static_cast<int8_t>(fetch());
        if (cond) { pc = static_cast<uint16_t>(pc + o); cycles += 2; }
    };
    auto membit = [&](uint16_t* addr, int* bit) {
        const uint16_t v = ea_abs();
        *addr = v & 0x1FFF;
        *bit = v >> 13;
    };
    // dp,dp and dp,#imm patterns: source operand comes first in the stream.
    auto op_dp_dp = [&](auto f, bool store) {
        const uint8_t s = fetch();
        const uint16_t daddr = dp(fetch());
        const uint8_t v = f(rd(daddr), rd(dp(s)));
        if (store) wr(daddr, v);
    };
    auto op_dp_imm = [&](auto f, bool store) {
        const uint8_t i = fetch();
        const uint16_t daddr = dp(fetch());
        const uint8_t v = f(rd(daddr), i);
        if (store) wr(daddr, v);
    };
    auto op_ix_iy = [&](auto f, bool store) {
        const uint8_t v = f(rd(dp(x)), rd(dp(y)));
        if (store) wr(dp(x), v);
    };

    switch (op) {
        case 0x00: break;                                              // NOP

        // ---- OR ----------------------------------------------------------
        case 0x08: a = or8(a, fetch()); break;
        case 0x06: a = or8(a, rd(dp(x))); break;
        case 0x04: a = or8(a, rd(ea_dp())); break;
        case 0x14: a = or8(a, rd(ea_dpx())); break;
        case 0x05: a = or8(a, rd(ea_abs())); break;
        case 0x15: a = or8(a, rd(static_cast<uint16_t>(ea_abs() + x))); break;
        case 0x16: a = or8(a, rd(static_cast<uint16_t>(ea_abs() + y))); break;
        case 0x07: a = or8(a, rd(ea_idpx())); break;
        case 0x17: a = or8(a, rd(ea_idpy())); break;
        case 0x09: op_dp_dp(or8, true); break;
        case 0x18: op_dp_imm(or8, true); break;
        case 0x19: op_ix_iy(or8, true); break;

        // ---- AND ------------------------------------------------------------
        case 0x28: a = and8(a, fetch()); break;
        case 0x26: a = and8(a, rd(dp(x))); break;
        case 0x24: a = and8(a, rd(ea_dp())); break;
        case 0x34: a = and8(a, rd(ea_dpx())); break;
        case 0x25: a = and8(a, rd(ea_abs())); break;
        case 0x35: a = and8(a, rd(static_cast<uint16_t>(ea_abs() + x))); break;
        case 0x36: a = and8(a, rd(static_cast<uint16_t>(ea_abs() + y))); break;
        case 0x27: a = and8(a, rd(ea_idpx())); break;
        case 0x37: a = and8(a, rd(ea_idpy())); break;
        case 0x29: op_dp_dp(and8, true); break;
        case 0x38: op_dp_imm(and8, true); break;
        case 0x39: op_ix_iy(and8, true); break;

        // ---- EOR ---------------------------------------------------------------
        case 0x48: a = eor8(a, fetch()); break;
        case 0x46: a = eor8(a, rd(dp(x))); break;
        case 0x44: a = eor8(a, rd(ea_dp())); break;
        case 0x54: a = eor8(a, rd(ea_dpx())); break;
        case 0x45: a = eor8(a, rd(ea_abs())); break;
        case 0x55: a = eor8(a, rd(static_cast<uint16_t>(ea_abs() + x))); break;
        case 0x56: a = eor8(a, rd(static_cast<uint16_t>(ea_abs() + y))); break;
        case 0x47: a = eor8(a, rd(ea_idpx())); break;
        case 0x57: a = eor8(a, rd(ea_idpy())); break;
        case 0x49: op_dp_dp(eor8, true); break;
        case 0x58: op_dp_imm(eor8, true); break;
        case 0x59: op_ix_iy(eor8, true); break;

        // ---- CMP ------------------------------------------------------------------
        case 0x68: cmp8(a, fetch()); break;
        case 0x66: cmp8(a, rd(dp(x))); break;
        case 0x64: cmp8(a, rd(ea_dp())); break;
        case 0x74: cmp8(a, rd(ea_dpx())); break;
        case 0x65: cmp8(a, rd(ea_abs())); break;
        case 0x75: cmp8(a, rd(static_cast<uint16_t>(ea_abs() + x))); break;
        case 0x76: cmp8(a, rd(static_cast<uint16_t>(ea_abs() + y))); break;
        case 0x67: cmp8(a, rd(ea_idpx())); break;
        case 0x77: cmp8(a, rd(ea_idpy())); break;
        case 0x69: op_dp_dp([&](uint8_t d, uint8_t s) { cmp8(d, s); return d; }, false); break;
        case 0x78: op_dp_imm([&](uint8_t d, uint8_t s) { cmp8(d, s); return d; }, false); break;
        case 0x79: op_ix_iy([&](uint8_t d, uint8_t s) { cmp8(d, s); return d; }, false); break;
        case 0xC8: cmp8(x, fetch()); break;
        case 0x3E: cmp8(x, rd(ea_dp())); break;
        case 0x1E: cmp8(x, rd(ea_abs())); break;
        case 0xAD: cmp8(y, fetch()); break;
        case 0x7E: cmp8(y, rd(ea_dp())); break;
        case 0x5E: cmp8(y, rd(ea_abs())); break;

        // ---- ADC -----------------------------------------------------------------------
        case 0x88: a = adc8(a, fetch()); break;
        case 0x86: a = adc8(a, rd(dp(x))); break;
        case 0x84: a = adc8(a, rd(ea_dp())); break;
        case 0x94: a = adc8(a, rd(ea_dpx())); break;
        case 0x85: a = adc8(a, rd(ea_abs())); break;
        case 0x95: a = adc8(a, rd(static_cast<uint16_t>(ea_abs() + x))); break;
        case 0x96: a = adc8(a, rd(static_cast<uint16_t>(ea_abs() + y))); break;
        case 0x87: a = adc8(a, rd(ea_idpx())); break;
        case 0x97: a = adc8(a, rd(ea_idpy())); break;
        case 0x89: op_dp_dp(adc8, true); break;
        case 0x98: op_dp_imm(adc8, true); break;
        case 0x99: op_ix_iy(adc8, true); break;

        // ---- SBC ------------------------------------------------------------------------
        case 0xA8: a = sbc8(a, fetch()); break;
        case 0xA6: a = sbc8(a, rd(dp(x))); break;
        case 0xA4: a = sbc8(a, rd(ea_dp())); break;
        case 0xB4: a = sbc8(a, rd(ea_dpx())); break;
        case 0xA5: a = sbc8(a, rd(ea_abs())); break;
        case 0xB5: a = sbc8(a, rd(static_cast<uint16_t>(ea_abs() + x))); break;
        case 0xB6: a = sbc8(a, rd(static_cast<uint16_t>(ea_abs() + y))); break;
        case 0xA7: a = sbc8(a, rd(ea_idpx())); break;
        case 0xB7: a = sbc8(a, rd(ea_idpy())); break;
        case 0xA9: op_dp_dp(sbc8, true); break;
        case 0xB8: op_dp_imm(sbc8, true); break;
        case 0xB9: op_ix_iy(sbc8, true); break;

        // ---- MOV A loads ---------------------------------------------------------------------
        case 0xE8: a = fetch(); setZN(a); break;
        case 0xE4: a = rd(ea_dp()); setZN(a); break;
        case 0xF4: a = rd(ea_dpx()); setZN(a); break;
        case 0xE5: a = rd(ea_abs()); setZN(a); break;
        case 0xF5: a = rd(static_cast<uint16_t>(ea_abs() + x)); setZN(a); break;
        case 0xF6: a = rd(static_cast<uint16_t>(ea_abs() + y)); setZN(a); break;
        case 0xE6: a = rd(dp(x)); setZN(a); break;
        case 0xBF: a = rd(dp(x)); ++x; setZN(a); break;                 // MOV A,(X)+
        case 0xE7: a = rd(ea_idpx()); setZN(a); break;
        case 0xF7: a = rd(ea_idpy()); setZN(a); break;

        // ---- MOV A stores (no flags) --------------------------------------------------------------
        case 0xC4: wr(ea_dp(), a); break;
        case 0xD4: wr(ea_dpx(), a); break;
        case 0xC5: wr(ea_abs(), a); break;
        case 0xD5: wr(static_cast<uint16_t>(ea_abs() + x), a); break;
        case 0xD6: wr(static_cast<uint16_t>(ea_abs() + y), a); break;
        case 0xC6: wr(dp(x), a); break;
        case 0xAF: wr(dp(x), a); ++x; break;                            // MOV (X)+,A
        case 0xC7: wr(ea_idpx(), a); break;
        case 0xD7: wr(ea_idpy(), a); break;

        // ---- MOV X / Y ------------------------------------------------------------------------------
        case 0xCD: x = fetch(); setZN(x); break;
        case 0xF8: x = rd(ea_dp()); setZN(x); break;
        case 0xF9: x = rd(ea_dpy()); setZN(x); break;
        case 0xE9: x = rd(ea_abs()); setZN(x); break;
        case 0xD8: wr(ea_dp(), x); break;
        case 0xD9: wr(ea_dpy(), x); break;
        case 0xC9: wr(ea_abs(), x); break;
        case 0x8D: y = fetch(); setZN(y); break;
        case 0xEB: y = rd(ea_dp()); setZN(y); break;
        case 0xFB: y = rd(ea_dpx()); setZN(y); break;
        case 0xEC: y = rd(ea_abs()); setZN(y); break;
        case 0xCB: wr(ea_dp(), y); break;
        case 0xDB: wr(ea_dpx(), y); break;
        case 0xCC: wr(ea_abs(), y); break;

        // ---- register moves ---------------------------------------------------------------------------
        case 0x7D: a = x; setZN(a); break;
        case 0xDD: a = y; setZN(a); break;
        case 0x5D: x = a; setZN(x); break;
        case 0xFD: y = a; setZN(y); break;
        case 0x9D: x = sp; setZN(x); break;
        case 0xBD: sp = x; break;

        // ---- MOV memory,memory ---------------------------------------------------------------------------
        case 0xFA: {  // MOV dp,dp
            const uint8_t s = fetch();
            const uint16_t daddr = dp(fetch());
            wr(daddr, rd(dp(s)));
            break;
        }
        case 0x8F: {  // MOV dp,#imm
            const uint8_t i = fetch();
            wr(dp(fetch()), i);
            break;
        }

        // ---- shifts / rotates / inc / dec ----------------------------------------------------------------
        case 0x1C: a = asl8(a); break;
        case 0x0B: rmw(ea_dp(), asl8); break;
        case 0x1B: rmw(ea_dpx(), asl8); break;
        case 0x0C: rmw(ea_abs(), asl8); break;
        case 0x3C: a = rol8(a); break;
        case 0x2B: rmw(ea_dp(), rol8); break;
        case 0x3B: rmw(ea_dpx(), rol8); break;
        case 0x2C: rmw(ea_abs(), rol8); break;
        case 0x5C: a = lsr8(a); break;
        case 0x4B: rmw(ea_dp(), lsr8); break;
        case 0x5B: rmw(ea_dpx(), lsr8); break;
        case 0x4C: rmw(ea_abs(), lsr8); break;
        case 0x7C: a = ror8(a); break;
        case 0x6B: rmw(ea_dp(), ror8); break;
        case 0x7B: rmw(ea_dpx(), ror8); break;
        case 0x6C: rmw(ea_abs(), ror8); break;
        case 0xBC: a = inc8(a); break;
        case 0xAB: rmw(ea_dp(), inc8); break;
        case 0xBB: rmw(ea_dpx(), inc8); break;
        case 0xAC: rmw(ea_abs(), inc8); break;
        case 0x3D: x = inc8(x); break;
        case 0xFC: y = inc8(y); break;
        case 0x9C: a = dec8(a); break;
        case 0x8B: rmw(ea_dp(), dec8); break;
        case 0x9B: rmw(ea_dpx(), dec8); break;
        case 0x8C: rmw(ea_abs(), dec8); break;
        case 0x1D: x = dec8(x); break;
        case 0xDC: y = dec8(y); break;
        case 0x9F: a = static_cast<uint8_t>((a >> 4) | (a << 4)); setZN(a); break;  // XCN

        // ---- 16-bit --------------------------------------------------------------------------------------------
        case 0xBA: {  // MOVW YA,dp
            const uint8_t d = fetch();
            const uint16_t w = rd_dp_word(d);
            a = static_cast<uint8_t>(w);
            y = static_cast<uint8_t>(w >> 8);
            setZN16(w);
            break;
        }
        case 0xDA: {  // MOVW dp,YA
            const uint8_t d = fetch();
            wr(dp(d), a);
            wr(dp(static_cast<uint8_t>(d + 1)), y);
            break;
        }
        case 0x3A: {  // INCW dp
            const uint8_t d = fetch();
            uint16_t w = static_cast<uint16_t>(rd_dp_word(d) + 1);
            wr(dp(d), static_cast<uint8_t>(w));
            wr(dp(static_cast<uint8_t>(d + 1)), static_cast<uint8_t>(w >> 8));
            setZN16(w);
            break;
        }
        case 0x1A: {  // DECW dp
            const uint8_t d = fetch();
            uint16_t w = static_cast<uint16_t>(rd_dp_word(d) - 1);
            wr(dp(d), static_cast<uint8_t>(w));
            wr(dp(static_cast<uint8_t>(d + 1)), static_cast<uint8_t>(w >> 8));
            setZN16(w);
            break;
        }
        case 0x7A: {  // ADDW YA,dp
            const uint16_t w = rd_dp_word(fetch());
            const uint32_t ya = static_cast<uint32_t>(a) | (y << 8);
            const uint32_t sum = ya + w;
            setFlag(C, sum > 0xFFFF);
            setFlag(V, ((~(ya ^ w)) & (ya ^ sum) & 0x8000) != 0);
            setFlag(H, ((ya & 0xFFF) + (w & 0xFFF)) > 0xFFF);
            a = static_cast<uint8_t>(sum);
            y = static_cast<uint8_t>(sum >> 8);
            setZN16(static_cast<uint16_t>(sum));
            break;
        }
        case 0x9A: {  // SUBW YA,dp
            const uint16_t w = rd_dp_word(fetch());
            const uint32_t ya = static_cast<uint32_t>(a) | (y << 8);
            const int32_t diff = static_cast<int32_t>(ya) - w;
            setFlag(C, diff >= 0);
            setFlag(V, (((ya ^ w) & (ya ^ static_cast<uint32_t>(diff))) & 0x8000) != 0);
            setFlag(H, (ya & 0xFFF) >= (w & 0xFFF));
            a = static_cast<uint8_t>(diff);
            y = static_cast<uint8_t>(diff >> 8);
            setZN16(static_cast<uint16_t>(diff));
            break;
        }
        case 0x5A: {  // CMPW YA,dp
            const uint16_t w = rd_dp_word(fetch());
            const uint32_t ya = static_cast<uint32_t>(a) | (y << 8);
            const int32_t diff = static_cast<int32_t>(ya) - w;
            setFlag(C, diff >= 0);
            setZN16(static_cast<uint16_t>(diff));
            break;
        }
        case 0xCF: {  // MUL YA
            const uint16_t prod = static_cast<uint16_t>(y * a);
            a = static_cast<uint8_t>(prod);
            y = static_cast<uint8_t>(prod >> 8);
            setZN(y);
            break;
        }
        case 0x9E: {  // DIV YA,X
            const uint32_t ya = static_cast<uint32_t>(a) | (y << 8);
            setFlag(V, y >= x);
            setFlag(H, (y & 15) >= (x & 15));
            if (y < static_cast<int>(x) << 1) {
                a = static_cast<uint8_t>(x ? ya / x : 0xFF);
                y = static_cast<uint8_t>(x ? ya % x : y);
            } else {
                a = static_cast<uint8_t>(255 - (ya - (x << 9)) / (256 - x));
                y = static_cast<uint8_t>(x + (ya - (x << 9)) % (256 - x));
            }
            setZN(a);
            break;
        }
        case 0xDF:  // DAA
            if ((psw & C) || a > 0x99) { a = static_cast<uint8_t>(a + 0x60); setFlag(C, true); }
            if ((psw & H) || (a & 0x0F) > 0x09) a = static_cast<uint8_t>(a + 0x06);
            setZN(a);
            break;
        case 0xBE:  // DAS
            if (!(psw & C) || a > 0x99) { a = static_cast<uint8_t>(a - 0x60); setFlag(C, false); }
            if (!(psw & H) || (a & 0x0F) > 0x09) a = static_cast<uint8_t>(a - 0x06);
            setZN(a);
            break;

        // ---- branches -----------------------------------------------------------------------------------------------
        case 0x10: branch(!(psw & N)); break;
        case 0x30: branch(psw & N); break;
        case 0x50: branch(!(psw & V)); break;
        case 0x70: branch(psw & V); break;
        case 0x90: branch(!(psw & C)); break;
        case 0xB0: branch(psw & C); break;
        case 0xD0: branch(!(psw & Z)); break;
        case 0xF0: branch(psw & Z); break;
        case 0x2F: branch(true); break;                                 // BRA
        case 0x2E: { const uint8_t v = rd(ea_dp()); branch(a != v); break; }      // CBNE dp
        case 0xDE: { const uint8_t v = rd(ea_dpx()); branch(a != v); break; }     // CBNE dp+X
        case 0x6E: {  // DBNZ dp
            const uint16_t d = ea_dp();
            const uint8_t v = static_cast<uint8_t>(rd(d) - 1);
            wr(d, v);
            branch(v != 0);
            break;
        }
        case 0xFE: --y; branch(y != 0); break;                          // DBNZ Y

        // ---- bit-addressed ops -------------------------------------------------------------------------------------------
        case 0x03: case 0x23: case 0x43: case 0x63:
        case 0x83: case 0xA3: case 0xC3: case 0xE3: {                   // BBS n
            const uint8_t v = rd(ea_dp());
            branch(v & (1 << (op >> 5)));
            break;
        }
        case 0x13: case 0x33: case 0x53: case 0x73:
        case 0x93: case 0xB3: case 0xD3: case 0xF3: {                   // BBC n
            const uint8_t v = rd(ea_dp());
            branch(!(v & (1 << (op >> 5))));
            break;
        }
        case 0x02: case 0x22: case 0x42: case 0x62:
        case 0x82: case 0xA2: case 0xC2: case 0xE2:                     // SET1 n
            rmw(ea_dp(), [&](uint8_t v) { return static_cast<uint8_t>(v | (1 << (op >> 5))); });
            break;
        case 0x12: case 0x32: case 0x52: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:                     // CLR1 n
            rmw(ea_dp(), [&](uint8_t v) { return static_cast<uint8_t>(v & ~(1 << (op >> 5))); });
            break;
        case 0x0A: { uint16_t ad; int b; membit(&ad, &b); if ((rd(ad) >> b) & 1) psw |= C; break; }        // OR1
        case 0x2A: { uint16_t ad; int b; membit(&ad, &b); if (!((rd(ad) >> b) & 1)) psw |= C; break; }     // OR1 /
        case 0x4A: { uint16_t ad; int b; membit(&ad, &b); if (!((rd(ad) >> b) & 1)) psw &= ~C; break; }    // AND1
        case 0x6A: { uint16_t ad; int b; membit(&ad, &b); if ((rd(ad) >> b) & 1) psw &= ~C; break; }       // AND1 /
        case 0x8A: { uint16_t ad; int b; membit(&ad, &b); if ((rd(ad) >> b) & 1) psw ^= C; break; }        // EOR1
        case 0xAA: { uint16_t ad; int b; membit(&ad, &b); setFlag(C, (rd(ad) >> b) & 1); break; }          // MOV1 C,m
        case 0xCA: {                                                    // MOV1 m,C
            uint16_t ad; int b;
            membit(&ad, &b);
            uint8_t v = rd(ad);
            v = static_cast<uint8_t>((psw & C) ? (v | (1 << b)) : (v & ~(1 << b)));
            wr(ad, v);
            break;
        }
        case 0xEA: {                                                    // NOT1
            uint16_t ad; int b;
            membit(&ad, &b);
            wr(ad, static_cast<uint8_t>(rd(ad) ^ (1 << b)));
            break;
        }
        case 0x0E: {                                                    // TSET1
            const uint16_t ad = ea_abs();
            const uint8_t v = rd(ad);
            setZN(static_cast<uint8_t>(a - v));
            wr(ad, static_cast<uint8_t>(v | a));
            break;
        }
        case 0x4E: {                                                    // TCLR1
            const uint16_t ad = ea_abs();
            const uint8_t v = rd(ad);
            setZN(static_cast<uint8_t>(a - v));
            wr(ad, static_cast<uint8_t>(v & ~a));
            break;
        }

        // ---- stack ------------------------------------------------------------------------------------------------------------
        case 0x0D: push(psw); break;
        case 0x2D: push(a); break;
        case 0x4D: push(x); break;
        case 0x6D: push(y); break;
        case 0x8E: psw = pull(); break;
        case 0xAE: a = pull(); break;
        case 0xCE: x = pull(); break;
        case 0xEE: y = pull(); break;

        // ---- calls / jumps -----------------------------------------------------------------------------------------------------
        case 0x5F: pc = ea_abs(); break;                                // JMP !abs
        case 0x1F: {                                                    // JMP [!abs+X]
            const uint16_t t = static_cast<uint16_t>(ea_abs() + x);
            pc = static_cast<uint16_t>(rd(t) | (rd(static_cast<uint16_t>(t + 1)) << 8));
            break;
        }
        case 0x3F: {                                                    // CALL !abs
            const uint16_t t = ea_abs();
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc));
            pc = t;
            break;
        }
        case 0x4F: {                                                    // PCALL up
            const uint8_t u = fetch();
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc));
            pc = static_cast<uint16_t>(0xFF00 | u);
            break;
        }
        case 0x01: case 0x11: case 0x21: case 0x31:
        case 0x41: case 0x51: case 0x61: case 0x71:
        case 0x81: case 0x91: case 0xA1: case 0xB1:
        case 0xC1: case 0xD1: case 0xE1: case 0xF1: {                   // TCALL n
            const uint16_t vec = static_cast<uint16_t>(0xFFDE - 2 * (op >> 4));
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc));
            pc = static_cast<uint16_t>(rd(vec) | (rd(static_cast<uint16_t>(vec + 1)) << 8));
            break;
        }
        case 0x0F:                                                      // BRK
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc));
            push(psw);
            psw = static_cast<uint8_t>((psw | B) & ~I);
            pc = static_cast<uint16_t>(rd(0xFFDE) | (rd(0xFFDF) << 8));
            break;
        case 0x6F: {                                                    // RET
            const uint8_t lo = pull();
            pc = static_cast<uint16_t>(lo | (pull() << 8));
            break;
        }
        case 0x7F: {                                                    // RETI
            psw = pull();
            const uint8_t lo = pull();
            pc = static_cast<uint16_t>(lo | (pull() << 8));
            break;
        }

        // ---- flags -----------------------------------------------------------------------------------------------------------------
        case 0x60: psw &= ~C; break;                                    // CLRC
        case 0x80: psw |= C; break;                                     // SETC
        case 0xED: psw ^= C; break;                                     // NOTC
        case 0xE0: psw &= static_cast<uint8_t>(~(V | H)); break;        // CLRV
        case 0x20: psw &= ~P; break;                                    // CLRP
        case 0x40: psw |= P; break;                                     // SETP
        case 0xA0: psw |= I; break;                                     // EI
        case 0xC0: psw &= ~I; break;                                    // DI

        // ---- halt ------------------------------------------------------------------------------------------------------------------
        case 0xEF:                                                      // SLEEP
        case 0xFF:                                                      // STOP
            running = false;
            break;

        default: break;  // unreachable: all 256 opcodes handled
    }
    return cycles;
}

}  // namespace famemu::snes
