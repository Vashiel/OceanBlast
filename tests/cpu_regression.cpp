// ROM-free instruction-level regressions using only the public CPU/bus API.
#include "cpu/arm920t.h"
#include "core/input_script.h"
#include <iostream>
#include <sstream>
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
    { Bus b; ARM920T c(b); b.write32(0,0xe2933001); b.write32(4,0xe7910062);
      c.setReg(3,0xffffffff); c.setReg(1,0xb0000000); c.setReg(2,0); b.write32(0x30000000,0x12345678); c.step(); c.step();
      check("ARM memory RRX offset uses CPSR carry",c.getReg(0)==0x12345678); }
    for (u32 offset=1; offset<=3; ++offset) {
        Bus b; ARM920T c(b); b.write32(0,0xe5910000); c.setReg(1,0x30000100+offset);
        b.write32(0x30000100,0x12345678); b.write32(0x30000104,0xaabbccdd); c.step();
        const u32 expected = (0x12345678u >> (offset*8)) | (0x12345678u << (32-offset*8));
        check("ARM unaligned LDR rotates an aligned word",c.getReg(0)==expected);
    }
    { Bus b; ARM920T c(b); b.write32(0,0xe5810000); c.setReg(1,0x30000101); c.setReg(0,0x12345678);
      b.write32(0x30000104,0xaabbccdd); c.step();
      check("ARM unaligned STR aligns without corrupting next word",b.read32(0x30000100)==0x12345678 && b.read32(0x30000104)==0xaabbccdd); }
    { Bus b; ARM920T c(b); c.setReg(1,0x30000100); c.setReg(2,1); b.write32(0x30000100,0x12345678);
      thumb(b,c,0x5888); check("Thumb unaligned LDR rotates an aligned word",c.getReg(0)==0x78123456); }
    { std::vector<InputEvent> events; std::string error;
      std::istringstream valid("# step mask\n0 0\n100 0x100 # Start\n200 0\n");
      check("Input replay parses timed presses and release",readInputScript(valid,events,error) && events.size()==3 && events[1].step==100 && events[1].mask==0x100 && events[2].mask==0);
      std::istringstream unordered("100 1\n50 0\n");
      check("Input replay rejects unordered events",!readInputScript(unordered,events,error) && events.empty());
      std::istringstream invalid("0 400\n");
      check("Input replay rejects unsupported button bits",!readInputScript(invalid,events,error)); }
    { Bus b; ARM920T c(b); b.write32(0,0xe11000a1); c.setReg(0,1); c.setReg(1,3); c.step();
      check("ARM TST receives carry from shifted operand",(c.getCPSR()&FLAG_C) && !(c.getCPSR()&FLAG_Z) && c.getReg(0)==1); }
    { Bus b; ARM920T c(b); b.write32(0,0xe13000a1); c.setReg(0,1); c.setReg(1,3); c.step();
      check("ARM TEQ receives carry and zero without register write",(c.getCPSR()&FLAG_C) && (c.getCPSR()&FLAG_Z) && c.getReg(0)==1); }
    for (u16 op = 0; op < 8; ++op) {
        Bus b; ARM920T c(b);
        c.setReg(0, 0x1234abcd); c.setReg(1, 0x30000100); c.setReg(2, 4);
        b.write32(0x30000104, 0x89abcdef);
        thumb(b,c,static_cast<u16>(0x5000 | (op << 9) | (2 << 6) | (1 << 3)));
        const char* names[] = {"Thumb STR register", "Thumb STRH register", "Thumb STRB register", "Thumb LDSB register",
                               "Thumb LDR register", "Thumb LDRH register", "Thumb LDRB register", "Thumb LDSH register"};
        const u32 expected[] = {0x1234abcd,0x89ababcd,0x89abcdcd,0xffffffef,0x89abcdef,0xcdef,0xef,0xffffcdef};
        check(names[op], (op < 3 ? b.read32(0x30000104) : c.getReg(0)) == expected[op]);
    }
    { Bus b; ARM920T c(b); c.setReg(13,0x30000200); c.setReg(0,0x11223344); c.setReg(14,0x12345678);
      thumb(b,c,0xb501); // PUSH {r0,lr}
      check("Thumb PUSH stores low register and LR",c.getSP()==0x300001f8 && b.read32(c.getSP())==0x11223344 && b.read32(c.getSP()+4)==0x12345678); }
    { Bus b; ARM920T c(b); c.setReg(13,0x30000200); b.write32(0x30000200,0x11223344); b.write32(0x30000204,0x201);
      thumb(b,c,0xbd01); // POP {r0,pc}
      check("Thumb POP restores registers and branches",c.getReg(0)==0x11223344 && c.getSP()==0x30000208 && c.getPC()==0x200 && c.isThumb()); }
    { Bus b;
      b.write32(0x4b000080,0x30010000); b.write32(0x4b000088,0x55000010);
      b.write32(0x4b00008c,1); b.write32(0x4b000090,16|(1u<<20)|(1u<<22)); b.write32(0x4b0000a0,2);
      check("DMA current source starts at configured buffer",b.read32(0x4b000098)==0x30010000);
      b.write32(0x4b000080,0x30020000); b.tick((uint64_t(32)*20000000/22050/4)/2);
      check("DMA pointer advances through latched source",b.read32(0x4b000098)==0x30010010 && b.read32(0x4b000094)==8);
      check("DMA fixed IIS destination does not advance",b.read32(0x4b00009c)==0x55000010);
      b.tick(100000);
      check("DMA completion exposes end pointer",b.read32(0x4b000098)==0x30010020 && b.read32(0x4b000094)==0); }
    { Bus b;
      b.write32(0x4b000080,0x30010000); b.write32(0x4b000090,16); b.write32(0x4b0000a0,2);
      b.write32(0x4b000080,0x30020000); b.write32(0x4b000090,32);
      check("DMA current count is independent of queued count",b.read32(0x4b000094)==16);
      b.tick(150000);
      check("DMA autoreload latches queued buffer before IRQ",b.read32(0x4b000094)==32 && (b.getMmio(0x4a000000)&(1u<<19)));
      b.write32(0x4b000090,32|(1u<<22)); b.tick(150000);
      check("DMA NORELOAD stops after current buffer",b.read32(0x4b000094)==0 && !(b.read32(0x4b0000a0)&2)); }
    { Bus b; ARM920T c(b); b.write32(0,0xe1b00021); c.setReg(1,0x80000000); c.step();
      check("ARM immediate LSR zero encoding means 32",c.getReg(0)==0 && (c.getCPSR()&FLAG_C)); }
    { Bus b; ARM920T c(b); b.write32(0,0xe1b00041); c.setReg(1,0x80000000); c.step();
      check("ARM immediate ASR zero encoding means 32",c.getReg(0)==0xffffffff && (c.getCPSR()&FLAG_C)); }
    { Bus b; ARM920T c(b); b.write32(0,0xe1b00061); c.setReg(1,3); c.step();
      check("ARM RRX uses old carry and bit zero",c.getReg(0)==1 && (c.getCPSR()&FLAG_C)); }
    { Bus b; ARM920T c(b); b.write32(0,0xe0a00081); c.setReg(0,10); c.setReg(1,0x80000000); c.step();
      check("ARM ADC uses CPSR carry rather than shifter carry",c.getReg(0)==10); }
    { Bus b; ARM920T c(b); b.write32(0,0xe1b00271); c.setReg(1,0x80000000); c.setReg(2,32); c.step();
      check("ARM register ROR 32 updates carry from bit31",c.getReg(0)==0x80000000 && (c.getCPSR()&FLAG_C)); }
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
