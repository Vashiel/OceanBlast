#pragma once
#include "execution_scheduler.h"
#include <optional>
#include <stdexcept>

namespace oceanblast {

// One emulated machine, without a window, audio device or host pacing policy.
// References inside the CPU and scheduler require a stable owner address.
class Machine {
public:
    Machine() : processor(memory,architecturalState), scheduler(std::in_place,processor,memory,false,1) {
        processor.reset();
    }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;

    Bus& bus() { return memory; }
    // Execution engines may update this only while no engine is running.
    ARM920TState& state() { return architecturalState; }
    const ARM920TState& state() const { return architecturalState; }
    ARM920T& cpu() { return processor; }
    ExecutionScheduler& execution() { return *scheduler; }
    const Bus& bus() const { return memory; }
    const ARM920T& cpu() const { return processor; }
    const ExecutionScheduler& execution() const { return *scheduler; }

    // Starts a fresh execution timeline without resetting guest memory or registers.
    void configureExecution(bool registerClocks,size_t ratio) {
        if (!ratio || ratio>16) throw std::invalid_argument("CPU ratio must be between 1 and 16");
        processor.setCycleTiming(registerClocks);
        scheduler.emplace(processor,memory,registerClocks,ratio);
    }

private:
    Bus memory;
    ARM920TState architecturalState;
    ARM920T processor;
    std::optional<ExecutionScheduler> scheduler;
};

} // namespace oceanblast
