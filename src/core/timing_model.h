#pragma once
#include "emulated_time.h"
#include <cstddef>

namespace oceanblast {
enum class TimingModel { LegacyInstructionRatio, RegisterCycleEstimate };
inline constexpr TimingModel timingModel(bool registerClocks) {
    return registerClocks?TimingModel::RegisterCycleEstimate:TimingModel::LegacyInstructionRatio;
}
inline constexpr const char* timingModelName(TimingModel model) {
    return model==TimingModel::RegisterCycleEstimate?"register_cycle_estimate":"legacy_instruction_ratio";
}
inline constexpr const char* effectiveExecutionBackend(bool jitRequested,TimingModel model) {
    return jitRequested&&model==TimingModel::LegacyInstructionRatio?"jit_hybrid":"interpreter";
}
inline constexpr double legacyDefaultInstructionCapMips(size_t ratio) {
    return double(EmulatedTime::ticksPerSecond)/1000000.0*ratio;
}
// Pacing only decides when the host waits; it does not advance CPU/devices.
// A zero instruction cap disables waiting and is handled by the caller.
inline double hostPacingSeconds(TimingModel model,uint64_t instructions,uint64_t ticks,double instructionCapMips) {
    return model==TimingModel::RegisterCycleEstimate?EmulatedTime::seconds(ticks):
        double(instructions)/(instructionCapMips*1000000.0);
}
}
