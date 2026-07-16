// ember32/tools/exception_test.cpp — verify the privileged machine: banked modes,
// per-mode SPSR, the exception entry/return sequence, and IRQ/FIQ from the bus
// interrupt controller. A firmware-style ROM (vector table + handlers) sets up a
// distinct SP per mode and a FIQ-banked r8, then takes a SWI, an IRQ (vblank) and
// a FIQ (vblank routed to FIQ), each handler leaving markers in RAM we check.
// Also checks the S/N/I cycle counter on a hand-counted program.
//   c++ -std=c++17 -O2 -I.. tools/exception_test.cpp -o /tmp/e32exc && /tmp/e32exc
#include "../cpu_arm7.hpp"
#include <cstdio>
using namespace ember32;

// ---- ARM encoders (pure) ----
static bool arm_imm(uint32_t c, uint32_t& e){ e=0; for(int r=0;r<16;r++){int k=2*r;uint32_t v=k?((c<<k)|(c>>(32-k))):c; if(v<=0xFF){e=(uint32_t(r)<<8)|v;return true;}} return false; }
static uint32_t mov_imm(int rd,uint32_t c){uint32_t e; if(!arm_imm(c,e)) std::fprintf(stderr,"!! imm %x needs >1 instr\n",c); return 0xE3A00000u|(rd<<12)|e;}
static uint32_t mvn_imm(int rd,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE3E00000u|(rd<<12)|e;}
static uint32_t str_off(int rt,int rn,int off){return 0xE5800000u|(rn<<16)|(rt<<12)|(off&0xFFF);}
static uint32_t b_ins(uint32_t from,uint32_t to){return 0xEA000000u|(((to-(from+8))>>2)&0xFFFFFF);}
static uint32_t msr_cpsr_c(uint32_t imm){return 0xE321F000u|(imm&0xFF);}   // MSR CPSR_c,#imm
static uint32_t mrs_cpsr(int rd){return 0xE10F0000u|(rd<<12);}
static uint32_t mrs_spsr(int rd){return 0xE14F0000u|(rd<<12);}
static uint32_t and_imm(int rd,int rn,uint32_t c){uint32_t e;arm_imm(c,e);return 0xE2000000u|(rn<<16)|(rd<<12)|e;}
static uint32_t movs_pc_lr(){return 0xE1B0F00Eu;}       // MOVS pc,lr  (SWI/undef return)
static uint32_t subs_pc_lr_4(){return 0xE25EF004u;}     // SUBS pc,lr,#4 (IRQ/FIQ return)
static uint32_t swi_(uint32_t n){return 0xEF000000u|(n&0xFFFFFF);}

int main(){
    static Bus bus; CPU cpu; cpu.bus=&bus;
    auto W=[&](uint32_t a,uint32_t v){ bus.w32(a,v); };

    // ---- vector table (0x00..0x1C) ----
    W(0x00, b_ins(0x00,0x100));   // reset -> init
    W(0x04, b_ins(0x04,0x100));   // undefined (unused here)
    W(0x08, b_ins(0x08,0x200));   // SWI
    W(0x0C, b_ins(0x0C,0x0C));    // prefetch abort (spin)
    W(0x10, b_ins(0x10,0x10));    // data abort (spin)
    W(0x14, b_ins(0x14,0x14));    // reserved
    W(0x18, b_ins(0x18,0x240));   // IRQ
    W(0x1C, b_ins(0x1C,0x280));   // FIQ

    // ---- init (0x100), boots in System mode ----
    uint32_t p=0x100;
    auto E=[&](uint32_t w){ W(p,w); p+=4; };
    E(mov_imm(13,0x1000));                 // sys sp
    E(msr_cpsr_c(MODE_SVC)); E(mov_imm(13,0x2000));   // svc sp
    E(msr_cpsr_c(MODE_IRQ)); E(mov_imm(13,0x3000));   // irq sp
    E(msr_cpsr_c(MODE_FIQ)); E(mov_imm(13,0x4000)); E(mov_imm(8,0xF1)); // fiq sp + fiq-banked r8
    E(msr_cpsr_c(MODE_SYS)); E(mov_imm(8,0x51));      // back to System; sys r8
    E(mov_imm(4,0x900));                   // r4 = RAM scratch base (not banked)
    E(str_off(13,4,0x00));                 // [0x900] = sys r13   (expect 0x1000)
    // -- SWI --
    E(swi_(0));
    E(mrs_cpsr(5)); E(and_imm(5,5,0x1F)); E(str_off(5,4,0x04)); // [0x904] mode after SWI (expect 0x1F)
    // -- IRQ via vblank --
    E(mov_imm(0,0x04000000)); E(mov_imm(1,1));
    E(str_off(1,0,0x20));                  // IRQ_ENABLE = VBLANK
    E(str_off(1,0,0x04));                  // RENDER -> vblank; IRQ taken before the next fetch
    // -- FIQ via vblank routed to FIQ --
    E(mov_imm(1,0)); E(str_off(1,0,0x20)); // IRQ_ENABLE = 0
    E(mov_imm(1,1)); E(str_off(1,0,0x28)); // FIQ_ENABLE = VBLANK
    E(str_off(1,0,0x04));                  // RENDER -> vblank; FIQ taken before the next fetch
    E(str_off(8,4,0x20));                  // [0x920] = sys r8 after FIQ (expect 0x51, uncorrupted)
    E(mvn_imm(15,0));                       // halt

    // ---- SWI handler (0x200), SVC mode ----
    p=0x200;
    E(mov_imm(6,0x51)); E(str_off(6,4,0x08));        // [0x908] swi marker
    E(mrs_spsr(6)); E(and_imm(6,6,0x1F)); E(str_off(6,4,0x0C)); // [0x90C] SPSR mode (expect 0x1F)
    E(str_off(13,4,0x10));                            // [0x910] svc r13 (expect 0x2000)
    E(movs_pc_lr());

    // ---- IRQ handler (0x240), IRQ mode ----
    p=0x240;
    E(mov_imm(6,0x19)); E(str_off(6,4,0x14));        // [0x914] irq marker
    E(str_off(13,4,0x18));                            // [0x918] irq r13 (expect 0x3000)
    E(mov_imm(0,0x04000000)); E(mov_imm(1,1)); E(str_off(1,0,0x24)); // ack vblank
    E(subs_pc_lr_4());

    // ---- FIQ handler (0x280), FIQ mode ----
    p=0x280;
    E(mov_imm(6,0xF1)); E(str_off(6,4,0x1C));        // [0x91C] fiq marker
    E(str_off(13,4,0x28));                            // [0x928] fiq r13 (expect 0x4000)
    E(str_off(8,4,0x2C));                             // [0x92C] fiq-banked r8 (expect 0xF1)
    E(mov_imm(0,0x04000000)); E(mov_imm(1,1)); E(str_off(1,0,0x24)); // ack vblank
    E(subs_pc_lr_4());

    cpu.reset(0); cpu.run(2000);

    struct Chk{ const char* what; uint32_t addr, want; };
    Chk chks[] = {
        {"sys r13 banked",        0x900, 0x1000},
        {"mode after SWI return", 0x904, 0x1F},
        {"SWI handler ran",       0x908, 0x51},
        {"SWI SPSR = caller mode",0x90C, 0x1F},
        {"SVC r13 banked",        0x910, 0x2000},
        {"IRQ handler ran",       0x914, 0x19},
        {"IRQ r13 banked",        0x918, 0x3000},
        {"FIQ handler ran",       0x91C, 0xF1},
        {"sys r8 uncorrupted",    0x920, 0x51},
        {"FIQ r13 banked",        0x928, 0x4000},
        {"FIQ-banked r8",         0x92C, 0xF1},
    };
    int pass=0, n=sizeof(chks)/sizeof(chks[0]);
    for(auto&c:chks){ uint32_t got=bus.r32(c.addr); bool ok=got==c.want; pass+=ok;
        std::printf("  [%s] %-24s got %08X want %08X\n", ok?"PASS":"FAIL", c.what, got, c.want); }
    std::printf("  halted=%d  cycles=%llu  tcycles=%llu\n",
                cpu.halted, (unsigned long long)cpu.cycles, (unsigned long long)cpu.tcycles);

    // ---- exact S/N/I timing on a hand-counted program: 3 MOVs (1S each) + MOVS-to-PC
    // halt (mvn r15 = 2S+1N). Expect tcycles = 1+1+1+3 = 6, cycles = 4. ----
    static Bus tb; CPU tc; tc.bus=&tb;
    tb.w32(0, mov_imm(0,1)); tb.w32(4, mov_imm(1,2)); tb.w32(8, mov_imm(2,3)); tb.w32(12, mvn_imm(15,0));
    tc.reset(0); tc.run(100);
    bool tok = tc.cycles==4 && tc.tcycles==6;
    std::printf("  [%s] timing model            cycles=%llu (want 4)  tcycles=%llu (want 6)\n",
                tok?"PASS":"FAIL", (unsigned long long)tc.cycles, (unsigned long long)tc.tcycles);
    pass+=tok; n++;

    std::printf("%d/%d passed\n", pass, n);
    return (pass==n && cpu.halted)?0:1;
}
