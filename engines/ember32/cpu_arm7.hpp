// ember32/cpu_arm7.hpp — Ember 32 CPU: an ARM7TDMI-class ARM + Thumb interpreter,
// REFERENCE MODEL (correctness over speed).
//
// Covers the ARM instruction classes: data-processing (all 16 opcodes, immediate
// / register / register-shifted operands, the S-bit flag update and shifter
// carry), MUL/MLA, single load/store (imm & register offset, pre/post index,
// writeback, byte/word), load/store multiple (IA/IB/DA/DB + writeback + the `^`
// exception-return form), branch/branch-link, MRS/MSR (CPSR *and* SPSR), SWI,
// and the full Thumb ISA with ARM<->Thumb interworking.
//
// The privileged machine is modelled: the seven processor modes with their
// BANKED r13/r14 (and r8-r12 for FIQ) and per-mode SPSR, plus the exception
// entry/exit sequence for Reset / Undefined / SWI / Prefetch-Abort / Data-Abort
// / IRQ / FIQ (vectors at 0x00..0x1C). IRQ/FIQ lines come from the bus interrupt
// controller (bus.hpp). A separate S/N/I bus-cycle counter (`tcycles`) models
// exact ARM7TDMI timing alongside the retired-instruction count (`cycles`).
//
// Return-from-exception is the architectural `MOVS/SUBS PC, LR, #off` (data-proc
// to PC with S) and `LDM {..,pc}^`, both of which restore CPSR from the current
// mode's SPSR. Interrupt handlers return with the customary `SUBS PC, LR, #4`.
//
// Memory goes through a Bus (bus.hpp): flat RAM + an MMIO window onto the GPU.
#pragma once
#include <cstdint>
#include "bus.hpp"

namespace ember32 {

// Processor modes (CPSR[4:0]).
enum : uint32_t { MODE_USR=0x10, MODE_FIQ=0x11, MODE_IRQ=0x12, MODE_SVC=0x13,
                  MODE_ABT=0x17, MODE_UND=0x1B, MODE_SYS=0x1F };
// Exception vector addresses.
enum : uint32_t { VEC_RESET=0x00, VEC_UNDEF=0x04, VEC_SWI=0x08, VEC_PABT=0x0C,
                  VEC_DABT=0x10, VEC_IRQ=0x18, VEC_FIQ=0x1C };

struct CPU {
    uint32_t r[16] = {};      // r15 = PC (points at the instruction being fetched + 8, ARM)
    uint32_t cpsr = 0x1F;     // System mode; NZCVQ flags high, I/F/T + mode bits low
    Bus* bus = nullptr;
    bool halted = false;
    uint64_t cycles = 0;      // retired instructions
    uint64_t tcycles = 0;     // bus cycles (S+N+I) — exact ARM7TDMI timing

    // Banked state. Bank index: 0=usr/sys, 1=fiq, 2=irq, 3=svc, 4=abt, 5=und.
    // The ACTIVE mode's r13/r14 live in r[]; the banks hold the inactive copies.
    uint32_t bank13[6] = {}, bank14[6] = {}, bankspsr[6] = {};
    uint32_t usr_r8_12[5] = {};   // r8..r12 shared by every non-FIQ mode
    uint32_t fiq_r8_12[5] = {};   // r8..r12 private to FIQ
    bool undef_trap = false;      // opt-in: trap unimplemented encodings to VEC_UNDEF
                                  // (default keeps the permissive skip the carts rely on)

    // ---- flags (NZCV in bits 31..28) ----
    bool N() const { return cpsr >> 31 & 1; }  bool Z() const { return cpsr >> 30 & 1; }
    bool C() const { return cpsr >> 29 & 1; }  bool V() const { return cpsr >> 28 & 1; }
    void setN(bool v){ cpsr = (cpsr & ~(1u<<31)) | (uint32_t(v)<<31); }
    void setZ(bool v){ cpsr = (cpsr & ~(1u<<30)) | (uint32_t(v)<<30); }
    void setC(bool v){ cpsr = (cpsr & ~(1u<<29)) | (uint32_t(v)<<29); }
    void setV(bool v){ cpsr = (cpsr & ~(1u<<28)) | (uint32_t(v)<<28); }
    void setNZ(uint32_t x){ setN(x>>31&1); setZ(x==0); }

    void reset(uint32_t entry){
        for(auto&x:r)x=0; r[15]=entry; cpsr=0x1F; halted=false; cycles=0; tcycles=0;
        for(int i=0;i<6;i++){ bank13[i]=bank14[i]=bankspsr[i]=0; }
        for(int i=0;i<5;i++){ usr_r8_12[i]=fiq_r8_12[i]=0; }
        // Bring-up convention: boot in System mode with interrupts enabled but no
        // source armed (bus IRQ mask = 0), so existing carts behave exactly as
        // before; a cart arms interrupts explicitly to receive them.
    }

    // ---- modes, banking, SPSR --------------------------------------------------
    static int bankIdx(uint32_t m){
        switch(m & 0x1F){
            case MODE_FIQ: return 1;  case MODE_IRQ: return 2;  case MODE_SVC: return 3;
            case MODE_ABT: return 4;  case MODE_UND: return 5;  default: return 0; // usr/sys
        }
    }
    int  modeIdx() const { return bankIdx(cpsr); }
    uint32_t& spsr(){ return bankspsr[modeIdx()]; }   // usr/sys: index 0 acts as scratch

    // Switch processor mode, swapping the banked r13/r14 (and r8-r12 for FIQ).
    void set_mode(uint32_t nm){
        int oi = bankIdx(cpsr), ni = bankIdx(nm);
        if(oi != ni){
            bank13[oi] = r[13]; bank14[oi] = r[14];
            if(oi==1) for(int i=0;i<5;i++) fiq_r8_12[i]=r[8+i];
            else      for(int i=0;i<5;i++) usr_r8_12[i]=r[8+i];
            r[13] = bank13[ni]; r[14] = bank14[ni];
            if(ni==1) for(int i=0;i<5;i++) r[8+i]=fiq_r8_12[i];
            else      for(int i=0;i<5;i++) r[8+i]=usr_r8_12[i];
        }
        cpsr = (cpsr & ~0x1Fu) | (nm & 0x1F);
    }

    // Enter an exception: bank in `newmode`, save the old CPSR to its SPSR, set the
    // return link, force ARM state, mask IRQ (and FIQ on reset/FIQ), vector the PC.
    void exception(uint32_t vector, uint32_t newmode, uint32_t lr, bool disableF){
        uint32_t saved = cpsr;
        set_mode(newmode);
        spsr() = saved;
        r[14] = lr;
        cpsr &= ~0x20u;            // execute the handler in ARM state
        cpsr |= 0x80u;             // mask IRQ
        if(disableF) cpsr |= 0x40u;// mask FIQ
        r[15] = vector;
        tcycles += 3;              // 2S + 1N pipeline refill
    }

    // Take a pending FIQ/IRQ if unmasked and the bus is asserting the line. Checked
    // before every fetch. Handlers return with SUBS PC, LR, #4.
    bool service_interrupts(){
        if(!bus) return false;
        if(!(cpsr & 0x40u) && bus->fiq_line()){ exception(VEC_FIQ, MODE_FIQ, r[15] + 4, true);  return true; }
        if(!(cpsr & 0x80u) && bus->irq_line()){ exception(VEC_IRQ, MODE_IRQ, r[15] + 4, false); return true; }
        return false;
    }

    // Restore CPSR from the current mode's SPSR (exception return) and realign PC.
    void return_from_exception(){
        uint32_t sp = spsr();
        set_mode(sp & 0x1F);
        cpsr = sp;
        if(cpsr & 0x20u) r[15] &= ~1u; else r[15] &= ~3u;
    }

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

    // ARM7 MUL internal cycles (m): 1..4 by the significant bytes of Rs.
    static int mul_m(uint32_t rs){
        if((rs&0xFFFFFF00)==0 || (rs&0xFFFFFF00)==0xFFFFFF00) return 1;
        if((rs&0xFFFF0000)==0 || (rs&0xFFFF0000)==0xFFFF0000) return 2;
        if((rs&0xFF000000)==0 || (rs&0xFF000000)==0xFF000000) return 3;
        return 4;
    }

    void step() {
        if (service_interrupts()) return;           // FIQ/IRQ taken before the fetch
        if (cpsr & 0x20) { thumb_step(); return; }  // CPSR.T set → Thumb state
        uint32_t pc = r[15];
        uint32_t insn = bus->r32(pc);
        r[15] = pc + 8;                      // ARM PC reads as insn+8
        cycles++;
        if (!cond(insn>>28)) { r[15] = pc + 4; tcycles += 1; return; }   // 1S

        if ((insn&0x0FFFFFF0)==0x012FFF10) {       // BX — switches ARM/Thumb via bit0
            uint32_t t = r[insn&0xF];
            if (t & 1) cpsr |= 0x20; else cpsr &= ~0x20u;
            r[15] = t & ~1u; tcycles += 3; return;              // 2S+1N
        }
        if ((insn&0x0F000000)==0x0A000000 || (insn&0x0F000000)==0x0B000000) { // B / BL
            int32_t off = int32_t(insn<<8)>>6;      // sign-extend 24-bit, <<2
            if(insn&0x01000000) r[14]=pc+4;         // BL: link
            r[15] = pc + 8 + off; tcycles += 3; return;         // 2S+1N
        }
        if ((insn&0x0F000000)==0x0F000000) {        // SWI / SVC
            exception(VEC_SWI, MODE_SVC, pc + 4, false); return;  // LR = next instruction
        }
        if ((insn&0x0FC000F0)==0x00000090) {        // MUL / MLA
            uint32_t rd=(insn>>16)&0xF, rn=(insn>>12)&0xF, rs=(insn>>8)&0xF, rm=insn&0xF;
            uint32_t res=r[rm]*r[rs]; if(insn&0x00200000) res+=r[rn];
            r[rd]=res; if(insn&0x00100000) setNZ(res); r[15]=pc+4;
            tcycles += 1 + mul_m(r[rs]) + ((insn&0x00200000)?1:0); return;
        }
        if ((insn&0x0FBF0FFF)==0x010F0000) {        // MRS (CPSR or SPSR)
            r[(insn>>12)&0xF] = (insn&0x00400000) ? spsr() : cpsr;
            r[15]=pc+4; tcycles += 1; return;
        }
        if ((insn&0x0DB0F000)==0x0120F000) {        // MSR (reg or imm; CPSR or SPSR)
            bool I=(insn>>25)&1; bool c; uint32_t v = I? op2(insn,true,c) : r[insn&0xF];
            uint32_t mask=0; if(insn&0x00080000)mask|=0xFF000000; if(insn&0x00010000)mask|=0x000000FF;
            if(insn&0x00400000){ spsr() = (spsr() & ~mask) | (v & mask); }   // SPSR
            else {                                                           // CPSR
                uint32_t nc = (cpsr & ~mask) | (v & mask);
                set_mode(nc & 0x1F);                    // bank-swap if the mode byte changed
                cpsr = (cpsr & 0x1Fu) | (nc & ~0x1Fu);  // keep the new mode bits, apply the rest
            }
            r[15]=pc+4; tcycles += 1; return;
        }
        uint32_t top=(insn>>26)&3;
        if (top==0) {                                // data processing
            uint32_t opc=(insn>>21)&0xF, rd=(insn>>12)&0xF; bool S=(insn>>20)&1;
            bool reg_shift = !(insn&0x02000000) && (insn&0x10);   // register-specified shift
            data_proc(insn);
            bool writes_pc = rd==15 && !(opc>=0x8 && opc<=0xB);   // not TST/TEQ/CMP/CMN
            if(writes_pc){
                if(S) return_from_exception();       // MOVS/SUBS PC,LR → restore CPSR from SPSR
                tcycles += 3 + (reg_shift?1:0);      // 2S+1N pipeline refill; PC stands otherwise
            } else {
                r[15]=pc+4; tcycles += 1 + (reg_shift?1:0);      // 1S (+1I reg-shift)
            }
            return;
        }
        if (top==1) {                                // single load/store
            bool pc_ld = ((insn>>12)&0xF)==15 && (insn&0x00100000);
            load_store(insn);
            if(!pc_ld) r[15]=pc+4;
            tcycles += (insn&0x00100000) ? (3 + (pc_ld?2:0)) : 2;   // LDR 1S+1N+1I(+refill); STR 2N
            return;
        }
        if (top==2 && ((insn>>25)&1)==0) {           // block transfer (LDM/STM)
            uint32_t list=insn&0xFFFF; int n=0; for(int i=0;i<16;i++) if(list>>i&1) n++;
            bool pc_ld = (list&0x8000) && (insn&0x00100000);
            bool s_ret = pc_ld && (insn&0x00400000);   // LDM {..,pc}^ : restore CPSR from SPSR
            block_transfer(insn);
            if(s_ret) return_from_exception();
            else if(!pc_ld) r[15]=pc+4;
            if(insn&0x00100000) tcycles += n + 2 + (pc_ld?2:0);     // LDM nS+1N+1I (+refill)
            else                tcycles += (n>0?n-1:0) + 2;         // STM (n-1)S+2N
            return;
        }
        if (undef_trap) { exception(VEC_UNDEF, MODE_UND, pc + 4, false); return; }
        r[15]=pc+4; tcycles += 1;                    // unimplemented → skip (reference is permissive)
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
        // rd==15 with S means "restore CPSR from SPSR" (handled in step) — not a flag update.
        if(S && rd!=15){ setNZ(res); if((opc>=0x2&&opc<=0x7)||opc==0xA||opc==0xB){ setC(cf); setV(vf); } else setC(cf); }
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

    // ---- Thumb (16-bit) state ----
    void thumb_step() {
        uint32_t pc = r[15];
        uint16_t op = bus->r16(pc);
        r[15] = pc + 2;                     // will be overridden by branches
        uint32_t rpc = pc + 4;              // Thumb PC reads +4
        cycles++; tcycles += 1;             // ~1S/instruction (branches add refill below)
        uint32_t h = op >> 13;

        if (h == 0) {
            if ((op >> 11 & 3) == 3) {                      // fmt 2: ADD/SUB reg/imm3
                uint32_t rd=op&7, rn=op>>3&7, arg=op>>6&7;
                uint32_t a=r[rn], b=(op&0x400)?arg:r[arg], res;
                if (op&0x200){ res=a-b; setNZ(res); setC(a>=b); setV(((a^b)&(a^res)&0x80000000)!=0); }
                else         { res=a+b; setNZ(res); setC(((uint64_t)a+b)>>32); setV((~(a^b)&(a^res)&0x80000000)!=0); }
                r[rd]=res; return;
            }
            uint32_t rd=op&7, rm=op>>3&7, amt=op>>6&0x1F, type=op>>11&3;  // fmt 1: shift by imm
            bool cf; uint32_t res=shift(r[rm], type, (type!=0 && amt==0)?32:amt, cf);
            r[rd]=res; setNZ(res); setC(cf); return;
        }
        if (h == 1) {                                        // fmt 3: MOV/CMP/ADD/SUB imm8
            uint32_t rd=op>>8&7, imm=op&0xFF, o=op>>11&3, a=r[rd], res;
            if (o==0){ r[rd]=imm; setNZ(imm); }
            else if (o==2){ res=a+imm; r[rd]=res; setNZ(res); setC(((uint64_t)a+imm)>>32); setV((~(a^imm)&(a^res)&0x80000000)!=0); }
            else { res=a-imm; if(o==3) r[rd]=res; setNZ(res); setC(a>=imm); setV(((a^imm)&(a^res)&0x80000000)!=0); }
            return;
        }
        if (h == 2) {
            if ((op>>10&0x3F)==0x10) {                       // fmt 4: ALU
                uint32_t rd=op&7, rm=op>>3&7, o=op>>6&0xF, a=r[rd], b=r[rm], res=a; bool cf=C();
                switch(o){
                    case 0x0: res=a&b; setNZ(res); r[rd]=res; return;
                    case 0x1: res=a^b; setNZ(res); r[rd]=res; return;
                    case 0x2: res=shift(a,0,b&0xFF,cf); setNZ(res); setC(cf); r[rd]=res; return;
                    case 0x3: res=shift(a,1,b&0xFF,cf); setNZ(res); setC(cf); r[rd]=res; return;
                    case 0x4: res=shift(a,2,b&0xFF,cf); setNZ(res); setC(cf); r[rd]=res; return;
                    case 0x5: { uint64_t s=(uint64_t)a+b+C(); res=s; setNZ(res); setC(s>>32); setV((~(a^b)&(a^res)&0x80000000)!=0); r[rd]=res; return; }
                    case 0x6: { uint64_t s=(uint64_t)a+(~b&0xFFFFFFFFu)+C(); res=s; setNZ(res); setC(s>>32); setV(((a^b)&(a^res)&0x80000000)!=0); r[rd]=res; return; }
                    case 0x7: res=shift(a,3,b&0xFF,cf); setNZ(res); setC(cf); r[rd]=res; return;
                    case 0x8: setNZ(a&b); return;                                   // TST
                    case 0x9: res=0-b; setNZ(res); setC(b==0); r[rd]=res; return;   // NEG
                    case 0xA: res=a-b; setNZ(res); setC(a>=b); setV(((a^b)&(a^res)&0x80000000)!=0); return; // CMP
                    case 0xB: { uint64_t s=(uint64_t)a+b; setNZ((uint32_t)s); setC(s>>32); return; }        // CMN
                    case 0xC: res=a|b; setNZ(res); r[rd]=res; return;
                    case 0xD: res=a*b; setNZ(res); r[rd]=res; return;
                    case 0xE: res=a&~b; setNZ(res); r[rd]=res; return;
                    case 0xF: res=~b; setNZ(res); r[rd]=res; return;
                }
            }
            if ((op>>10&0x3F)==0x11) {                       // fmt 5: hi-reg ops + BX
                uint32_t o=op>>8&3, rd=(op&7)|(op>>4&8), rm=op>>3&0xF, vm=r[rm];
                if (rm==15) vm=rpc;
                if (o==3){ if(vm&1) cpsr|=0x20; else cpsr&=~0x20u; r[15]=vm&~1u; return; }
                if (o==0){ r[rd]=(rd==15?rpc:r[rd])+vm; if(rd==15) r[15]&=~1u; }
                else if (o==1){ uint32_t a=(rd==15?rpc:r[rd]),res=a-vm; setNZ(res); setC(a>=vm); setV(((a^vm)&(a^res)&0x80000000)!=0); }
                else { r[rd]=vm; if(rd==15) r[15]&=~1u; }
                return;
            }
            if ((op>>11&0x1F)==9) {                          // fmt 6: LDR Rd,[PC,#imm]
                r[op>>8&7]=bus->r32((rpc&~3u)+(op&0xFF)*4); return;
            }
            uint32_t rd=op&7, rn=op>>3&7, rm=op>>6&7, addr=r[rn]+r[rm];   // fmt 7/8: reg offset
            if (op&0x200){ uint32_t o=op>>10&3;                          // SH ops
                if(o==0) bus->w16(addr, r[rd]);
                else if(o==1) r[rd]=uint32_t(int32_t(int8_t(bus->r8(addr))));
                else if(o==2) r[rd]=bus->r16(addr);
                else r[rd]=uint32_t(int32_t(int16_t(bus->r16(addr))));
            } else { uint32_t o=op>>10&3;
                if(o==0) bus->w32(addr,r[rd]); else if(o==1) bus->w8(addr,r[rd]);
                else if(o==2) r[rd]=bus->r32(addr); else r[rd]=bus->r8(addr);
            }
            return;
        }
        if (h == 3) {                                        // fmt 9: load/store imm offset
            uint32_t rd=op&7, rn=op>>3&7, off=op>>6&0x1F, addr;
            bool B=op&0x1000, L=op&0x800;
            addr = r[rn] + (B?off:off*4);
            if(L) r[rd]=B?bus->r8(addr):bus->r32(addr);
            else { if(B) bus->w8(addr,r[rd]); else bus->w32(addr,r[rd]); }
            return;
        }
        if (h == 4) {
            if(!(op&0x1000)){ uint32_t rd=op&7,rn=op>>3&7,addr=r[rn]+(op>>6&0x1F)*2;  // fmt 10: halfword
                if(op&0x800) r[rd]=bus->r16(addr); else bus->w16(addr,r[rd]); return; }
            uint32_t rd=op>>8&7, addr=r[13]+(op&0xFF)*4;      // fmt 11: SP-relative
            if(op&0x800) r[rd]=bus->r32(addr); else bus->w32(addr,r[rd]); return;
        }
        if (h == 5) {
            if(!(op&0x1000)){ r[op>>8&7]=(op&0x800)? r[13]+(op&0xFF)*4 : (rpc&~3u)+(op&0xFF)*4; return; } // fmt 12
            if((op&0xFF00)==0xB000){ uint32_t o=(op&0x7F)*4; r[13]+=(op&0x80)? -int(o):int(o); return; }  // fmt 13
            if((op&0xF600)==0xB400){                          // fmt 14: PUSH/POP
                bool L=op&0x800, R=op&0x100; uint8_t list=op&0xFF;
                if(!L){ int n=__builtin_popcount(list)+(R?1:0); uint32_t a=r[13]-n*4; r[13]=a;
                        for(int i=0;i<8;i++) if(list>>i&1){ bus->w32(a,r[i]); a+=4; } if(R) bus->w32(a,r[14]); }
                else { uint32_t a=r[13]; for(int i=0;i<8;i++) if(list>>i&1){ r[i]=bus->r32(a); a+=4; }
                       if(R){ r[15]=bus->r32(a)&~1u; a+=4; } r[13]=a; }
                return;
            }
            return;
        }
        if (h == 6) {
            if(!(op&0x1000)){ uint32_t rn=op>>8&7,a=r[rn],list=op&0xFF;                // fmt 15: LDMIA/STMIA
                for(int i=0;i<8;i++) if(list>>i&1){ if(op&0x800) r[i]=bus->r32(a); else bus->w32(a,r[i]); a+=4; }
                r[rn]=a; return; }
            uint32_t c=op>>8&0xF;                             // fmt 16: conditional branch / SWI
            if(c==0xF){ exception(VEC_SWI, MODE_SVC, pc + 2, false); return; }  // Thumb SWI → SVC
            if(cond(c)){ r[15]=rpc + int32_t(int8_t(op&0xFF))*2; tcycles += 2; }
            return;
        }
        // h == 7
        if((op>>11&0x1F)==0x1C){ r[15]=rpc + ((int32_t(op<<21)>>20)); return; }        // fmt 18: B
        if((op>>11&0x1F)==0x1E){ r[14]=rpc + (int32_t(op<<21)>>9); return; }            // fmt 19: BL hi
        if((op>>11&0x1F)==0x1F){ uint32_t ret=(pc+2)|1; r[15]=(r[14]+((op&0x7FF)<<1))&~1u; r[14]=ret; return; } // BL lo
    }

    void run(uint64_t max_steps) {
        for(uint64_t i=0;i<max_steps && !halted;i++){
            if(r[15]==0xFFFFFFFF){ halted=true; break; }   // convention: jump to -1 halts
            step();
        }
    }
};

} // namespace ember32
