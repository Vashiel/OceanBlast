#include "core/emulation_clock.h"
#include "cpu/arm920t.h"
#include <iostream>
using namespace oceanblast;
static int failures = 0;
static void check(const char* label, bool success) {
    std::cout << (success ? "PASS " : "FAIL ") << label << '\n';
    failures += !success;
}
static void arm(Bus& bus, ARM920T& cpu, u32 instr) {
    bus.write32(cpu.getPC(), instr); cpu.step(0);
}
int main() {
    { EmulationClock c; uint64_t ticks = 0;
      for (unsigned i=0;i<1800000;++i) ticks += c.advance(1,180000000);
      check("180 MHz cycles advance 20 MHz peripheral time",ticks>=200000 && ticks<=200001);
      check("Clock reports accumulated modeled seconds",c.seconds()>=0.01 && c.seconds()<0.010001);
    }
    { EmulationClock c; c.advance(1,40000000); c.advance(1,80000000); c.advance(1,80000000);
      check("Clock changes preserve fractional elapsed time",c.elapsedTicks()==1);
      c.advanceIdle(20000); check("Idle advancement retains modeled time",c.elapsedTicks()==20001);
    }
    { Bus b; ARM920T c(b); c.setCycleTiming(true);
      arm(b,c,0xe1a00000); check("Cached data operation baseline is one cycle",c.getLastCycles()==1);
      arm(b,c,0xe1a00211); check("Register controlled shift adds a cycle",c.getLastCycles()==2);
      arm(b,c,0x0affffff); check("Failed condition does not charge taken branch cycles",c.getLastCycles()==1);
      arm(b,c,0xeaffffff); check("Taken ARM branch charges refill cycles",c.getLastCycles()==3);
    }
    { Bus b;ARM920T c(b);c.setCycleTiming(true);c.setReg(0,0x30000000);c.setReg(1,2);c.setReg(2,3);
      arm(b,c,0xe0000291);check("Short multiply uses early termination",c.getReg(0)==6 && c.getLastCycles()==2);
      c.setReg(1,2);c.setReg(2,0x12345678);arm(b,c,0xe0000291);
      check("Large multiplier charges additional cycles",c.getLastCycles()==5);
    }
    { Bus b;ARM920T c(b);c.setReg(0,0x30000000);
      arm(b,c,0xe890000e);check("Multiple register load charges transferred words",c.getLastCycles()==3);
      b.write32(0x30000000,0x100);arm(b,c,0xe590f000);
      check("Load to PC charges pipeline refill",c.getPC()==0x100 && c.getLastCycles()==5);
    }
    { Bus b;ARM920T c(b); b.write32(0x4c000004,0x52011);b.write32(0x4c000014,3);
      check("Reset FastBus execution uses HCLK",c.getExecutionClock()==90000000);
      c.setReg(0,0x40000070);arm(b,c,0xee010f10);
      check("Synchronous cached execution uses FCLK",c.getExecutionClock()==180000000);
      c.setReg(0,0xc0000070);arm(b,c,0xee010f10);
      check("Asynchronous cached execution uses FCLK",c.getExecutionClock()==180000000);
    }
    { Bus b;ARM920T c(b);c.setCycleTiming(true);
      arm(b,c,0xee070f90);check("CP15 wait enters idle state",c.isWaitingForInterrupt() && c.getPC()==4);
      b.write32(4,0xe3a00007);c.step(0);
      check("Idle CPU neither fetches nor retires the following instruction",c.getPC()==4 && c.getReg(0)==0 && c.getLastCycles()==0);
      b.write32(0x4a000008,~1u);b.setButtonMask(1u<<4);c.step(0);
      check("Pending IRQ wakes idle even with CPSR IRQ masked",!c.isWaitingForInterrupt() && c.getPC()==8 && c.getReg(0)==7);
    }
    { Bus b;ARM920T c(b);c.setCycleTiming(true);c.setReg(0,0x13);arm(b,c,0xe121f000);
      arm(b,c,0xee070f90);b.write32(0x4a000008,~1u);b.setButtonMask(1u<<4);c.step(0);
      check("Unmasked IRQ wakes idle into vector with correct return link",c.getPC()==0x18 && c.getLR()==12 && !c.isWaitingForInterrupt());
      c.reset();check("Reset clears idle state",!c.isWaitingForInterrupt());
    }
    { Bus b;ARM920T c(b);arm(b,c,0xee070f90);
      check("Legacy stepping retains previous wait behavior",!c.isWaitingForInterrupt());
    }
    { Bus b; b.write32(0x4c000004,0x52011);b.write32(0x4c000014,3);
      b.write32(0x5100003c,99);b.write32(0x51000008,(1u<<20)|(1u<<21));
      const auto next=b.ticksUntilEvent();b.tick(next-1);
      check("Idle bound ends at Timer 4 deadline",next==89 && !(b.getMmio(0x4a000000)&(1u<<14)));
      b.tick(1);check("Idle timer deadline raises interrupt",(b.getMmio(0x4a000000)&(1u<<14))!=0);
    }
    { Bus b;b.write32(0x4b000080,0x30010000);b.write32(0x4b000088,0x55000010);
      b.write32(0x4b000090,16|(1u<<20)|(1u<<22));b.write32(0x4b0000a0,2);
      size_t samples=0;b.setAudioCallback([&](const int16_t*,size_t n){samples+=n;});
      uint64_t elapsed=0;while(b.getMmio(0x4b000094)){auto next=b.ticksUntilEvent();b.tick(next);elapsed+=next;}
      check("Idle DMA advancement captures every sample before completion",samples==16 && elapsed==uint64_t(32)*20000000/22050/4);
    }
    std::cout << failures << " failure(s)\n";return failures?1:0;
}
