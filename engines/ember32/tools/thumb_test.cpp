// ember32/tools/thumb_test.cpp — verify the Thumb decoder. An ARM stub BX'es into
// Thumb; the Thumb code computes 4*5+3 (MUL/ADD imm) and STRs it to 0x100 via a
// shifted address (LSL), then BX'es back to an ARM MVN-halt. Checks the result
// and that ARM↔Thumb interworking + the T bit round-trip correctly.
//   c++ -std=c++17 -O2 -I.. tools/thumb_test.cpp -o /tmp/e32t && /tmp/e32t
#include "../cpu_arm7.hpp"
#include <cstdio>
using namespace ember32;

int main(){
    static Bus bus;
    // ARM stub @0: mov r0,#0x11 (Thumb entry 0x10 | thumb-bit); bx r0
    bus.w32(0x00, 0xE3A00011); bus.w32(0x04, 0xE12FFF10);
    // Thumb @0x10
    uint16_t t[] = {
        0x2104,  // mov r1,#4
        0x2205,  // mov r2,#5
        0x4351,  // mul r1,r2      -> 20
        0x3103,  // add r1,#3      -> 23
        0x2340,  // mov r3,#0x40
        0x009B,  // lsl r3,r3,#2   -> 0x100
        0x6019,  // str r1,[r3]
        0x2030,  // mov r0,#0x30   (ARM halt entry, bit0=0)
        0x4700,  // bx r0
    };
    for(size_t i=0;i<sizeof(t)/2;i++) bus.w16(0x10 + i*2, t[i]);
    // ARM halt @0x30
    bus.w32(0x30, 0xE3E0F000);   // mvn r15,#0 -> PC=0xFFFFFFFF -> halt

    CPU cpu; cpu.bus=&bus; cpu.reset(0); cpu.run(1000);
    uint32_t res = bus.r32(0x100);
    std::printf("ran %llu instrs, halted=%d, [0x100]=%u (expect 23), back-in-ARM T=%d\n",
                (unsigned long long)cpu.cycles, cpu.halted, res, (cpu.cpsr>>5)&1);
    bool ok = cpu.halted && res==23 && ((cpu.cpsr>>5)&1)==0;
    std::printf(ok ? "THUMB OK\n" : "!! THUMB FAIL\n");
    return ok?0:1;
}
