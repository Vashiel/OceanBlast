#pragma once
#include "cpu/arm920t_state.h"
#include "memory/bus.h"
#include <fstream>
#include <string>

namespace oceanblast {
// Explicit little-endian fields exclude host padding and engine-derived caches.
// Checkpoints contain cartridge-derived RAM and belong outside source control.
inline bool writeExecutionCheckpoint(const std::string& prefix,const ARM920TState& state,const Bus& bus) {
    std::ofstream cpu(prefix+".cpu.bin",std::ios::binary),ram(prefix+".ram.bin",std::ios::binary);
    if(!cpu || !ram) return false;
    auto word=[&](uint32_t value) {
        const char bytes[]={char(value),char(value>>8),char(value>>16),char(value>>24)};
        cpu.write(bytes,4);
    };
    word(1); // Representation version.
    for(auto value:state.r) word(value);
    for(auto value:{state.cpsr,state.spsr,state.r13_usr,state.r14_usr,
        state.r13_svc,state.r14_svc,state.spsr_svc,state.r13_irq,state.r14_irq,state.spsr_irq,
        state.r13_abt,state.r14_abt,state.spsr_abt,state.r13_und,state.r14_und,state.spsr_und,
        state.cp15_control,state.cp15_ttb,state.cp15_dacr,state.cp15_fsr,state.cp15_far,state.fpaFpsr}) word(value);
    for(const auto& value:state.fpa) { word(value.type); word(value.w0); word(value.w1); }
    word(state.halted); word(state.waitingForInterrupt);
    ram.write(reinterpret_cast<const char*>(bus.getSdramPtr()),ADDR_SDRAM_SIZE);
    return bool(cpu) && bool(ram);
}
} // namespace oceanblast
