# Legacy Phase Batches

## Change

The legacy interpreter loop now groups instructions by the next peripheral tick. A phase-zero instruction calls `CPU::step(1)`; the following instructions up to the next tick call `CPU::step(0)`. The phase is retained across budget boundaries and single steps.

Previously the batch loop advanced the phase and tested its wrap after every instruction. The grouped loop keeps that bookkeeping outside the unticked span. Every instruction still performs its normal fetch, fault and interrupt handling. Zero-tick bus calls retain level-triggered interrupt checks. No guest instructions, DMA samples or timer events are skipped or combined.

The single-step path and register-clock mode retain their existing implementations. This is a portable interpreter-loop change, not a decoded block engine or JIT.

## Short comparison

The isolated candidate passes all 87 timing checks, including mixed scalar/batch execution, ratio and phase boundaries, IRQ return, timers and DMA audio sample equivalence.

One normal GCC `-O3 -march=native` unity pair runs the first 1.2 billion Superstar Chefs instructions at ratio 3, without GUI or host pacing. Both executables include the CPU-state, scheduler, MMU and machine-owner changes. No compilation or other emulator replay runs concurrently with the measurement.

| Measure | Reference | Grouped phases |
| --- | ---: | ---: |
| Elapsed seconds | 15.7813 | 14.3339 |
| Process CPU seconds | 15.5625 | 14.1250 |
| Throughput | 76.039 MIPS | 83.718 MIPS |

CPU, SDRAM, framebuffer and snapshot-state hashes match. Throughput is about 10.1% higher in this pair and process CPU time about 9.2% lower. The [raw records](validation/2026-10-15_phase_batches.csv) include executable and cartridge hashes.

The simpler phase bookkeeping is retained. These measurements do not establish a repeatable gain across hosts, ratios or compiler profiles, and do not predict a PGO gameplay improvement. No repeated pairs or gameplay sweeps were added.

## Local PGO build

A native x64 `-O3` unity PGO build was generated from the source at `e962027`, using Superstar Chefs and Rayman 3 training runs of 2.1 billion instructions each at ratio 3. Training and compilation belong to build preparation, not gameplay acceptance. The PGO build consumed substantially more time than expected; future small changes should reuse a suitable installed build for baseline checks and explicitly budget profile generation and both compiler phases rather than automatically repeat this training workload.

One short Superstar Chefs GUI/audio run uses fresh settings and EEPROM, no input, ratio 3 and a 1.2-billion-instruction limit. The title menu was inspected after execution stopped. The [recorded intervals](validation/2026-10-15_phase_batches_pgo_title.csv) hold the 60-MIPS pacing target with zero empty audio queues and zero dropped samples. Final SDRAM and framebuffer hashes match the earlier state/scheduler title run. Listening quality, later gameplay and performance gain over the previous PGO executable were not tested.

The tested executable, SHA-256 `5a83fcb8c93b1fac14c3941ab4d5e89a86243b2774f4cb3c8c37eaf09700429d`, replaces the local `bin/oceanblast.exe`. The previous executable, SHA-256 `97203e3e51264a569bc6b36e39ecf87ab1184e63f6c07a24e11571166462a666`, is preserved in ignored `build/launcher-backup-2026-10-15/oceanblast.exe`. Existing sessions and frontend preferences were not changed. This is a local native Windows build; it is not an ARM build or a new published release package.
