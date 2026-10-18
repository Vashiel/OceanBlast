#pragma once
#include "core/interpreter_engine.h"
#include <memory>

namespace oceanblast {
struct NativeExecutionStats {
    uint64_t generatedSteps=0,fallbackSteps=0,compilations=0,compilationFailures=0;
    uint64_t cacheHits=0,cacheEvictions=0,cacheResets=0;
    uint64_t memorySteps=0;
    uint64_t fastCacheHits=0,sourceValidations=0;
    uint64_t fallbackCacheHits=0,fallbackEntries=0;
    uint64_t directCacheHits=0;
    uint64_t peakCacheEntries=0,peakCacheBytes=0;
    uint64_t mappingRevalidations=0;
    uint64_t coldBlockSteps=0,coldCacheHits=0,promotions=0;
    uint64_t internalBackedges=0;
};
struct NativeCacheLimits {
    size_t blocks=65536;
    size_t bytes=8*1024*1024;
    size_t compileThreshold=1; // Reference fixtures can request immediate code generation.
};
// Checked integration with bounded, generation-validated code reuse.
// Whole-machine pacing, audio and performance acceptance remain separate gates.
class X64ExecutionEngine final : public ExecutionEngine {
public:
    X64ExecutionEngine(ARM920TState& state,ARM920T& reference,Bus& bus,NativeCacheLimits limits={});
    ~X64ExecutionEngine() override;
    const ARM920TState& state() const override;
    const Bus& memory() const override;
    bool canHandoff() const override;
    void enter(bool registerClocks) override;
    ExecutionExit runToBoundary(ExecutionTimeline&,size_t,uint64_t) override;
    size_t run(ExecutionTimeline&,size_t,uint64_t) override;
    const NativeExecutionStats& stats() const;
    // Call only while this machine is not executing a batch.
    void resetCodeCache();
private:
    ExecutionExit runUntilDeadline(ExecutionTimeline&,size_t,uint64_t,uint64_t);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace oceanblast
