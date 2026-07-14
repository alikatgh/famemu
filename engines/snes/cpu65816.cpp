// famemu SNES engine — 65816 implementation. See cpu65816.hpp.
//
// Decimal (BCD) mode is not implemented yet: the D flag is tracked but ADC/
// SBC always compute binary. KORA never sets D; revisit with test ROMs.
#include "cpu65816.hpp"

namespace famemu::snes {

void Cpu65816::reset() {
    e = true;
    p = M | X | I;
    d = 0;
    dbr = pbr = 0;
    s = 0x01FF;
    a = x = y = 0;
    waiting = stopped = false;
    pc = static_cast<uint16_t>(bus_.read(0xFFFC) | (bus_.read(0xFFFD) << 8));
    cyc = 8;
}

void Cpu65816::interrupt(uint16_t native_vec, uint16_t emu_vec) {
    waiting = false;
    if (!e) push8(pbr);
    push16(pc);
    push8(e ? static_cast<uint8_t>(p | 0x30) : p);
    setFlag(I, true);
    setFlag(D, false);
    pbr = 0;
    const uint16_t vec = e ? emu_vec : native_vec;
    pc = static_cast<uint16_t>(rd(vec) | (rd(vec + 1) << 8));
    cyc += 2;
}

void Cpu65816::nmi() { interrupt(0xFFEA, 0xFFFA); }
void Cpu65816::irq() {
    if (p & I) { waiting = false; return; }  // WAI still wakes
    interrupt(0xFFEE, 0xFFFE);
}

void Cpu65816::adc(uint16_t v) {
    if (p & D) { adc_decimal(v, false); return; }
    if (m8()) {
        uint8_t a8 = static_cast<uint8_t>(a), v8 = static_cast<uint8_t>(v);
        uint16_t sum = static_cast<uint16_t>(a8 + v8 + ((p & C) ? 1 : 0));
        setFlag(C, sum > 0xFF);
        uint8_t r = static_cast<uint8_t>(sum);
        setFlag(V, ((~(a8 ^ v8)) & (a8 ^ r) & 0x80) != 0);
        a = (a & 0xFF00) | r;
        setZN8(r);
    } else {
        uint32_t sum = static_cast<uint32_t>(a) + v + ((p & C) ? 1 : 0);
        setFlag(C, sum > 0xFFFF);
        uint16_t r = static_cast<uint16_t>(sum);
        setFlag(V, ((~(a ^ v)) & (a ^ r) & 0x8000) != 0);
        a = r;
        setZN16(r);
    }
}

// Decimal mode, nibble-chained like the silicon; the subtract path receives
// the operand pre-inverted (borrow style) and corrects downward.
void Cpu65816::adc_decimal(uint16_t v, bool subtract) {
    const int digits = m8() ? 2 : 4;
    const uint16_t av = m8() ? (a & 0xFF) : a;
    if (subtract) v = m8() ? (v ^ 0xFF) : (v ^ 0xFFFF);
    uint32_t result = 0;
    uint32_t carry = (p & C) ? 1 : 0;
    const uint32_t sign = m8() ? 0x80 : 0x8000;
    bool v_flag = false;
    for (int d = 0; d < digits; ++d) {
        const int shift = d * 4;
        uint32_t sum = ((av >> shift) & 0xF) + ((v >> shift) & 0xF) + carry;
        if (d == digits - 1) {
            // V is derived from the top digit's PRE-adjustment binary sum
            // (verified by CPUADC.sfc: $49+$51 BCD -> $00 with V=1).
            const uint32_t inter = (sum << shift) & sign;
            v_flag = ((~(av ^ v)) & (av ^ inter) & sign) != 0;
        }
        if (!subtract) {
            if (sum > 9) sum += 6;
            carry = sum > 0xF;
        } else {
            if (sum <= 0xF) sum -= 6;     // borrow correction
            carry = sum > 0xF;            // (sum wrapped if borrowed)
        }
        result |= (sum & 0xF) << shift;
    }
    setFlag(V, v_flag);
    setFlag(C, carry != 0);
    if (m8()) {
        a = (a & 0xFF00) | (result & 0xFF);
        setZN8(static_cast<uint8_t>(result));
    } else {
        a = static_cast<uint16_t>(result);
        setZN16(static_cast<uint16_t>(result));
    }
}

void Cpu65816::sbc(uint16_t v) {
    if (p & D) { adc_decimal(v, true); return; }
    adc(m8() ? (v ^ 0xFF) : (v ^ 0xFFFF));
}

void Cpu65816::cmp_gen(uint16_t reg, uint16_t v, bool is8) {
    if (is8) {
        uint8_t r8 = static_cast<uint8_t>(reg), v8 = static_cast<uint8_t>(v);
        setFlag(C, r8 >= v8);
        setZN8(static_cast<uint8_t>(r8 - v8));
    } else {
        setFlag(C, reg >= v);
        setZN16(static_cast<uint16_t>(reg - v));
    }
}

void Cpu65816::branch(bool cond) {
    int8_t off = static_cast<int8_t>(fetch());
    if (!cond) return;
    ++cyc;
    pc = static_cast<uint16_t>(pc + off);
}

void Cpu65816::step() {
    if (stopped || waiting) { cyc += 2; return; }
    const uint8_t op = fetch();
    ++cyc;  // baseline internal cycle (coarse model; see header)

    // ---- generic op helpers bound to this instruction ----
    auto A8 = [&]() -> uint8_t { return static_cast<uint8_t>(a); };
    auto lda = [&](uint32_t ea) { uint16_t v = load_m(ea); if (m8()) { a = (a & 0xFF00) | v; setZN8(static_cast<uint8_t>(v)); } else { a = v; setZN16(v); } };
    auto ora = [&](uint32_t ea) { uint16_t v = load_m(ea); if (m8()) { a = (a & 0xFF00) | ((a | v) & 0xFF); setZN8(A8()); } else { a |= v; setZN16(a); } };
    auto anda = [&](uint32_t ea) { uint16_t v = load_m(ea); if (m8()) { a = (a & 0xFF00) | ((a & v) & 0xFF); setZN8(A8()); } else { a &= v; setZN16(a); } };
    auto eora = [&](uint32_t ea) { uint16_t v = load_m(ea); if (m8()) { a = (a & 0xFF00) | ((a ^ v) & 0xFF); setZN8(A8()); } else { a ^= v; setZN16(a); } };
    auto bit = [&](uint32_t ea) {
        uint16_t v = load_m(ea);
        if (m8()) {
            uint8_t v8 = static_cast<uint8_t>(v);
            setFlag(Z, (A8() & v8) == 0);
            setFlag(V, v8 & 0x40);
            setFlag(N, v8 & 0x80);
        } else {
            setFlag(Z, (a & v) == 0);
            setFlag(V, v & 0x4000);
            setFlag(N, v & 0x8000);
        }
    };
    // memory RMW honoring M
    auto rmw = [&](uint32_t ea, auto fn) {
        if (m8()) { uint8_t v = rd(ea); v = static_cast<uint8_t>(fn(v, true)); wr(ea, v); }
        else { uint16_t v = rd16(ea); v = fn(v, false); wr16(ea, v); }
        ++cyc;
    };
    auto op_asl = [&](uint16_t v, bool is8) -> uint16_t {
        setFlag(C, v & (is8 ? 0x80 : 0x8000));
        v = static_cast<uint16_t>(is8 ? ((v << 1) & 0xFF) : (v << 1));
        setZN(v, is8);
        return v;
    };
    auto op_lsr = [&](uint16_t v, bool is8) -> uint16_t {
        setFlag(C, v & 1);
        v = static_cast<uint16_t>((is8 ? (v & 0xFF) : v) >> 1);
        setZN(v, is8);
        return v;
    };
    auto op_rol = [&](uint16_t v, bool is8) -> uint16_t {
        bool c0 = p & C;
        setFlag(C, v & (is8 ? 0x80 : 0x8000));
        v = static_cast<uint16_t>(is8 ? (((v << 1) | (c0 ? 1 : 0)) & 0xFF)
                                      : ((v << 1) | (c0 ? 1 : 0)));
        setZN(v, is8);
        return v;
    };
    auto op_ror = [&](uint16_t v, bool is8) -> uint16_t {
        bool c0 = p & C;
        setFlag(C, v & 1);
        v = static_cast<uint16_t>((is8 ? (v & 0xFF) : v) >> 1);
        if (c0) v |= is8 ? 0x80 : 0x8000;
        setZN(v, is8);
        return v;
    };
    auto op_inc = [&](uint16_t v, bool is8) -> uint16_t {
        v = static_cast<uint16_t>(is8 ? ((v + 1) & 0xFF) : (v + 1));
        setZN(v, is8);
        return v;
    };
    auto op_dec = [&](uint16_t v, bool is8) -> uint16_t {
        v = static_cast<uint16_t>(is8 ? ((v - 1) & 0xFF) : (v - 1));
        setZN(v, is8);
        return v;
    };
    auto acc_rmw = [&](auto fn) {
        if (m8()) a = (a & 0xFF00) | fn(A8(), true);
        else a = fn(a, false);
    };
    auto imm_m = [&]() -> uint16_t { return m8() ? fetch() : fetch16(); };
    auto imm_x = [&]() -> uint16_t { return x8() ? fetch() : fetch16(); };

    switch (op) {
        // ---- LDA -------------------------------------------------------
        case 0xA9: { uint16_t v = imm_m(); if (m8()) { a = (a & 0xFF00) | v; setZN8(static_cast<uint8_t>(v)); } else { a = v; setZN16(v); } break; }
        case 0xA5: lda(am_dp()); break;
        case 0xB5: lda(am_dpx()); break;
        case 0xAD: lda(am_abs()); break;
        case 0xBD: lda(am_absx()); break;
        case 0xB9: lda(am_absy()); break;
        case 0xAF: lda(am_long()); break;
        case 0xBF: lda(am_longx()); break;
        case 0xA1: lda(am_ind_dpx()); break;
        case 0xB1: lda(am_ind_dpy()); break;
        case 0xB2: lda(am_ind_dp()); break;
        case 0xA7: lda(am_indl_dp()); break;
        case 0xB7: lda(am_indl_dpy()); break;
        case 0xA3: lda(am_sr()); break;
        case 0xB3: lda(am_sr_y()); break;

        // ---- LDX / LDY ----------------------------------------------------
        case 0xA2: { uint16_t v = imm_x(); x = v; setZN(v, x8()); break; }
        case 0xA6: { uint16_t v = load_x(am_dp()); x = v; setZN(v, x8()); break; }
        case 0xB6: { uint16_t v = load_x(am_dpy()); x = v; setZN(v, x8()); break; }
        case 0xAE: { uint16_t v = load_x(am_abs()); x = v; setZN(v, x8()); break; }
        case 0xBE: { uint16_t v = load_x(am_absy()); x = v; setZN(v, x8()); break; }
        case 0xA0: { uint16_t v = imm_x(); y = v; setZN(v, x8()); break; }
        case 0xA4: { uint16_t v = load_x(am_dp()); y = v; setZN(v, x8()); break; }
        case 0xB4: { uint16_t v = load_x(am_dpx()); y = v; setZN(v, x8()); break; }
        case 0xAC: { uint16_t v = load_x(am_abs()); y = v; setZN(v, x8()); break; }
        case 0xBC: { uint16_t v = load_x(am_absx()); y = v; setZN(v, x8()); break; }

        // ---- STA / STX / STY / STZ ------------------------------------------
        case 0x85: store_m(am_dp(), a); break;
        case 0x95: store_m(am_dpx(), a); break;
        case 0x8D: store_m(am_abs(), a); break;
        case 0x9D: store_m(am_absx(), a); break;
        case 0x99: store_m(am_absy(), a); break;
        case 0x8F: store_m(am_long(), a); break;
        case 0x9F: store_m(am_longx(), a); break;
        case 0x81: store_m(am_ind_dpx(), a); break;
        case 0x91: store_m(am_ind_dpy(), a); break;
        case 0x92: store_m(am_ind_dp(), a); break;
        case 0x87: store_m(am_indl_dp(), a); break;
        case 0x97: store_m(am_indl_dpy(), a); break;
        case 0x83: store_m(am_sr(), a); break;
        case 0x93: store_m(am_sr_y(), a); break;
        case 0x86: { uint32_t ea = am_dp(); if (x8()) wr(ea, static_cast<uint8_t>(x)); else wr16(ea, x); break; }
        case 0x96: { uint32_t ea = am_dpy(); if (x8()) wr(ea, static_cast<uint8_t>(x)); else wr16(ea, x); break; }
        case 0x8E: { uint32_t ea = am_abs(); if (x8()) wr(ea, static_cast<uint8_t>(x)); else wr16(ea, x); break; }
        case 0x84: { uint32_t ea = am_dp(); if (x8()) wr(ea, static_cast<uint8_t>(y)); else wr16(ea, y); break; }
        case 0x94: { uint32_t ea = am_dpx(); if (x8()) wr(ea, static_cast<uint8_t>(y)); else wr16(ea, y); break; }
        case 0x8C: { uint32_t ea = am_abs(); if (x8()) wr(ea, static_cast<uint8_t>(y)); else wr16(ea, y); break; }
        case 0x64: { uint32_t ea = am_dp(); if (m8()) wr(ea, 0); else wr16(ea, 0); break; }
        case 0x74: { uint32_t ea = am_dpx(); if (m8()) wr(ea, 0); else wr16(ea, 0); break; }
        case 0x9C: { uint32_t ea = am_abs(); if (m8()) wr(ea, 0); else wr16(ea, 0); break; }
        case 0x9E: { uint32_t ea = am_absx(); if (m8()) wr(ea, 0); else wr16(ea, 0); break; }

        // ---- logic -----------------------------------------------------------
        case 0x09: { uint16_t v = imm_m(); if (m8()) { a = (a & 0xFF00) | ((a | v) & 0xFF); setZN8(A8()); } else { a |= v; setZN16(a); } break; }
        case 0x05: ora(am_dp()); break;
        case 0x15: ora(am_dpx()); break;
        case 0x0D: ora(am_abs()); break;
        case 0x1D: ora(am_absx()); break;
        case 0x19: ora(am_absy()); break;
        case 0x0F: ora(am_long()); break;
        case 0x1F: ora(am_longx()); break;
        case 0x01: ora(am_ind_dpx()); break;
        case 0x11: ora(am_ind_dpy()); break;
        case 0x12: ora(am_ind_dp()); break;
        case 0x07: ora(am_indl_dp()); break;
        case 0x17: ora(am_indl_dpy()); break;
        case 0x03: ora(am_sr()); break;
        case 0x13: ora(am_sr_y()); break;

        case 0x29: { uint16_t v = imm_m(); if (m8()) { a = (a & 0xFF00) | ((a & v) & 0xFF); setZN8(A8()); } else { a &= v; setZN16(a); } break; }
        case 0x25: anda(am_dp()); break;
        case 0x35: anda(am_dpx()); break;
        case 0x2D: anda(am_abs()); break;
        case 0x3D: anda(am_absx()); break;
        case 0x39: anda(am_absy()); break;
        case 0x2F: anda(am_long()); break;
        case 0x3F: anda(am_longx()); break;
        case 0x21: anda(am_ind_dpx()); break;
        case 0x31: anda(am_ind_dpy()); break;
        case 0x32: anda(am_ind_dp()); break;
        case 0x27: anda(am_indl_dp()); break;
        case 0x37: anda(am_indl_dpy()); break;
        case 0x23: anda(am_sr()); break;
        case 0x33: anda(am_sr_y()); break;

        case 0x49: { uint16_t v = imm_m(); if (m8()) { a = (a & 0xFF00) | ((a ^ v) & 0xFF); setZN8(A8()); } else { a ^= v; setZN16(a); } break; }
        case 0x45: eora(am_dp()); break;
        case 0x55: eora(am_dpx()); break;
        case 0x4D: eora(am_abs()); break;
        case 0x5D: eora(am_absx()); break;
        case 0x59: eora(am_absy()); break;
        case 0x4F: eora(am_long()); break;
        case 0x5F: eora(am_longx()); break;
        case 0x41: eora(am_ind_dpx()); break;
        case 0x51: eora(am_ind_dpy()); break;
        case 0x52: eora(am_ind_dp()); break;
        case 0x47: eora(am_indl_dp()); break;
        case 0x57: eora(am_indl_dpy()); break;
        case 0x43: eora(am_sr()); break;
        case 0x53: eora(am_sr_y()); break;

        // ---- arithmetic ---------------------------------------------------------
        case 0x69: adc(imm_m()); break;
        case 0x65: adc(load_m(am_dp())); break;
        case 0x75: adc(load_m(am_dpx())); break;
        case 0x6D: adc(load_m(am_abs())); break;
        case 0x7D: adc(load_m(am_absx())); break;
        case 0x79: adc(load_m(am_absy())); break;
        case 0x6F: adc(load_m(am_long())); break;
        case 0x7F: adc(load_m(am_longx())); break;
        case 0x61: adc(load_m(am_ind_dpx())); break;
        case 0x71: adc(load_m(am_ind_dpy())); break;
        case 0x72: adc(load_m(am_ind_dp())); break;
        case 0x67: adc(load_m(am_indl_dp())); break;
        case 0x77: adc(load_m(am_indl_dpy())); break;
        case 0x63: adc(load_m(am_sr())); break;
        case 0x73: adc(load_m(am_sr_y())); break;

        case 0xE9: sbc(imm_m()); break;
        case 0xE5: sbc(load_m(am_dp())); break;
        case 0xF5: sbc(load_m(am_dpx())); break;
        case 0xED: sbc(load_m(am_abs())); break;
        case 0xFD: sbc(load_m(am_absx())); break;
        case 0xF9: sbc(load_m(am_absy())); break;
        case 0xEF: sbc(load_m(am_long())); break;
        case 0xFF: sbc(load_m(am_longx())); break;
        case 0xE1: sbc(load_m(am_ind_dpx())); break;
        case 0xF1: sbc(load_m(am_ind_dpy())); break;
        case 0xF2: sbc(load_m(am_ind_dp())); break;
        case 0xE7: sbc(load_m(am_indl_dp())); break;
        case 0xF7: sbc(load_m(am_indl_dpy())); break;
        case 0xE3: sbc(load_m(am_sr())); break;
        case 0xF3: sbc(load_m(am_sr_y())); break;

        case 0xC9: cmp_gen(a, imm_m(), m8()); break;
        case 0xC5: cmp_gen(a, load_m(am_dp()), m8()); break;
        case 0xD5: cmp_gen(a, load_m(am_dpx()), m8()); break;
        case 0xCD: cmp_gen(a, load_m(am_abs()), m8()); break;
        case 0xDD: cmp_gen(a, load_m(am_absx()), m8()); break;
        case 0xD9: cmp_gen(a, load_m(am_absy()), m8()); break;
        case 0xCF: cmp_gen(a, load_m(am_long()), m8()); break;
        case 0xDF: cmp_gen(a, load_m(am_longx()), m8()); break;
        case 0xC1: cmp_gen(a, load_m(am_ind_dpx()), m8()); break;
        case 0xD1: cmp_gen(a, load_m(am_ind_dpy()), m8()); break;
        case 0xD2: cmp_gen(a, load_m(am_ind_dp()), m8()); break;
        case 0xC7: cmp_gen(a, load_m(am_indl_dp()), m8()); break;
        case 0xD7: cmp_gen(a, load_m(am_indl_dpy()), m8()); break;
        case 0xC3: cmp_gen(a, load_m(am_sr()), m8()); break;
        case 0xD3: cmp_gen(a, load_m(am_sr_y()), m8()); break;

        case 0xE0: cmp_gen(x, imm_x(), x8()); break;
        case 0xE4: cmp_gen(x, load_x(am_dp()), x8()); break;
        case 0xEC: cmp_gen(x, load_x(am_abs()), x8()); break;
        case 0xC0: cmp_gen(y, imm_x(), x8()); break;
        case 0xC4: cmp_gen(y, load_x(am_dp()), x8()); break;
        case 0xCC: cmp_gen(y, load_x(am_abs()), x8()); break;

        // ---- BIT / TSB / TRB -------------------------------------------------------
        case 0x89: {  // BIT #imm: only Z
            uint16_t v = imm_m();
            setFlag(Z, m8() ? ((A8() & v) == 0) : ((a & v) == 0));
            break;
        }
        case 0x24: bit(am_dp()); break;
        case 0x34: bit(am_dpx()); break;
        case 0x2C: bit(am_abs()); break;
        case 0x3C: bit(am_absx()); break;
        case 0x04: rmw(am_dp(), [&](uint16_t v, bool is8) -> uint16_t {  // TSB
            uint16_t am = is8 ? A8() : a;
            setFlag(Z, (am & v) == 0);
            return v | am; }); break;
        case 0x0C: rmw(am_abs(), [&](uint16_t v, bool is8) -> uint16_t {
            uint16_t am = is8 ? A8() : a;
            setFlag(Z, (am & v) == 0);
            return v | am; }); break;
        case 0x14: rmw(am_dp(), [&](uint16_t v, bool is8) -> uint16_t {  // TRB
            uint16_t am = is8 ? A8() : a;
            setFlag(Z, (am & v) == 0);
            return v & ~am; }); break;
        case 0x1C: rmw(am_abs(), [&](uint16_t v, bool is8) -> uint16_t {
            uint16_t am = is8 ? A8() : a;
            setFlag(Z, (am & v) == 0);
            return v & ~am; }); break;

        // ---- shifts / inc / dec ------------------------------------------------------
        case 0x0A: acc_rmw(op_asl); break;
        case 0x06: rmw(am_dp(), op_asl); break;
        case 0x16: rmw(am_dpx(), op_asl); break;
        case 0x0E: rmw(am_abs(), op_asl); break;
        case 0x1E: rmw(am_absx(), op_asl); break;
        case 0x4A: acc_rmw(op_lsr); break;
        case 0x46: rmw(am_dp(), op_lsr); break;
        case 0x56: rmw(am_dpx(), op_lsr); break;
        case 0x4E: rmw(am_abs(), op_lsr); break;
        case 0x5E: rmw(am_absx(), op_lsr); break;
        case 0x2A: acc_rmw(op_rol); break;
        case 0x26: rmw(am_dp(), op_rol); break;
        case 0x36: rmw(am_dpx(), op_rol); break;
        case 0x2E: rmw(am_abs(), op_rol); break;
        case 0x3E: rmw(am_absx(), op_rol); break;
        case 0x6A: acc_rmw(op_ror); break;
        case 0x66: rmw(am_dp(), op_ror); break;
        case 0x76: rmw(am_dpx(), op_ror); break;
        case 0x6E: rmw(am_abs(), op_ror); break;
        case 0x7E: rmw(am_absx(), op_ror); break;
        case 0x1A: acc_rmw(op_inc); break;
        case 0xE6: rmw(am_dp(), op_inc); break;
        case 0xF6: rmw(am_dpx(), op_inc); break;
        case 0xEE: rmw(am_abs(), op_inc); break;
        case 0xFE: rmw(am_absx(), op_inc); break;
        case 0x3A: acc_rmw(op_dec); break;
        case 0xC6: rmw(am_dp(), op_dec); break;
        case 0xD6: rmw(am_dpx(), op_dec); break;
        case 0xCE: rmw(am_abs(), op_dec); break;
        case 0xDE: rmw(am_absx(), op_dec); break;
        case 0xE8: x = x8() ? ((x + 1) & 0xFF) : static_cast<uint16_t>(x + 1); setZN(x, x8()); break;
        case 0xC8: y = x8() ? ((y + 1) & 0xFF) : static_cast<uint16_t>(y + 1); setZN(y, x8()); break;
        case 0xCA: x = x8() ? ((x - 1) & 0xFF) : static_cast<uint16_t>(x - 1); setZN(x, x8()); break;
        case 0x88: y = x8() ? ((y - 1) & 0xFF) : static_cast<uint16_t>(y - 1); setZN(y, x8()); break;

        // ---- transfers ------------------------------------------------------------------
        case 0xAA: x = x8() ? (a & 0xFF) : a; setZN(x, x8()); break;              // TAX
        case 0xA8: y = x8() ? (a & 0xFF) : a; setZN(y, x8()); break;              // TAY
        case 0x8A: if (m8()) { a = (a & 0xFF00) | (x & 0xFF); setZN8(A8()); } else { a = x; setZN16(a); } break;  // TXA
        case 0x98: if (m8()) { a = (a & 0xFF00) | (y & 0xFF); setZN8(A8()); } else { a = y; setZN16(a); } break;  // TYA
        case 0xBA: x = x8() ? (s & 0xFF) : s; setZN(x, x8()); break;              // TSX
        case 0x9A: s = e ? static_cast<uint16_t>(0x0100 | (x & 0xFF)) : x; break; // TXS
        case 0x9B: y = x8() ? (x & 0xFF) : x; setZN(y, x8()); break;              // TXY
        case 0xBB: x = x8() ? (y & 0xFF) : y; setZN(x, x8()); break;              // TYX
        case 0x5B: d = a; setZN16(d); break;                                      // TCD
        case 0x7B: a = d; setZN16(a); break;                                      // TDC
        case 0x1B: s = e ? static_cast<uint16_t>(0x0100 | (a & 0xFF)) : a; break; // TCS
        case 0x3B: a = s; setZN16(a); break;                                      // TSC
        case 0xEB: {  // XBA
            a = static_cast<uint16_t>((a >> 8) | (a << 8));
            setZN8(A8());
            break;
        }

        // ---- stack ---------------------------------------------------------------------
        case 0x48: if (m8()) push8(A8()); else push16(a); break;                  // PHA
        case 0x68: if (m8()) { a = (a & 0xFF00) | pull8(); setZN8(A8()); } else { a = pull16(); setZN16(a); } break;
        case 0xDA: if (x8()) push8(static_cast<uint8_t>(x)); else push16(x); break;
        case 0xFA: x = x8() ? pull8() : pull16(); setZN(x, x8()); break;
        case 0x5A: if (x8()) push8(static_cast<uint8_t>(y)); else push16(y); break;
        case 0x7A: y = x8() ? pull8() : pull16(); setZN(y, x8()); break;
        case 0x08: push8(e ? static_cast<uint8_t>(p | 0x30) : p); break;          // PHP
        case 0x28: p = pull8(); force_widths(); break;                            // PLP
        case 0x8B: push8(dbr); break;                                             // PHB
        case 0xAB: dbr = pull8(); setZN8(dbr); break;                             // PLB
        case 0x0B: push16(d); break;                                              // PHD
        case 0x2B: d = pull16(); setZN16(d); break;                               // PLD
        case 0x4B: push8(pbr); break;                                             // PHK
        case 0xF4: push16(fetch16()); break;                                      // PEA
        case 0xD4: { uint16_t ptr = static_cast<uint16_t>((d + fetch()) & 0xFFFF); push16(rd16(ptr)); break; }  // PEI
        case 0x62: { int16_t off = static_cast<int16_t>(fetch16()); push16(static_cast<uint16_t>(pc + off)); break; }  // PER

        // ---- jumps / calls ------------------------------------------------------------------
        case 0x4C: pc = fetch16(); break;
        case 0x5C: { uint32_t t = fetch24(); pbr = static_cast<uint8_t>(t >> 16); pc = static_cast<uint16_t>(t); break; }  // JML
        case 0x6C: { uint16_t ptr = fetch16(); pc = static_cast<uint16_t>(rd(ptr) | (rd(static_cast<uint16_t>(ptr + 1)) << 8)); break; }  // JMP (abs) — bank 0
        case 0x7C: { uint16_t ptr = static_cast<uint16_t>(fetch16() + (x8() ? (x & 0xFF) : x)); uint32_t base = static_cast<uint32_t>(pbr) << 16; pc = static_cast<uint16_t>(rd(base | ptr) | (rd(base | static_cast<uint16_t>(ptr + 1)) << 8)); break; }  // JMP (abs,X)
        case 0xDC: { uint16_t ptr = fetch16(); uint16_t lo = static_cast<uint16_t>(rd(ptr) | (rd(static_cast<uint16_t>(ptr + 1)) << 8)); pbr = rd(static_cast<uint16_t>(ptr + 2)); pc = lo; break; }  // JML [abs]
        case 0x20: { uint16_t t = fetch16(); push16(static_cast<uint16_t>(pc - 1)); pc = t; break; }  // JSR
        case 0xFC: { uint16_t ptr16 = fetch16(); push16(static_cast<uint16_t>(pc - 1)); uint16_t ptr = static_cast<uint16_t>(ptr16 + (x8() ? (x & 0xFF) : x)); uint32_t base = static_cast<uint32_t>(pbr) << 16; pc = static_cast<uint16_t>(rd(base | ptr) | (rd(base | static_cast<uint16_t>(ptr + 1)) << 8)); break; }  // JSR (abs,X)
        case 0x22: { uint32_t t = fetch24(); push8(pbr); push16(static_cast<uint16_t>(pc - 1)); pbr = static_cast<uint8_t>(t >> 16); pc = static_cast<uint16_t>(t); break; }  // JSL
        case 0x60: pc = static_cast<uint16_t>(pull16() + 1); break;               // RTS
        case 0x6B: { pc = static_cast<uint16_t>(pull16() + 1); pbr = pull8(); break; }  // RTL
        case 0x40: {  // RTI
            p = pull8();
            force_widths();
            pc = pull16();
            if (!e) pbr = pull8();
            break;
        }

        // ---- branches ---------------------------------------------------------------------------
        case 0x90: branch(!(p & C)); break;
        case 0xB0: branch(p & C); break;
        case 0xD0: branch(!(p & Z)); break;
        case 0xF0: branch(p & Z); break;
        case 0x10: branch(!(p & N)); break;
        case 0x30: branch(p & N); break;
        case 0x50: branch(!(p & V)); break;
        case 0x70: branch(p & V); break;
        case 0x80: branch(true); break;                                            // BRA
        case 0x82: { int16_t off = static_cast<int16_t>(fetch16()); pc = static_cast<uint16_t>(pc + off); break; }  // BRL

        // ---- flags / modes ----------------------------------------------------------------------
        case 0x18: setFlag(C, false); break;
        case 0x38: setFlag(C, true); break;
        case 0x58: setFlag(I, false); break;
        case 0x78: setFlag(I, true); break;
        case 0xD8: setFlag(D, false); break;
        case 0xF8: setFlag(D, true); break;
        case 0xB8: setFlag(V, false); break;
        case 0xC2: p &= static_cast<uint8_t>(~fetch()); force_widths(); break;     // REP
        case 0xE2: p |= fetch(); force_widths(); break;                            // SEP
        case 0xFB: {  // XCE
            bool old_c = p & C;
            setFlag(C, e);
            e = old_c;
            if (e) {
                s = static_cast<uint16_t>(0x0100 | (s & 0xFF));
            }
            force_widths();
            break;
        }

        // ---- block moves ---------------------------------------------------------------------------
        case 0x54: case 0x44: {  // MVN / MVP
            const uint8_t dst_bank = fetch(), src_bank = fetch();
            dbr = dst_bank;
            const bool up = (op == 0x54);
            // A holds count-1 (always 16-bit for the count).
            do {
                uint8_t v = rd((static_cast<uint32_t>(src_bank) << 16) | x);
                wr((static_cast<uint32_t>(dst_bank) << 16) | y, v);
                if (up) { ++x; ++y; } else { --x; --y; }
                if (x8()) { x &= 0xFF; y &= 0xFF; }
                --a;
                cyc += 5;
            } while (a != 0xFFFF);
            break;
        }

        // ---- interrupts / misc ----------------------------------------------------------------------
        case 0x00: ++pc; interrupt(0xFFE6, 0xFFFE); break;                          // BRK
        case 0x02: ++pc; interrupt(0xFFE4, 0xFFF4); break;                          // COP
        case 0xCB: waiting = true; break;                                           // WAI
        case 0xDB: stopped = true; break;                                           // STP
        case 0x42: fetch(); break;                                                  // WDM
        case 0xEA: break;                                                           // NOP

        default:
            // All 256 opcodes are defined on the 65816; reaching here means a
            // decode gap — treat as NOP but this should not happen.
            break;
    }
}

}  // namespace famemu::snes
