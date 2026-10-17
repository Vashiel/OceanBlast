#pragma once
#include "cpu/arm920t_state.h"
#include "emulation_clock.h"
#include <cstddef>
#include <optional>

namespace oceanblast {
class Bus;
struct ExecutionTime {
    uint64_t ticks = 0;
    uint64_t idleTicks = 0;
};
// Owned by the scheduler; retained across engine switches.
struct ExecutionTimeline {
    ExecutionTimeline(bool registerClocks,size_t ratio) : registerClocks(registerClocks),ratio(ratio) {}
    bool registerClocks;
    size_t ratio;
    size_t phase = 0;
    EmulationClock clock;
    ExecutionTime elapsed;
};
enum class ExecutionExitReason { Budget, Deadline, CpuEvent, ContextChange, Waiting, Halted, BlockEnd };
struct ExecutionExit {
    ExecutionExitReason reason;
    size_t steps;
    uint64_t ticks;
    std::optional<CpuStepEvent> event;
};

// One dispatch per bounded batch. Implementations own derived execution data,
// never a second authoritative CPU state or an independent machine timeline.
class ExecutionEngine {
public:
    virtual ~ExecutionEngine() = default;
    virtual const ARM920TState& state() const = 0;
    virtual const Bus& memory() const = 0;
    virtual bool canHandoff() const = 0;
    virtual void enter(bool registerClocks) = 0;
    // Step counts include exception/IRQ entry; event metadata is present only
    // for CpuEvent exits. All observable guest state is materialized on return.
    virtual ExecutionExit runToBoundary(ExecutionTimeline&,size_t budget,uint64_t stopTicks) = 0;
    virtual size_t run(ExecutionTimeline& timeline,size_t budget,uint64_t stopTicks) {
        return runToBoundary(timeline,budget,stopTicks).steps;
    }
    virtual void step(ExecutionTimeline& timeline) { runToBoundary(timeline,1,UINT64_MAX); }
};
} // namespace oceanblast
