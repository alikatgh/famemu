// ember32/cpu_arm7.hpp — Ember 32 CPU: an ARM7TDMI-class ARM-state interpreter,
// REFERENCE MODEL (correctness over speed).
//
// Covers the ARM instruction classes a bring-up program needs and then some:
// data-processing (all 16 opcodes, immediate / register / register-shifted
// operands, the S-bit flag update and shifter carry), MUL/MLA, single load/store
// (imm & register offset, pre/post index, writeback, byte/word), load/store
// multiple (IA/IB/DA/DB + writeback), branch/branch-link, and MRS/MSR. Thumb,
// banked FIQ/IRQ registers and exceptions are deferred (see README) — this is
// the "does an ARM program drive the compositor?" core, not the full silicon.
//
// Memory goes through a Bus (bus.hpp): flat RAM + an MMIO window onto the GPU.
#pragma once
#include <cstdint>
#include "bus.hpp"

namespace ember32 {

struct CPU {
    uint32_t r[16] = {};      // r15 = PC (points at the instruction being fetched + 8, ARM)
    uint32_t cpsr = 0x1F;     // System mode; flags in the top bits
    Bus* bus = nullptr;
    bool halted = false;
    uint64_t cycles = 0;

    // ---- flags (NZCV in bits 31..28) ----
    bool N() const { return cpsr >> 31 & 1; }  bool Z() const { return cpsr >> 30 & 1; }
    bool C() const { return cpsr >> 29 & 1; }  bool V() const { return cpsr >> 28 & 1; }
    void setN(bool v){ cpsr = (cpsr & ~(1u<<31)) | (uint32_t(v)<<31); }
    void setZ(bool v){ cpsr = (cpsr & ~(1u<<30)) | (uint32_t(v)<<30); }
    void setC(bool v){ cpsr = (cpsr & ~(1u<<29)) | (uint32_t(v)<<29); }
    void setV(bool v){ cpsr = (cpsr & ~(1u<<28)) | (uint32_t(v)<<28); }
    void setNZ(uint32_t x){ setN(x>>31&1); setZ(x==0); }

    void reset(uint32_t entry){ for(auto&x:r)x=0; r[15]=entry; cpsr=0x1F; halted=false; cycles=0; }

    bool cond(uint32_t c) const {
        switch(c){
            case 0x0: return Z();          case 0x1: return !Z();
            case 0x2: return C();          case 0x3: return !C();
            case 0x4: return N();          case 0x5: return !N();
            case 0x6: return V();          case 0x7: return !V();
            case 0x8: return C()&&!Z();     case 0x9: return !C()||Z();
            case 0xA: return N()==V();      case 0xB: return N()!=V();
            case 0xC: return !Z()&&(N()==V()); case 0xD: return Z()||(N()!=V());
            default:  return true;         // 0xE always (0xF is unpredictable → treat as never)
        }
    }

    // barrel shifter for operand 2 (register form); updates `carry` (shifter carry-out)
    uint32_t shift(uint32_t val, uint32_t type, uint32_t amt, bool& carry) const {
        if (type==0) { // LSL
            if(amt==0){ carry=C(); return val; }
            if(amt<32){ carry=(val>>(32-amt))&1; return val<<amt; }
            if(amt==32){ carry=val&1; return 0; } carry=false; return 0;
        } else if (type==1) { // LSR
            if(amt==0||amt==32){ carry=(val>>31)&1; return 0; }
            if(amt<32){ carry=(val>>(amt-1))&1; return val>>amt; } carry=false; return 0;
        } else if (type==2) { // ASR
            if(amt==0||amt>=32){ carry=(val>>31)&1; return (val>>31)?0xFFFFFFFF:0; }
            carry=(val>>(amt-1))&1; return uint32_t(int32_t(val)>>amt);
        } else { // ROR
            if(amt==0){ bool c=C(); carry=val&1; return (val>>1)|(uint32_t(c)<<31); } // RRX
            amt&=31; if(amt==0){ carry=(val>>31)&1; return val; }
            carry=(val>>(amt-1))&1; return (val>>amt)|(val<<(32-amt));
        }
    }

    // decode operand 2 for data-processing (I = immediate flag)
    uint32_t op2(uint32_t insn, bool I, bool& carry) const {
        if (I) { uint32_t imm=insn&0xFF, rot=((insn>>8)&0xF)*2;
                 if(rot==0){ carry=C(); return imm; }
                 uint32_t v=(imm>>rot)|(imm<<(32-rot)); carry=(v>>31)&1; return v; }
        uint32_t rm = r[insn&0xF]; uint32_t type=(insn>>5)&3;
        uint32_t amt; if(insn&0x10){ amt=r[(insn>>8)&0xF]&0xFF; if((insn&0xF)==15) rm+=4; }
                      else amt=(insn>>7)&0x1F;
        return shift(rm, type, amt, carry);
    }

    void step() {
        uint32_t pc = r[15];
        uint32_t insn = bus->r32(pc);
        r[15] = pc + 8;                      // ARM PC reads as insn+8
        cycles++;
        if (!cond(insn>>28)) { r[15] = pc + 4; return; }

        if ((insn&0x0FFFFFF0)==0x012FFF10) {       // BX — used here as HALT if to r with bit0 quirk
            r[15] = r[insn&0xF] & ~1u; return;
        }
        if ((insn&0x0F000000)==0x0A000000 || (insn&0x0F000000)==0x0B000000) { // B / BL
            int32_t off = int32_t(insn<<8)>>6;      // sign-extend 24-bit, <<2
            if(insn&0x01000000) r[14]=pc+4;         // BL: link
            r[15] = pc + 8 + off; return;
        }
        if ((insn&0x0FC000F0)==0x00000090) {        // MUL / MLA
            uint32_t rd=(insn>>16)&0xF, rn=(insn>>12)&0xF, rs=(insn>>8)&0xF, rm=insn&0xF;
            uint32_t res=r[rm]*r[rs]; if(insn&0x00200000) res+=r[rn];
            r[rd]=res; if(insn&0x00100000) setNZ(res); r[15]=pc+4; return;
        }
        if ((insn&0x0FBF0FFF)==0x010F0000) {        // MRS
            r[(insn>>12)&0xF]=cpsr; r[15]=pc+4; return;
        }
        if ((insn&0x0DB0F000)==0x0120F000) {        // MSR (reg or imm, cpsr)
            bool I=(insn>>25)&1; bool c; uint32_t v = I? op2(insn,true,c) : r[insn&0xF];
            uint32_t mask=0; if(insn&0x00080000)mask|=0xFF000000; if(insn&0x00010000)mask|=0x000000FF;
            cpsr=(cpsr&~mask)|(v&mask); r[15]=pc+4; return;
        }
        uint32_t top=(insn>>26)&3;
        if (top==0) {                                // data processing
            uint32_t opc=(insn>>21)&0xF, rd=(insn>>12)&0xF;
            data_proc(insn);
            bool writes_pc = rd==15 && !(opc>=0x8 && opc<=0xB);   // not TST/TEQ/CMP/CMN
            if(!writes_pc) r[15]=pc+4;               // else the op branched (PC stands)
            return;
        }
        if (top==1) { load_store(insn); if(((insn>>12)&0xF)!=15) r[15]=pc+4; return; }
        if (top==2 && ((insn>>25)&1)==0) { block_transfer(insn); if(!(insn&(1<<15)&&(insn&(1<<20)))) r[15]=pc+4; return; }
        r[15]=pc+4;                                  // unimplemented → skip (reference is permissive)
    }

    void data_proc(uint32_t insn) {
        bool I=(insn>>25)&1, S=(insn>>20)&1;
        uint32_t opc=(insn>>21)&0xF, rn=(insn>>16)&0xF, rd=(insn>>12)&0xF;
        bool sc; uint32_t a=r[rn], b=op2(insn,I,sc), res=0; bool wb=true; bool cf=sc, vf=V();
        auto addf=[&](uint64_t x,uint64_t y,uint64_t cin){ uint64_t s=x+y+cin; cf=s>>32&1;
                    vf=(~(x^y)&(x^s)&0x80000000)!=0; return uint32_t(s); };
        auto subf=[&](uint32_t x,uint32_t y,uint32_t cin){ uint64_t s=uint64_t(x)+(~y&0xFFFFFFFF)+cin;
                    cf=s>>32&1; vf=((x^y)&(x^uint32_t(s))&0x80000000)!=0; return uint32_t(s); };
        switch(opc){
            case 0x0: res=a&b; break;               case 0x1: res=a^b; break;
            case 0x2: res=subf(a,b,1); break;        case 0x3: res=subf(b,a,1); break;
            case 0x4: res=addf(a,b,0); break;        case 0x5: res=addf(a,b,C()); break;
            case 0x6: res=subf(a,b,C()); break;      case 0x7: res=subf(b,a,C()); break;
            case 0x8: res=a&b; wb=false; break;      case 0x9: res=a^b; wb=false; break;
            case 0xA: res=subf(a,b,1); wb=false; break; case 0xB: res=addf(a,b,0); wb=false; break;
            case 0xC: res=a|b; break;                case 0xD: res=b; break;
            case 0xE: res=a&~b; break;               case 0xF: res=~b; break;
        }
        if(wb) r[rd]=res;
        if(S){ setNZ(res); if(opc>=0x2&&opc<=0x7||opc==0xA||opc==0xB){ setC(cf); setV(vf); } else setC(cf); }
    }

    void load_store(uint32_t insn) {
        bool I=(insn>>25)&1, P=(insn>>24)&1, U=(insn>>23)&1, B=(insn>>22)&1, W=(insn>>21)&1, L=(insn>>20)&1;
        uint32_t rn=(insn>>16)&0xF, rd=(insn>>12)&0xF;
        uint32_t off; if(I){ bool c; off=shift(r[insn&0xF],(insn>>5)&3,(insn>>7)&0x1F,c); } else off=insn&0xFFF;
        uint32_t base=r[rn]; uint32_t addr = P ? (U?base+off:base-off) : base;
        if(L){ uint32_t v = B? bus->r8(addr) : bus->r32(addr); r[rd]=v; }
        else { uint32_t v=r[rd]; if(rd==15) v+=8; if(B) bus->w8(addr,v&0xFF); else bus->w32(addr,v); }
        if(!P){ addr = U?base+off:base-off; r[rn]=addr; }
        else if(W) r[rn]=addr;
        if(L && rd==15) r[15]&=~3u;
    }

    void block_transfer(uint32_t insn) {
        bool P=(insn>>24)&1, U=(insn>>23)&1, W=(insn>>21)&1, L=(insn>>20)&1;
        uint32_t rn=(insn>>16)&0xF, base=r[rn]; uint16_t list=insn&0xFFFF;
        int n=0; for(int i=0;i<16;i++) if(list>>i&1) n++;
        uint32_t addr = U? base : base - n*4; uint32_t final = U? base+n*4 : base-n*4;
        if(U){ if(P) addr+=4; } else { if(!P) addr+=4; }
        for(int i=0;i<16;i++) if(list>>i&1){
            if(L) r[i]=bus->r32(addr); else bus->w32(addr, r[i] + (i==15?8:0));
            addr+=4;
        }
        if(W) r[rn]=final;
        if(L && (list&(1<<15))) r[15]&=~3u;
    }

    void run(uint64_t max_steps) {
        for(uint64_t i=0;i<max_steps && !halted;i++){
            if(r[15]==0xFFFFFFFF){ halted=true; break; }   // convention: jump to -1 halts
            step();
        }
    }
};

} // namespace ember32
