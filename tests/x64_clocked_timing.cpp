#include "core/machine.h"
#include "cpu/x64_execution_engine.h"
#include "cpu/arm920t_cycles.h"
#include <iostream>
#include <vector>
using namespace oceanblast;
static int failures=0;
static NativeCacheLimits clockedLimits() { NativeCacheLimits limits;limits.clockedNative=true;return limits; }
static void check(const char* name,bool value) { std::cout<<(value?"PASS ":"FAIL ")<<name<<'\n'; failures+=!value; }
static bool same(Machine& a,Machine& b) {
    const auto& x=a.state(); const auto& y=b.state();
    for(unsigned i=0;i<16;++i) if(x.r[i]!=y.r[i]) return false;
    return x.cpsr==y.cpsr && x.spsr==y.spsr && x.spsr_irq==y.spsr_irq &&
        x.r13_irq==y.r13_irq && x.r14_irq==y.r14_irq && x.waitingForInterrupt==y.waitingForInterrupt &&
        x.cp15_control==y.cp15_control && x.cp15_ttb==y.cp15_ttb && x.cp15_dacr==y.cp15_dacr &&
        x.cp15_fsr==y.cp15_fsr && x.cp15_far==y.cp15_far &&
        a.execution().time().ticks==b.execution().time().ticks &&
        a.execution().time().idleTicks==b.execution().time().idleTicks &&
        a.bus().getTimer4Expirations()==b.bus().getTimer4Expirations() &&
        a.bus().getMmio(0x4a000000)==b.bus().getMmio(0x4a000000);
}
static void setup(Machine& m,bool fast=true) {
    m.bus().write32(0x4c000004,0x52011);m.bus().write32(0x4c000014,3);
    if(fast) m.state().cp15_control|=1u<<30;
    m.configureExecution(true,3);
    m.bus().write32(0,0xe2800001); // ADD r0,r0,#1
    m.bus().write32(4,0xe1a03211); // MOV r3,r1,LSL r2 (two cached cycles)
    m.bus().write32(8,0xe2544001); // SUBS r4,r4,#1
    m.bus().write32(12,0x1afffffb); // BNE 0
    m.bus().write32(16,0xeafffffe); // B 16
    m.state().r[1]=7;m.state().r[2]=2;m.state().r[4]=9;
}
int main() {
    {
        bool equal=true;size_t cases=0;uint64_t generated=0;
        for(bool fast:{false,true}) for(size_t budget:{0u,1u,2u,3u,7u,32u,65u,129u})
        for(uint64_t span:{1ull,2ull,3ull,8ull,20ull,UINT64_MAX}) {
            Machine a,b;setup(a,fast);setup(b,fast);
            X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
            const auto stop=span;
            const auto x=a.execution().run(budget,stop),y=b.execution().run(budget,stop);
            equal=equal && x==y && same(a,b);
            // The subsequent tiny batches expose mismatched fractional time.
            for(unsigned i=0;i<4;++i)
                equal=equal && a.execution().run(3)==b.execution().run(3) && same(a,b);
            generated+=native.stats().generatedSteps;++cases;
        }
        check("Clocked native blocks preserve partial budgets, deadlines and fractional time at FCLK and HCLK",equal&&cases==96&&generated>0);
    }
    {
        Machine a,b;setup(a);setup(b);bool equal=true;size_t cases=0;
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        for(uint32_t operation:{0x02800001u,0x01a03211u,0x0afffffeu})
        for(uint32_t condition=0;condition<15;++condition) for(uint32_t flags=0;flags<16;++flags) {
            for(Machine* m:{&a,&b}) {
                m->bus().write32(0,(condition<<28)|operation);
                m->state().r[15]=0;m->state().cpsr=0xd3|(flags<<28);
            }
            a.execution().run(1);b.execution().run(1);
            equal=equal && same(a,b) && a.cpu().getLastCycles()==b.cpu().getLastCycles();++cases;
        }
        check("Every ARM condition charges one skipped cycle and the correct executed ALU, shift or branch cost",
            equal&&cases==720&&native.stats().generatedSteps>0&&native.stats().generatedCycles>0);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->state().r[1]=0x30010000;m->state().r[2]=3;
            m->bus().write32(0,0xe2800001);m->bus().write32(4,0xe5810000);
            m->bus().write32(8,0xe5913000);m->bus().write32(12,0xe0040293);
            m->bus().write32(16,0xeafffffa);
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        bool equal=true;
        for(unsigned i=0;i<12;++i) {
            a.execution().run(17);b.execution().run(17);
            equal=equal && same(a,b) && a.bus().read32(0x30010000)==b.bus().read32(0x30010000);
        }
        check("Clocked integer blocks and reference memory/multiply exits share one timeline",
            equal&&native.stats().generatedSteps>0&&native.stats().fallbackSteps>0);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->bus().write32(0x5100003c,79);
            m->bus().write32(0x51000008,(1u<<20)|(1u<<21)|(1u<<22));
            m->bus().write32(0x4a000008,~(1u<<14));m->state().cpsr&=~FLAG_I;
            m->bus().write32(0x18,0xeafffffe);
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        bool equal=true;
        for(unsigned i=0;i<16;++i) { a.execution().run(61);b.execution().run(61);equal=equal&&same(a,b); }
        check("Timer expiration and IRQ entry remain exact across native event windows",
            equal&&a.bus().getTimer4Expirations()>0&&(a.state().cpsr&31)==0x12);
    }
    {
        Machine a,b;setup(a);setup(b);std::vector<int16_t> left,right;
        a.bus().setAudioCallback([&](const int16_t* p,size_t n){left.insert(left.end(),p,p+n);});
        b.bus().setAudioCallback([&](const int16_t* p,size_t n){right.insert(right.end(),p,p+n);});
        for(Machine* m:{&a,&b}) {
            for(unsigned i=0;i<16;++i)m->bus().write32(0x30010000+i*4,0x10001000+i);
            m->bus().write32(0x4b000080,0x30010000);m->bus().write32(0x4b000088,0x55000010);
            m->bus().write32(0x4b000090,16|(1u<<20)|(1u<<22));m->bus().write32(0x4b0000a0,2);
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        bool equal=true;
        for(unsigned i=0;i<40 && a.bus().getMmio(0x4b000094);++i) {
            const auto x=a.execution().run(4000),y=b.execution().run(4000);
            equal=equal && x==y && same(a,b);
        }
        check("DMA PCM samples and completion match the clocked reference",equal&&same(a,b)&&!left.empty()&&left==right&&
            a.bus().getMmio(0x4b000094)==b.bus().getMmio(0x4b000094));
    }
    {
        Machine a,b;setup(a,false);setup(b,false);
        for(Machine* m:{&a,&b}) {
            m->state().r[1]=0x4c000014;m->state().r[2]=1;
            m->bus().write32(4,0xe5812000); // Clock-divider MMIO uses reference.
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        a.execution().run(3);b.execution().run(3);
        b.execution().selectInterpreter(b.cpu());a.execution().run(7);b.execution().run(7);
        b.execution().selectEngine(native);a.execution().run(37);b.execution().run(37);
        bool equal=same(a,b);
        // Reusing the engine after changing timing modes must not reuse code
        // compiled for the previous timeline policy.
        a.configureExecution(false,6);b.configureExecution(false,6);b.execution().selectEngine(native);
        a.execution().run(19);b.execution().run(19);equal=equal&&same(a,b);
        a.configureExecution(true,3);b.configureExecution(true,3);b.execution().selectEngine(native);
        a.execution().run(23);b.execution().run(23);
        check("Clock changes, engine handoff and legacy/clocked cache reuse retain exact shared time",equal&&same(a,b));
    }
    {
        bool equal=true;
        for(bool prefetch:{false,true}) {
            Machine a,b;setup(a);setup(b);
            for(Machine* m:{&a,&b}) {
                m->bus().write32(0x30004000,0x30000c02);
                m->bus().write32(0x30000000,0xe2800001);
                m->bus().write32(0x30000004,prefetch?0xea03fffdu:0xe5921000u);
                m->bus().write32(0x3000000c,0xeafffffe);
                m->bus().write32(0x30000010,0xeafffffe);
                m->state().r[2]=0x100000;
                m->state().cp15_ttb=0x30004000;m->state().cp15_dacr=3;m->state().cp15_control|=1;
                m->cpu().enterExecution(true);
            }
            X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
            for(unsigned i=0;i<4;++i) {
                const auto x=a.execution().run(3),y=b.execution().run(3);
                equal=equal&&x==y&&same(a,b);
            }
            equal=equal&&(a.state().cpsr&31)==0x17&&native.stats().generatedSteps>0&&(!prefetch||native.stats().fallbackSteps>0);
        }
        check("Clocked native prefixes preserve data and prefetch abort state and fractional time",equal);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->bus().write32(4,0xee070f90); // WFI remains a precise reference exit.
            m->bus().write32(0x18,0xeafffffe);
            m->bus().write32(0x5100003c,79);
            m->bus().write32(0x51000008,(1u<<20)|(1u<<21));
            m->bus().write32(0x4a000008,~(1u<<14));m->state().cpsr&=~FLAG_I;
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        const auto x=a.execution().run(32),y=b.execution().run(32);
        const bool waiting=same(a,b)&&x==2&&y==2&&a.state().waitingForInterrupt;
        const auto idleA=a.execution().advanceIdle(1000),idleB=b.execution().advanceIdle(1000);
        a.execution().run(3);b.execution().run(3);
        check("WFI idle advancement and IRQ wake retain the native prefix fraction",waiting&&idleA==idleB&&idleA>0&&same(a,b)&&
            (a.state().cpsr&31)==0x12);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->state().cpsr|=FLAG_T;
            m->bus().write16(0,0x2103);m->bus().write16(2,0x3001);
            m->bus().write16(4,0x3901);m->bus().write16(6,0xd1fc);m->bus().write16(8,0xe7fe);
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        a.execution().run(120);b.execution().run(120);
        check("Thumb native integer and taken/skipped branches share the scalar cycle timeline",
            same(a,b)&&a.state().r[0]==3&&native.stats().generatedSteps==120&&native.stats().generatedCycles>120);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->state().r[1]=0;m->state().r[2]=0xe3a0002a;
            m->bus().write32(0,0xe3a00001);m->bus().write32(4,0xe5812000);
            m->bus().write32(8,0xeafffffc);
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        a.execution().run(7);b.execution().run(7);
        check("Clocked active-code stores exit before executing stale native code",
            same(a,b)&&a.state().r[0]==42&&b.bus().read32(0)==0xe3a0002a&&native.stats().memorySteps>0);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->state().r[15]=0x100;m->state().r[1]=0x51000008;
            m->state().r[2]=(1u<<20)|(1u<<22);m->state().cpsr&=~FLAG_I;
            for(unsigned i=0;i<12;++i)m->bus().write32(0x100+i*4,0xe2800001);
            m->bus().write32(0x130,0xe5812000);m->bus().write32(0x134,0xeafffffe);
            m->bus().write32(0x18,0xeafffffe);
            m->bus().write32(0x5100003c,0);m->bus().write32(0x51000008,1u<<21);
            m->bus().write32(0x4a000008,~(1u<<14));
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        const auto x=a.execution().run(50),y=b.execution().run(50);
        check("The native cycle prefix is committed before a guest arms an earlier timer deadline",
            x==y&&same(a,b)&&a.bus().getTimer4Expirations()>0&&(a.state().cpsr&31)==0x12);
    }
    {
        Machine a,b;setup(a);setup(b);
        for(Machine* m:{&a,&b}) {
            m->bus().write32(0,0xe2800001);m->bus().write32(4,0xea0001fd);
            m->bus().write32(0x800,0xe2811002);m->bus().write32(0x804,0xeafffdfd);
        }
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus(),clockedLimits());b.execution().selectEngine(native);
        a.execution().run(128);b.execution().run(128);
        const auto x=a.execution().run(513),y=b.execution().run(513);
        check("Clocked native windows share cumulative cycles across guarded cross-page successors",
            x==y&&same(a,b)&&native.stats().windowLinks>0&&native.stats().fastWindowLinks>0);
    }
    std::cout<<failures<<" failure(s); cached timing subset, not hardware calibration\n";
    return failures?1:0;
}
