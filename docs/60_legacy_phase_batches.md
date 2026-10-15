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
