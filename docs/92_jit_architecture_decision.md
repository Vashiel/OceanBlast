# Execution Architecture Decision: Block-Based Dynamic Translation

Status: accepted, 2026-10-17. This decision replaces the alternative-engine experiment gate in the earlier roadmap. Implementation is not yet delivered.

## Direction

Develop a production JIT execution engine with a shared ARM/Thumb frontend and intermediate representation, an x86-64 backend for Windows first, and an AArch64 backend for Android next. Keep the scalar interpreter as the reference, diagnostic mode and fallback for unsupported operations. A separate decoded-block interpreter is not a delivery target.

The product goal is correct real-time emulation with CPU reserve on older, low-end hardware. The earlier measured interpreter gains remain valid within their recorded scenes, but do not establish this goal. More isolated interpreter shortcuts are no longer the main development path. Necessary correctness fixes remain in scope.

Existing CPU state, machine ownership, MMU, peripherals, cartridge handling and frontend remain reusable. Replace the execution strategy incrementally rather than rewriting the machine. Do not change guest timing, skip rendering work or shorten guest functions to obtain JIT performance acceptance. Existing local blit acceleration must remain separately identifiable and excluded from the primary interpreter/JIT equivalence fixture.

## Execution contract

* Both engines use one authoritative architectural state and an explicit entry/exit contract. Materialize registers, flags, PC and relevant execution state at every observable exit. Engine switches must not lose pending exceptions, interrupt eligibility or timing phase.
* Decode ARM and Thumb into bounded basic blocks. Start with straight-line integer blocks. End blocks at control-flow changes, mode transitions, exception boundaries and operations whose semantics require an exit. Set initial code-size and instruction-count limits; avoid unbounded traces.
* Use a small typed IR for integer values, flags, memory operations, control flow and exits. Keep guest semantics separate from host register allocation and machine-code encoding. Initial optimization is deliberately limited; completing correct reusable translation takes precedence over speculative optimization passes.
* Charge the same modeled guest work as the reference engine. Preserve legacy ratio/phase behavior first. Enter generated code only with a known event budget; bound execution so interrupts, timers and DMA become observable at the required instruction boundary. The experimental register-clock model needs its own later equivalence gate. Host audio consumption must not determine guest instruction semantics.
* Begin with existing checked memory/device helpers. Add direct RAM paths after exception and translation behavior is validated. Loads, stores and faults must preserve the established guest semantics, including access size, alignment, permissions and fault attribution. MMIO must retain side effects and event ordering.
* Cache identity includes guest entry PC, instruction-set state, execution context and a mapping-generation mechanism. Track the physical code pages on which a block depends. Writes through aliases, guest code modification and relevant mapping/control changes must invalidate affected translations before stale code can execute. Do not key solely by virtual PC.
* Unsupported instructions exit before execution and run through the reference implementation with precise accounting. Start with explicit fallbacks for complex exceptions and floating-point behavior; expand coverage within the same engine. Measure fallback frequency so a nominal JIT cannot conceal predominantly interpreted execution.
* Use platform executable-memory APIs with separate writing and execution phases, instruction-cache synchronization where required, bounded cache size and a defined cache reset/lifetime. Allocation or backend failure must permit interpreter fallback. Host ABI, stack alignment and preserved registers are backend obligations.

These are implementation requirements, not claims that all current interpreter behavior matches physical hardware. Independent architectural defects need focused fixes and sources; matching an existing bug is not hardware acceptance.

## Delivery sequence

| Milestone | Concrete deliverable | Completion gate |
| --- | --- | --- |
| J0: engine boundary | Shared state ownership, engine selection and precise exits; deterministic short comparison harness | Interpreter behavior is preserved; state handoff, interrupt and timing-phase checks pass. No new game loop or peripheral implementation. |
| J1: translation frontend | ARM/Thumb block decoder, typed IR and block/context metadata | Focused arithmetic, flags, conditional execution and block-ending cases agree with the reference; unsupported cases produce explicit exits. This is the frontend of the final JIT, not a separate interpreter product. |
| J2: Windows x86-64 engine | Executable code cache, ABI-safe block entry and initial integer lowering | Generated blocks execute through the normal machine boundary; short differential checks pass, including flags and engine fallback. Record translated versus fallback work. |
| J3: memory and invalidation | Checked memory lowering, exception exits, physical-page dependencies and mapping invalidation | Targeted fault, alias-write, self-modification, context-change and cache-reset checks pass. Direct RAM access is added only with equivalent checks. |
| J4: complete machine execution | Broader ARM/Thumb coverage, interrupt/event integration and controlled fallback | Bounded boot/title/gameplay fixtures run through the JIT with documented coverage, equivalent state/events and no timing shortcuts. Sampling overhead and compilation cost are reported separately. |
| J5: Windows acceptance | Selectable production engine, safe interpreter fallback and reproducible build manifests | Short matched Wade check first; Chefs follows when Wade is healthy. Correct speed, live audio and sustained CPU reserve are assessed together. No default-engine switch before acceptance. |
| J6: Android ARM64 | AArch64 lowering, executable-memory/cache handling and Android integration | Shared IR/reference checks pass on ARM64, followed by a short real-device scene with pacing, audio and CPU-reserve evidence. Desktop success alone does not complete this milestone. |

Select an encoder/emitter dependency during J0/J2 on the basis of license, supported targets, maintenance burden and integration requirements. The shared execution/IR boundary remains the project contract. Do not introduce a second engine architecture while delivering these milestones without revising this decision explicitly.

## Performance and compatibility acceptance

A JIT is the committed development direction, not a promised speed multiplier. Use the same ROM, EEPROM, input, guest timing, host target, compiler profile and scene when measuring engine differences. Record executable hashes, build flags, translation coverage, cold compilation cost and steady-state CPU cost. Compiler success, code-cache hits or fewer decoder samples do not establish usable speed.

The working engineering target is real-time pacing with approximately 30% CPU-time reserve on the selected representative weak host: at most about 0.70 seconds of emulation-thread CPU time per wall second in the chosen steady-state scene, while maintaining correct guest speed and stable audio. This is a target to validate, not a current result, a universal minimum specification or a substitute for listening and gameplay checks. Report startup spikes and audio deadlines separately; a mean can hide an audible failure. Define the representative hardware and scene before milestone acceptance, without blocking portable engine work on unavailable hardware.

The Windows baseline must use instructions supported by the declared x86-64 target. Native-host builds are separate local configurations. Record compiler, architecture, optimization and PGO provenance in build manifests; profile data must match the configuration. Preserve the last accepted executable before installation.

Android's first JIT backend targets 64-bit ARM. A 32-bit ARM-only device cannot execute that backend and would require a separate AArch32 backend or interpreter fallback. Support and performance on those devices remain uncommitted; AArch64 work must not be advertised as 32-bit ARM support.

## Work boundaries

Stop adding discretionary interpreter fast paths, cartridge-specific PC hooks or guest-time compression as the route to this goal. Keep the existing reference reproducible. Frontend cleanup and unrelated cartridge expansion do not take precedence over J0-J5.

Deliver independently reviewable slices with an expected output, total time budget and smallest relevant check. Use the roadmap's normal 45-60-minute structural-work allowance unless a different budget is agreed. Several milestones may progress within an authorized budget; incomplete work is reported as incomplete, not accepted. Ask before extending the budget. A failed short scene ends that validation run; do not broaden it into repeated long gameplay tests.

Correctness checks are part of implementing the committed engine. An early speed result informs engineering within this architecture; it is not another round of unrelated optimization candidates. Change this architecture decision only for a concrete technical blocker or an explicit project decision.
