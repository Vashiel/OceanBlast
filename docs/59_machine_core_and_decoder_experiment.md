# Machine Core and Decoder Experiment

## Changes on 2026-10-15

The execution refactor now has separate CPU state, execution scheduling and address translation components. The application and the bounded runtime probe share a `Machine` owner. These changes establish the core structure for further execution work; they do not establish faster gameplay on weak hardware.

| Component | Responsibility | Boundary |
| --- | --- | --- |
| `cpu/arm920t_state.h` | Modeled registers, banked registers, CP15 and FPA state, halt/wait state | No bus, window or interpreter dependency. Read-only inspection through `ARM920T::getState()`. This is not a portable save format or a claim of complete ARM920T state coverage. |
| `core/execution_batch.h` | Interpreter single steps and bounded batches | Instruction semantics remain in `ARM920T`. |
| `core/execution_scheduler.h` | Legacy phase, register-clock fractions, elapsed time and idle advancement | Register-clock batches return at the next known device deadline or caller deadline, between instructions. Host pacing and presentation remain outside. |
| `memory/arm920t_mmu.h` | MMU state, table walking and TLB | Uses a physical RAM view; device accesses and instruction-address caching remain in `Bus`. |
| `core/machine.h` | Stable ownership of bus, CPU and execution scheduler | Used by the normal application and `tools/runtime_probe.cpp`, without requiring display or host audio services. Copying and moving are disabled because internal references must remain valid. |

`Machine::configureExecution()` starts a fresh timeline and preserves guest registers and memory. Current callers configure it before execution. Resetting an entire running machine, loading save states and switching execution engines are separate tasks.

The scheduler retains the existing legacy cartridge ratios. Register-clock mode remains experimental. It checks devices after each instruction, and a deadline can be crossed by the final instruction's modeled cycles. This is not cycle-exact scheduling within an instruction or original-hardware speed calibration.

## Translation corrections

The MMU extraction first preserved the existing implementation: all observations from `tools/probe_tlb_permissions.cpp` matched before and after the move. A separate change then corrected two cached/cold inconsistencies:

- A small-page TLB entry now retains all four 1-KB AP fields. Visiting an accessible subregion no longer grants its rights to an adjacent restricted subregion.
- A cached section denial now returns a section permission fault, matching the cold section lookup.

Cold permission faults still leave colliding entries intact. Existing synthetic mappings and coarse-table descriptor type 3 behavior were preserved. Domain-control, system/ROM protection and unsupported descriptor semantics remain architectural review items; this change does not validate those policies against hardware. The [earlier TLB report](57_tlb_permission_tag_candidates.md) records their history.

## Targeted checks

The developer executable and affected tools build with the normal GCC `-O3 -march=native` configuration. The installed PGO launcher was not replaced.

| Suite | Passing checks | Relevant coverage |
| --- | ---: | --- |
| CPU regression | 57 | Existing instruction checks plus banked-register state inspection and independent state copies |
| Emulation timing | 87 | Mixed single/batch execution, legacy deadlines, timer-event boundaries, idle deadlines and IRQ wakeup |
| UART interrupt | 12 | Existing level-triggered interrupt behavior |
| Standalone MMU | 10 | Translation, invalidation, collisions, all 256 small-page AP combinations across access modes, and matching cached/cold section faults |
| Machine | 4 | Bounded execution, independent machines, configuration lifetime and invalid ratio rejection |

The bounded runtime probe also completes a 200,000-tick / ratio-3 startup check and writes its expected CSV record. This only establishes that the shared command-line machine path executes.

## Short GUI checks

Host: Intel Core i7-1185G7. Cartridge: Superstar Chefs, SHA-256 `465ddcd7f9722d73b4fd748d3b8d822bc99c06ea91c301bd768f583a1d52b2ff`. Runs use separate settings and EEPROM files, ratio 3 and no gameplay input.

After CPU state, scheduler and MMU changes, a 1.2-billion-step GUI/audio run completes with zero empty audio queues and dropped samples in the [recorded intervals](validation/2026-10-15_state_scheduler_title.csv). Final SDRAM and framebuffer hashes match the earlier execution-boundary title run. Executable SHA-256: `70930db712c4cfd2bdc115c6edfd5f44cca1127a86f4e181f7ff7a6b1e50be11`.

After the `Machine` ownership change, the title menu was inspected in a visible window. That run recorded seven empty-queue observations near the title transition, with no dropped samples. The run was closed without entering gameplay; its [metrics](validation/2026-10-15_machine_title.csv) are retained rather than classified as a clean audio pass. Window activation and capture occurred during this run, but their causal contribution was not established.

One short controlled comparison then isolated the ownership change: 750 million steps per executable, the same blank EEPROM, empty input fixture, fresh settings, GUI/audio and normal pacing. Both sustain 60.00 MIPS with zero empty queues and zero dropped samples. Final CPU, SDRAM and framebuffer hashes match. The [paired records](validation/2026-10-15_machine_comparison.csv) include executable and fixture hashes. Candidate SHA-256: `c9ad04abaa47996d4eab0c1bb15b4900742e59c1b13be5cbbc6793b806e72331`.

This comparison supports retaining the ownership refactor. It does not resolve every host-interaction audio stall, establish listening quality or prove general gameplay equivalence. No additional scenes or repeated benchmark pairs were run.

## Isolated decoder prototype

The first P4 experiment lazily caches decoded operands in blocks of sixteen ARM instruction slots. It supports unconditional, immediate, non-flag-setting logical/add/subtract operations with ordinary register operands; other instructions retain the existing interpreter path. Every instruction is still fetched and permission-checked. A cache hit requires the exact fetched word, so changed code, remapping and cache-index collisions cannot reuse a different instruction's operands. This is a limited decoder cache, not a JIT or a complete block execution engine.

The isolated candidate passes 57 CPU checks, 87 timing checks and a focused cache test covering reuse, changed words, collisions, rotations and fallback guards. It was compared against the same normal compiler configuration after the translation fixes, so permission semantics match between the pair. No compilation or other emulator replay ran concurrently with the measurement.

One unpaced, headless pair covers the first 1.2 billion Superstar Chefs steps at ratio 3 with no scripted input:

| Measure | Reference | Decoder candidate |
| --- | ---: | ---: |
| Elapsed seconds | 15.1233 | 16.8423 |
| Process CPU seconds | 14.8906 | 16.6562 |
| Throughput | 79.348 MIPS | 71.249 MIPS |

CPU, SDRAM, framebuffer and snapshot-state hashes match. Throughput is about 10.2% lower in this pair. The experiment supplies no reason to retain the cache, so it was rejected without further scenes or repeated pairs. One pair does not prove a general regression on all hosts or rule out other block/JIT designs.

The [results](validation/2026-10-15_decoded_blocks.csv) and [candidate patch](validation/2026-10-15_decoded_blocks.patch) preserve the experiment. Apply the zero-context patch with `git apply --unidiff-zero` at revision `629c8bb` in a separate checkout; it is not part of the production interpreter. Local binaries and guest dumps remain under ignored `build/` directories.

## Next steps

The next performance task should target an execution path that can remove repeated dispatch work, rather than repeat the rejected operand-cache design. A real block engine also needs explicit fallback, fault, interrupt and modified-code boundaries. Select the intended weak CPU before committing to a host-specific JIT backend. Broad gameplay and release validation remain separate, budgeted work.
