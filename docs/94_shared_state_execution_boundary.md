# J0: Shared CPU State and Reference Execution Boundary

Status: first J0 slice delivered, 2026-10-17. Native engine dispatch and complete J0 acceptance remain open.

## Delivered

`Machine` now owns `ARM920TState`. The interpreter binds to that state rather than owning a separate machine state. Standalone interpreter instances retain an owned state for existing instruction tests and diagnostic tools. Constructing a view of shared state does not reset registers or change the active bus context. Selecting that view explicitly synchronizes CP15 mapping and privilege state and discards derived translation/fetch caches.

`ExecutionScheduler::selectInterpreter` switches between interpreter views of the same state and bus. The scheduler retains the legacy ratio phase, clock-conversion fraction, elapsed time and idle time. A view must outlive its selection. A mismatched state or bus is rejected. This is a checked state-handoff foundation, not a selectable native engine.

`runToBoundary` is a scalar reference entry point with explicit results for step budget, device/external deadline, CPU event, context change, wait and halt. CPU events identify interrupt entry, prefetch/data abort, undefined instruction, supervisor call and the pre-existing legacy acceleration. The returned step count includes interrupt/exception entry calls; it is not a retired-instruction count. Event metadata is absent for exits that did not execute a CPU event. Context exits include ARM/Thumb, privilege/interrupt-mask and CP15 mapping/control changes. A newly programmed earlier device event shortens the current deadline.

The existing `run` and `step` timing policies remain available. The reference boundary is not substituted into the production game loop in this slice. Existing title-blit acceleration is preserved separately and is disabled in the ROM-free fixtures. Handoff during its deferred stall is rejected; reset clears that deferred state.

## Validation

The ROM-free `execution_boundary` test covers ratio phases 1/3/6/16, fractional clock accumulation, state binding without reset, bank restoration, FPA/fault/wait-state preservation, cross-machine rejection, software/undefined exceptions, MMU entry, prefetch/data faults, ARM/Thumb transition, an earlier newly armed timer, WFI and pending IRQ handoff. Existing machine, CPU and emulation-timing regressions pass. A conservative x86-64 developer executable builds successfully.

This establishes the tested structural behavior. It does not establish complete ARM920T hardware accuracy, native-code performance, correct live audio or weak-device acceptance. The installed launcher executable remains unchanged.

## Next boundary work

Complete the backend-neutral batch entry/exit and timing interface when connecting the block frontend and native backend. Backend selection must occur once per bounded batch, rather than introducing a virtual call for every guest instruction. Generated blocks must charge the same work and preserve the scheduler-owned phase/fraction on fallback. An interpreter-view switch alone does not complete the J0 engine-selection gate.

The shared ARM/Thumb frontend can now describe bounded blocks against this machine-owned state. J1 frontend work remains separate from native-code generation and must not be presented as a speedup.
