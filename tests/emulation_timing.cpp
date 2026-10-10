#include "core/emulation_clock.h"
#include "core/execution_batch.h"
#include "cpu/arm920t.h"
#include "../tools/kernel_jiffies.h"
#include "../tools/guest_ram.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <cstdio>
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
    { Bus bus;u32 word=0;bus.write32(0x30002000,0x12345678);
      check("Diagnostic RAM inspection reads physical SDRAM",readGuestRam32(bus,0x30002000,word)&&word==0x12345678);
      bus.setTtb(0x30004000);bus.write32(0x30004004,0x30000002);bus.setMmuEnabled(true);bus.setUserMode(true);
      check("Diagnostic section inspection does not change guest privilege",readGuestRam32(bus,0x00102000,word)&&word==0x12345678&&bus.isUserMode());
      bus.setMmuEnabled(false);bus.write32(0x30004008,0x30008001);bus.write32(0x3000800c,0x30002002);bus.setMmuEnabled(true);
      check("Diagnostic coarse-page inspection reads mapped RAM",readGuestRam32(bus,0x00203000,word)&&word==0x12345678);
      check("Diagnostic RAM inspection rejects unmapped and unaligned addresses",!readGuestRam32(bus,0x00300000,word)&&!readGuestRam32(bus,0x00203001,word));
      bus.setMmuEnabled(false);bus.setMmioProfiling(true);
      check("Diagnostic RAM inspection never reads MMIO",!readGuestRam32(bus,0x51000040,word)&&bus.getMmioProfile().empty());
    }
    { Bus bus;ARM920T cpu(bus);cpu.reset();
      arm(bus,cpu,0xe321f010); // MSR CPSR_c, user mode.
      cpu.setReg(15,0);
      u32 number=0,callPC=0;std::array<u32,6> args{};
      cpu.setSyscallObserver([&](u32 n,u32 pc,const std::array<u32,6>& a){number=n;callPC=pc;args=a;});
      cpu.setReg(0,9);cpu.setReg(1,0x4680);cpu.setReg(7,54);
      bus.setMmuEnabled(false);
      // Execute under an identity section mapping, without loading a cartridge.
      bus.write32(ADDR_SDRAM_BASE+0x4000,0x00000c02);bus.setTtb(ADDR_SDRAM_BASE+0x4000);bus.setDacr(3);
      bus.write32(0,0xef900036);bus.setMmuEnabled(true);cpu.step(0);
      check("OABI syscall observation precedes exception entry and preserves arguments",number==54 && callPC==0 && args[0]==9 && args[1]==0x4680 && cpu.getPC()==8);
      cpu.reset();arm(bus,cpu,0xe321f010);cpu.setReg(15,0);cpu.setReg(7,54);
      number=0;callPC=UINT32_MAX;
      bus.setTtb(ADDR_SDRAM_BASE+0x4000);bus.setDacr(3);
      bus.write32(0,0xef000000);bus.setMmuEnabled(true);cpu.step(0);
      check("Zero-immediate EABI syscall observation uses r7",number==54 && callPC==0 && args[0]==0 && cpu.getPC()==8);
    }
    { Bus bus;bus.reset();
      for(unsigned i=0;i<8;++i)bus.write8(ADDR_SDRAM_BASE+0x100+i,"jiffies"[i]);
      for(unsigned i=0;i<11;++i)bus.write8(ADDR_SDRAM_BASE+0x120+i,"jiffies_64"[i]);
      bus.write32(ADDR_SDRAM_BASE+0x200,0xc0000300);bus.write32(ADDR_SDRAM_BASE+0x204,0xc0000100);
      bus.write32(ADDR_SDRAM_BASE+0x208,0xc0000300);bus.write32(ADDR_SDRAM_BASE+0x20c,0xc0000120);
      check("Jiffies discovery requires agreeing kernel export pairs",findKernelJiffies(bus)==0xc0000300);
      bus.setTtb(ADDR_SDRAM_BASE+0x4000);bus.write32(ADDR_SDRAM_BASE+0x7000,0x30000402);
      bus.write32(ADDR_SDRAM_BASE+0x300,123);bus.setMmuEnabled(true);bus.setUserMode(true);
      u32 value=0;check("Jiffies inspection verifies section mapping without changing privilege",readKernelJiffies(bus,0xc0000300,value)&&value==123&&bus.isUserMode());
      bus.setMmuEnabled(false);bus.write32(ADDR_SDRAM_BASE+0x7000,0x30100402);bus.setMmuEnabled(true);
      check("Jiffies inspection rejects an unexpected physical mapping",!readKernelJiffies(bus,0xc0000300,value));
      bus.setMmuEnabled(false);bus.write32(ADDR_SDRAM_BASE+0x208,0xc0000304);
      check("Jiffies discovery rejects disagreeing exports",findKernelJiffies(bus)==0);
    }
    { Bus bus;bus.setMmioProfiling(true);
      bus.write32(0x4d000000,0x579);bus.read32(0x4d000000);
      bus.getMmio(0x4d000000);bus.getMmio(0x4d000000);
      const char* path="build/mmio-profile-test.csv";
      bool saved=bus.saveMmioProfile(path);std::ifstream input(path);
      std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
      input.close();std::remove(path);
      check("MMIO profile excludes host register inspection",saved&&text.find("0x4d000000,1,1")!=std::string::npos);
    }
    { Bus scalarBus,batchBus;ARM920T scalar(scalarBus),batch(batchBus);
      EmulationClock scalarClock,batchClock;uint64_t batchTicks=0;
      for(Bus* bus:{&scalarBus,&batchBus}) {
        bus->write32(0,0xe2800001);bus->write32(4,0xeafffffd);
        bus->write32(0x5100003c,29);bus->write32(0x51000008,(1u<<20)|(1u<<21)|(1u<<22));
      }
      scalar.setCycleTiming(true);batch.setCycleTiming(true);
      const size_t retired=runClockedInstructions(batch,batchBus,batchClock,10001,UINT64_MAX,batchTicks);
      for(size_t i=0;i<retired;++i) {
        auto hz=scalar.getExecutionClock();scalar.step(0);scalarBus.tick(scalarClock.advance(scalar.getLastCycles(),hz));
      }
      check("Clocked batch preserves CPU, fractional clock and timer state",
        retired==10001&&scalar.getReg(0)==batch.getReg(0)&&scalar.getPC()==batch.getPC()&&
        scalarClock.elapsedTicks()==batchTicks&&scalarBus.getMmio(0x51000040)==batchBus.getMmio(0x51000040));
      const size_t bounded=runClockedInstructions(batch,batchBus,batchClock,10000,batchTicks+7,batchTicks);
      check("Clocked batch yields at the modeled time deadline",bounded>0&&bounded<10000);
    }
    { Bus bus;ARM920T cpu(bus);EmulationClock clock;uint64_t ticks=0;
      cpu.setCycleTiming(true);bus.write32(0,0xee070f90);bus.write32(4,0xe3a00007);
      check("Clocked batch yields immediately after wait instruction",
        runClockedInstructions(cpu,bus,clock,100,UINT64_MAX,ticks)==1&&cpu.isWaitingForInterrupt()&&cpu.getPC()==4);
      check("Clocked batch does not retire an already waiting CPU",
        runClockedInstructions(cpu,bus,clock,100,UINT64_MAX,ticks)==0&&cpu.getReg(0)==0);
    }
    for (size_t ratio : {1u, 2u, 4u, 16u}) for (size_t phase = 0; phase < ratio; ++phase) {
        uint64_t count = 0, ticks = 0; size_t currentPhase = phase;
        while (ticks < 3) { ticks += currentPhase == 0; ++count; if (++currentPhase == ratio) currentPhase = 0; }
        check("Batch limit ends at exact peripheral deadline for every phase",
            legacyStepsUntilTickLimit(3, ratio, phase) == count && legacyStepsUntilTickLimit(0, ratio, phase) == 0);
    }
    for (size_t ratio : {1u, 2u, 4u}) {
        Bus scalarBus, batchBus; ARM920T scalar(scalarBus), batch(batchBus);
        // ADD r0,r0,#1; B back to ADD. Timer IRQ also exercises masked pending state.
        for (Bus* bus : {&scalarBus, &batchBus}) {
            bus->write32(0, 0xe2800001); bus->write32(4, 0xeafffffd);
            bus->write32(0x5100003c, 29); bus->write32(0x51000008,(1u<<20)|(1u<<21)|(1u<<22));
        }
        size_t phase = 0; uint64_t ticks = 0;
        runLegacyInstructions(batch, 37, ratio, phase, ticks);
        runLegacyInstructions(batch, 964, ratio, phase, ticks);
        for (size_t i = 0; i < 1001; ++i) scalar.step(i % ratio == 0 ? 1 : 0);
        check("Batched CPU preserves registers and phase across split budgets",
            scalar.getPC() == batch.getPC() && scalar.getReg(0) == batch.getReg(0) &&
            scalar.getCPSR() == batch.getCPSR() && phase == 1001 % ratio && ticks == (1001 + ratio - 1) / ratio);
        check("Batched CPU preserves timer and pending interrupts",
            scalarBus.getMmio(0x51000040) == batchBus.getMmio(0x51000040) &&
            scalarBus.getMmio(0x4a000000) == batchBus.getMmio(0x4a000000));
    }
    { Bus scalarBus, batchBus; ARM920T scalar(scalarBus), batch(batchBus);
      for (Bus* bus : {&scalarBus, &batchBus}) {
        bus->write32(0,0xe2800001);bus->write32(4,0xeafffffd);bus->write32(0x18,0xe25ef004);
        bus->write32(0x4a000008,~(1u<<14));bus->write32(0x5100003c,29);
        bus->write32(0x51000008,(1u<<20)|(1u<<21));
      }
      for (ARM920T* cpu : {&scalar,&batch}) {
        cpu->setReg(2,0x13);cpu->setReg(15,0x100);
      }
      scalarBus.write32(0x100,0xe121f002);batchBus.write32(0x100,0xe121f002);
      scalarBus.write32(0x104,0xeaffffbd);batchBus.write32(0x104,0xeaffffbd);
      size_t phase=0;uint64_t ticks=0;
      runLegacyInstructions(batch,701,2,phase,ticks);
      for(size_t i=0;i<701;++i)scalar.step(i%2==0?1:0);
      bool same=true;for(int i=0;i<16;++i)same=same&&scalar.getReg(i)==batch.getReg(i);
      check("Batch preserves unmasked IRQ entry and exception return",same&&scalar.getCPSR()==batch.getCPSR());
    }
    { Bus scalarBus,batchBus;ARM920T scalar(scalarBus),batch(batchBus);
      std::vector<int16_t> scalarPcm,batchPcm;
      scalarBus.setAudioCallback([&](const int16_t* p,size_t n){scalarPcm.insert(scalarPcm.end(),p,p+n);});
      batchBus.setAudioCallback([&](const int16_t* p,size_t n){batchPcm.insert(batchPcm.end(),p,p+n);});
      for(Bus* bus:{&scalarBus,&batchBus}) {
        bus->write32(0,0xeafffffe);
        for(uint32_t i=0;i<512;++i)bus->write16(0x30010000+i*2,uint16_t(i*31));
        bus->write32(0x4b000080,0x30010000);bus->write32(0x4b000088,0x55000010);
        bus->write32(0x4b000090,512|(1u<<20)|(1u<<22));bus->write32(0x4b0000a0,2);
      }
      size_t phase=0;uint64_t ticks=0;
      runLegacyInstructions(batch,600001,2,phase,ticks);
      for(size_t i=0;i<600001;++i)scalar.step(i%2==0?1:0);
      check("Batch preserves every consumed DMA audio sample",scalarPcm==batchPcm&&scalarPcm.size()==512);
      check("Batch preserves DMA completion registers",scalarBus.getMmio(0x4b000098)==batchBus.getMmio(0x4b000098)&&scalarBus.getMmio(0x4b000094)==batchBus.getMmio(0x4b000094));
    }
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
