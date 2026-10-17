#pragma once
#include "interpreter_engine.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace oceanblast {

// Owns the machine timeline. Engine selection dispatches once per batch;
// presentation and host pacing remain outside this scheduler.
class ExecutionScheduler {
public:
    ExecutionScheduler(ARM920T& cpu,Bus& bus,bool registerClocks,size_t ratio)
        : bus(bus),reference(cpu,bus),active(&reference),timeline{registerClocks,ratio} {
        if (!ratio || ratio>16) throw std::invalid_argument("CPU ratio must be between 1 and 16");
    }
    ExecutionScheduler(const ExecutionScheduler&) = delete;
    ExecutionScheduler& operator=(const ExecutionScheduler&) = delete;
    ExecutionScheduler(ExecutionScheduler&&) = delete;
    ExecutionScheduler& operator=(ExecutionScheduler&&) = delete;

    // The caller keeps an external engine alive for the duration of selection.
    void selectEngine(ExecutionEngine& next) {
        if (&active->state()!=&next.state() || &next.memory()!=&bus)
            throw std::invalid_argument("Execution state and bus must match");
        if (!active->canHandoff() || !next.canHandoff())
            throw std::logic_error("Deferred execution must finish before engine handoff");
        next.enter(timeline.registerClocks);
        active=&next;
    }
    void selectInterpreter(ARM920T& next) {
        auto candidate=std::make_unique<InterpreterEngine>(next,bus);
        selectEngine(*candidate);
        selectedInterpreter=std::move(candidate);
    }
    const ExecutionTime& time() const { return timeline.elapsed; }
    ExecutionExit runToBoundary(size_t budget,uint64_t stopTicks=UINT64_MAX) {
        return active->runToBoundary(timeline,budget,stopTicks);
    }
    size_t run(size_t budget,uint64_t stopTicks=UINT64_MAX) {
        return active->run(timeline,budget,stopTicks);
    }
    void step() { active->step(timeline); }
    uint64_t advanceIdle(uint64_t stopTicks) {
        const auto& state=active->state();
        if (!timeline.registerClocks || state.halted || !state.waitingForInterrupt ||
            bus.hasPendingIrq() || timeline.elapsed.ticks>=stopTicks) return 0;
        const uint64_t amount=std::min(bus.ticksUntilEvent(),stopTicks-timeline.elapsed.ticks);
        bus.tick(amount);
        timeline.clock.advanceIdle(amount);
        timeline.elapsed.ticks+=amount;
        timeline.elapsed.idleTicks+=amount;
        return amount;
    }
private:
    Bus& bus;
    InterpreterEngine reference;
    std::unique_ptr<InterpreterEngine> selectedInterpreter;
    ExecutionEngine* active;
    ExecutionTimeline timeline;
};
} // namespace oceanblast
