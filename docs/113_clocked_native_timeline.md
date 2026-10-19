# Shared clocked-native execution timeline

Date: 2026-10-19. Implements an initial T3 slice of the [timing plan](110_common_timing_model_and_calibration.md), following the [Chefs battery counter-check](112_chefs_battery_countercheck.md). The task budget is 45 minutes, including implementation, build and short checks.

## Delivered behavior

The developer JIT can now execute ARM/Thumb integer and branch blocks plus checked ARM single-memory operations using the same cycle-to-device-time conversion as the interpreter. Previously, requesting JIT with register timing always delegated execution to the interpreter. The new path is explicitly enabled by `--engine jit --timing auto --jit-clocked`; without `--jit-clocked`, the existing reference backend remains selected. Legacy cartridge presets and installed/manual-test executables are preserved.

`ARM920TCycles` shares the existing cached data-operation, register-shift and branch costs between scalar execution and native code generation. Native single-memory operations retain the existing one-cycle base estimate. The [ARM920T TRM, chapter 12](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb) describes cached baseline costs and separately requires system-dependent external-access penalties and dependency interlocks. This implementation does not fill those unresolved costs with guessed constants or claim full hardware timing calibration.

## Execution boundary

Native code accumulates executed cycles, charging skipped ARM conditions separately. Bounded internal loops and guarded successor blocks carry that cycle count through one shared run frame. Both timing modes use the same successor lifetime, mapping and code-version checks. Switching timing modes clears compiled entries before they can be reused under different accounting.

The entry budget conservatively fits the maximum cost of supported instructions before the next known event. The interpreter handles the final deadline-crossing instruction. Checked accesses commit preceding cycles before MMIO or faults; an access that changes a clock is charged at the frequency observed before that instruction. Device accesses, faults and active-code writes exit before further native work. Partial cycles remain in the scheduler-owned clock across native/reference switches, idle advancement and clock changes. Multiply and multiple-transfer instructions retain reference execution in this initial clocked subset.

Startup/snapshot/CSV reporting identifies `jit_clocked_hybrid` only when the developer flag is active. `generated_cycles` is appended to the engine CSV. The hardware-calibration field remains `unverified`.

## Short acceptance

All **13 CTest suites pass in 29.30 seconds** on the final build. The new clocked suite covers 96 budget/deadline/frequency combinations, 720 ARM condition/cost cases, mixed native/reference execution, Timer IRQs, matching DMA PCM, clock writes, engine handoff, data/prefetch aborts, WFI/idle wake, Thumb branches, active-code writes, guest-armed earlier timers and guarded cross-page successors. Existing native semantics, memory/invalidation and CPU/timer/audio suites also pass. A separate contract check preserves the default interpreter backend unless clocked native execution is explicitly enabled.

A short Chefs startup check executes the same 300 million instructions as the archived reference. Canonical CPU and complete SDRAM hashes match. Both final snapshots report **49421520 internal ticks and 0 idle ticks**. The candidate executes 288,940,380 instructions natively (96.31%) and records 382,601,710 generated cycles. Native instruction coverage is not a speedup measurement.

| Headless register-timing check | Reference interpreter | Opt-in native hybrid |
| --- | ---: | ---: |
| Process CPU time | 5.953125 s | 7.250000 s |
| Wall time | 6.5227 s | 7.8221 s |

The candidate is slower in this recorded check. The reference was reused from the same bounded work block; host variation and launch/translation costs limit attribution. This accepts a shared timing capability and exact state agreement, not performance readiness. No additional live-audio or game sweep follows this result. The existing [battery audio failure](112_chefs_battery_countercheck.md) remains open.

The [manifest](validation/2026-10-19_clocked_native.json), [final checks](validation/2026-10-19_clocked_native_ctest.txt) and [engine counters](validation/2026-10-19_clocked_native_stats.csv) record exact source/build identities and limits. ROM, EEPROM, RAM and executable artifacts remain local and ignored. Final candidate SHA-256: `59e754c0321bbc535565cd46fd378e6d4e9eccb984df820c38858c5f556949a3`.

## Remaining gates

T1 still requires dependency/interlock, memory/cache, fault, accelerated-FPA11 and board/reset review. T2 still requires actual original-video event observations. T3 now has a tested initial native subset; completing the reviewed timing contract and broader native coverage remains open. T4 preset migration and real-time/audio/reserve acceptance remain open. This is a developer milestone, not a replacement playable EXE or a new original-speed claim.
