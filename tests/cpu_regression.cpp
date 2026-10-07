// ROM-free instruction-level regressions using only the public CPU/bus API.
#include "cpu/arm920t.h"
#include <iostream>
using namespace oceanblast;
static int failures = 0;
static void check(const char* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
static void thumb(Bus& b, ARM920T& c, u16 instruction) {
    b.write32(0, 0xe12fff13); // BX r3
    b.write16(0x100, instruction);
    c.setReg(3, 0x101);
    c.step();
    c.step();
}
int main() {
    { Bus b; ARM920T c(b); b.write32(0,0xe2900001); c.setReg(0,0xffffffff); c.step();
      check("ARM ADDS overflow-to-zero carries",c.getReg(0)==0 && (c.getCPSR()&FLAG_C) && (c.getCPSR()&FLAG_Z)); }
    { Bus b; ARM920T c(b); c.setReg(0,0xffffffff); c.setReg(1,1); thumb(b,c,0x1840); // ADDS r0,r0,r1
      check("Thumb aliased ADDS uses original operands for carry",c.getReg(0)==0 && (c.getCPSR()&FLAG_C)); }
    { Bus b; ARM920T c(b); c.setReg(0,0x80000000); thumb(b,c,0x0040); // LSLS r0,r0,#1
      check("Thumb LSLS updates carry",c.getReg(0)==0 && (c.getCPSR()&FLAG_C)); }
    { Bus b; ARM920T c(b); c.setReg(0,0x200); thumb(b,c,0x4700); // BX r0
      check("Thumb BX returns to ARM",c.getPC()==0x200 && !c.isThumb()); }
    { Bus b; ARM920T c(b); c.setReg(0,0x30000000); b.write32(0x30000000,0x12345678); thumb(b,c,0x6801); // LDR r1,[r0]
      check("Thumb LDR word executes",c.getReg(1)==0x12345678); }
    { Bus b; ARM920T c(b); thumb(b,c,0xdf00); // SWI 0
      check("Thumb SWI enters supervisor vector with Thumb return address",c.getPC()==8 && c.getLR()==0x102 && !c.isThumb()); }
    { Bus b; ARM920T c(b); b.write32(0,0xe121f001); c.setReg(1,0x10); c.step(); // MSR CPSR_c,r1
      b.write32(4,0xe121f001); c.setReg(1,0x13); c.step();
      check("User MSR cannot switch to privileged SVC mode",(c.getCPSR()&31)==0x10); }
    std::cout << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
