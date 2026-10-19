#include "core/machine.h"
#include "core/timing_model.h"
#include "cpu/x64_execution_engine.h"
#include <fstream>
#include <iostream>
using namespace oceanblast;
static int failures=0;
static void check(const char* text,bool ok) {std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';failures+=!ok;}
static void setup(Machine& machine,bool clocked,size_t ratio,bool arithmetic) {
    auto& bus=machine.bus();
    bus.write32(0x4c000004,0x52011);bus.write32(0x4c000014,3);
    bus.write32(0,arithmetic?0xe2800001:0xeafffffe); // ADD; B or B alone.
    if(arithmetic)bus.write32(4,0xeafffffd);
    machine.state().cp15_control|=1u<<30;
    machine.configureExecution(clocked,ratio);
    // 45 MHz PCLK / 2, count 22,500 -> 1 ms -> 20,000 internal ticks.
    bus.write32(0x5100003c,22499);
    bus.write32(0x51000008,(1u<<20)|(1u<<21)|(1u<<22));
}
int main() {
    std::ofstream csv("timing_contract.csv");
    csv<<"model,workload,ratio,steps,guest_ticks,timer_expirations\n";
    {
        Machine a,b;setup(a,true,3,true);setup(b,true,6,false);
        const auto addSteps=a.execution().run(90000),branchSteps=b.execution().run(60000);
        check("Equal estimated CPU time advances the same timer despite different step counts and legacy ratios",
            addSteps==90000&&branchSteps==60000&&a.execution().time().ticks==20000&&b.execution().time().ticks==20000&&
            a.bus().getTimer4Expirations()==1&&b.bus().getTimer4Expirations()==1&&a.state().r[0]==45000);
        csv<<"register_cycle_estimate,add_branch,3,"<<addSteps<<','<<a.execution().time().ticks<<','<<a.bus().getTimer4Expirations()<<'\n';
        csv<<"register_cycle_estimate,branch,6,"<<branchSteps<<','<<b.execution().time().ticks<<','<<b.bus().getTimer4Expirations()<<'\n';
    }
    {
        Machine a,b;setup(a,false,3,false);setup(b,false,6,false);
        a.execution().run(1200);b.execution().run(1200);
        check("Legacy presets change device time for identical guest work and cannot be treated as host-only limits",
            a.state().r[15]==b.state().r[15]&&a.execution().time().ticks==400&&b.execution().time().ticks==200);
        csv<<"legacy_instruction_ratio,branch,3,1200,"<<a.execution().time().ticks<<",0\n";
        csv<<"legacy_instruction_ratio,branch,6,1200,"<<b.execution().time().ticks<<",0\n";
    }
    {
        Machine a,b;setup(a,true,3,true);setup(b,true,6,true);
        X64ExecutionEngine native(b.state(),b.cpu(),b.bus());b.execution().selectEngine(native);
        const auto x=a.execution().run(1200),y=b.execution().run(1200);
        check("Register-cycle execution ignores legacy allocation and truthfully reports the JIT reference fallback",
            x==y&&a.execution().time().ticks==b.execution().time().ticks&&a.state().r[0]==b.state().r[0]&&
            native.stats().generatedSteps==0&&native.stats().fallbackSteps==1200&&
            std::string(effectiveExecutionBackend(true,TimingModel::RegisterCycleEstimate))=="interpreter");
    }
    {
        const auto legacy=TimingModel::LegacyInstructionRatio,clocked=TimingModel::RegisterCycleEstimate;
        check("Default legacy caps match configured model time while an explicit host cap stays a separate pacing choice",
            legacyDefaultInstructionCapMips(3)==60&&legacyDefaultInstructionCapMips(6)==120&&
            hostPacingSeconds(legacy,60000000,20000000,60)==1&&hostPacingSeconds(legacy,60000000,20000000,30)==2&&
            hostPacingSeconds(clocked,60000000,20000000,30)==1&&hostPacingSeconds(clocked,60000000,20000000,120)==1);
    }
    std::cout<<failures<<" failure(s); internal timing characterization, not original-hardware calibration\n";
    return failures?1:0;
}
