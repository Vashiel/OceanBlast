# Fixed-Ratio Legacy Batch Experiment

## Candidate and budget

The current profile attributes 8.9% of in-module samples to legacy batch execution. This is a hotspot estimate, not the amount removable by unrolling. One isolated candidate specializes ratios 3 and 6: finish an incoming partial phase, execute complete groups with one timed instruction and a compile-time sequence of zero-time instructions, then finish any short tail with scalar stepping. Other ratios retain the existing generic loop.

Every instruction still calls `ARM920T::step`. IRQ, fetch, fault, wait and peripheral behavior remain in that instruction boundary. Halt checks remain between consecutive instructions, and an interrupted group records its partial phase. There is no decoded-code cache or new persistent CPU state.

The source copy starts at `2880b0d`. The [archived implementation and test patch](validation/2026-10-16_legacy_unroll.patch) applies with `git apply --unidiff-zero --ignore-space-change`. Production sources were not edited. Total task allowance was 30 minutes, including build and validation; the experiment ended early after its failed comparison.

## Focused checks

All 58 CPU checks and 90 timing checks pass. The additional phase matrix compares scalar and batch execution for ratios 1, 2, 3, 6 and 16, every valid initial phase, and successive budgets of 0, 1, 2, ratio-1, ratio, ratio+1, 17 and 4,096 instructions. It checks retired count, phase, elapsed ticks, all registers, CPSR, last cycle count, timer state and pending source state. The existing unmasked IRQ entry/exception-return scenario now also exercises ratios 3 and 6. These checks establish agreement with the current scalar interpreter, not complete hardware fidelity.

## Wade-first comparison

One matching normal GCC `-O3 -march=native` unity pair runs Wade Hixton's Counter Punch with GUI/audio, ratio 6, disabled host pacing, `tests/wade_ratio6_fight.txt`, a zero-filled 2,048-byte EEPROM and isolated settings. The replay ends at 8.5 billion instructions; the fixed measurement section spans 7 to 8.5 billion. No compilation or other emulator replay ran concurrently. End-screen inspection occurs after the measured execution stops.

Both stopped windows show the Rocco fight with round counter 77. Final CPU, SDRAM and framebuffer hashes match exactly. Active framebuffer SHA-256 is `3c2ddd71d41804001c6f240373bc4976cb9f56c7894ce114f6786877afbf6ea9`. The fixture reaches the fight through scripted confirmations; it does not inject further player actions inside the measured fight interval. This is bounded fight-throughput evidence, not a broad controller/gameplay acceptance test.

The [paired record](validation/2026-10-16_legacy_unroll_wade.csv), [reference intervals](validation/2026-10-16_legacy_unroll_wade_baseline.csv) and [candidate intervals](validation/2026-10-16_legacy_unroll_wade_candidate.csv) retain executable, cartridge, input and initial EEPROM hashes.

| Observation | Reference | Candidate |
| --- | ---: | ---: |
| Fight-section throughput | 62.0486 MIPS | 49.0984 MIPS |
| Fight-section elapsed time | 24,174.6 ms | 30,550.9 ms |
| Section CPU/bus elapsed time | 23,949.3 ms | 29,086.7 ms |
| Section GUI time excluding wait | 184.877 ms | 1,417.060 ms |
| Whole-run interval throughput | 61.9222 MIPS | 46.9983 MIPS |

Fight-section throughput is 20.87% lower and CPU/bus elapsed time 21.45% higher in this pair. GUI cost also increases substantially, so the result must not be attributed entirely to generated loop code. Host frequency and scheduling were not controlled. This single pair supplies no benefit and is sufficient to reject the candidate; it does not establish a repeatable regression or its precise cause.

Whole-process CPU and wall times include manual end inspection and are excluded from the performance decision. Both runs record 1,474 empty-queue events and zero dropped samples. Neither reaches the configured 120-MIPS pacing target; this unpaced comparison is not audio-quality, synchronization or full-speed acceptance. Only the stopped fight images were visually inspected during this task.

## Decision

Reject both fixed-ratio specializations. Keep the existing generic phase-group batch loop and the preceding zero-tick bypass unchanged. No repeated pair or Superstar Chefs follow-up was performed after the non-improving Wade result. No PGO build, launcher replacement or release was made. The ratio-3 specialization has correctness coverage but no cartridge performance claim.

Further work should account for host timing variation and inspect generated code for the exact build before proposing another loop specialization. The result does not justify a larger rewrite or treating all sampled batch/step time as removable overhead.
