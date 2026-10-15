# Immediate-ALU Flag Extension Experiment

## Candidate

The [active-scene profile](65_active_scene_host_profile.md) leaves ARM dispatch and general simple-ALU execution as substantial sampled costs. The retained direct immediate path currently accepts nine unconditional operations with S clear. This isolated candidate extends those same operations to S set: AND, EOR, SUB, RSB, ADD, ORR, MOV, BIC and MVN. It computes NZCV after the result when required.

Arithmetic derives carry and overflow from the original operands. Logical operations preserve V and use the rotated immediate's high bit for C, or preserve incoming C when the rotation is zero. Other CPSR bits remain unchanged. Conditional instructions, PC operands, carry-dependent arithmetic, test/compare encodings, register operands and debugging continue through the existing decoder. Interrupt and fetch checks still precede direct execution. There is no code cache or additional persistent state.

The [archived patch](validation/2026-10-15_immediate_flags.patch) applies to revision `757eaa0` with `git apply --unidiff-zero --ignore-space-change`. It includes the isolated implementation and expanded reference tests. The extension was not adopted in production.

## Short checks

All 58 CPU checks and 87 timing checks pass. The immediate-operation matrix now compares both S states against the general interpreter across all 16 incoming NZCV combinations, nine supported opcodes, all 16 rotations, immediate values 0/1/127/128/255 and five boundary left operands. Registers, full CPSR and cycle counts must match. The existing conditional matrix also covers both S states; random and PC-operand checks remain enabled. Passing reference comparisons establishes agreement with the existing interpreter, not complete hardware fidelity.

## One active-scene pair

Reference and candidate use normal GCC `-O3 -march=native` unity builds, not PGO. Both replay Superstar Chefs with the same active input fixture, blank EEPROM, isolated settings and ratio 3. GUI/audio are enabled, pacing is disabled and execution ends at four billion instructions. Only the three-to-four-billion-step section defines the scene-throughput result. No compilation or other emulator replay ran concurrently. There was one pair, without repetitions.

Both stopped windows show the expected level, 760 points and four lives. Final CPU, SDRAM and framebuffer hashes match each other and the previous active reference. The [paired results](validation/2026-10-15_immediate_flags.csv), [reference intervals](validation/2026-10-15_immediate_flags_baseline.csv) and [candidate intervals](validation/2026-10-15_immediate_flags_candidate.csv) preserve the evidence and executable hashes.

| Observation | Reference | Candidate |
| --- | ---: | ---: |
| Active-section throughput | 81.4724 MIPS | 78.9322 MIPS |
| Active-section elapsed time | 12,274.1 ms | 12,669.1 ms |
| Section CPU/bus elapsed time | 12,198.4 ms | 12,556.0 ms |
| Section GUI time excluding wait | 60.570 ms | 93.233 ms |
| Whole-run interval throughput | 81.8867 MIPS | 81.0470 MIPS |

The candidate's active-section throughput is 3.12% lower in this pair. This is sufficient to reject the extra path; it does not establish a repeatable slowdown or identify the generated-code cause. The profile did not measure eligible guest-opcode frequency, and the result does not distinguish execution overhead from compiler layout effects.

Whole-process CPU/elapsed times include manual end inspection and are excluded from the performance decision. Unpaced runs discard 729,350 and 705,024 audio samples respectively, with zero empty queues. These throughput counters do not establish normal-paced audio quality or synchronization. The stopped-screen checks do not establish continuous motion quality.

## Decision

Reject the extension. Keep the existing immediate no-flags shortcut and general flag-setting interpreter unchanged. No extra pair, PGO build or installation was performed. Several small extensions have now failed their acceptance gates; the next proposal should account for those rejected paths and identify the actual eligible instruction mix before adding more special cases. Sampling a large function alone does not establish that a particular subset warrants a new shortcut.
