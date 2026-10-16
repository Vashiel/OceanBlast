# Register-ALU No-Flags Shortcut

## Candidate

The [active-scene guest instruction mix](67_active_scene_instruction_mix.md) shows that unconditional/passing ARM register-ALU instructions with `S` clear (`REG_ALU_NOFLAGS`) form the largest remaining slice at `29.15%` of active gameplay steps (`14.94%` unshifted and `14.16%` immediate-shifted, versus `0.05%` register-shifted). An earlier [register-operand attempt](61_arm_dispatch_measurement.md) handled only unshifted operands inside a combined immediate/register helper and lost throughput in a startup comparison.

This candidate adds a separate `executeRegisterNoFlags` helper in [`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp) for unconditional class-0 data-processing instructions with `S` clear and bit 4 clear (`(instruction & 0xfe100010u) == 0xe0000000u`). It accepts the same nine non-carry opcodes as `executeImmediateNoFlags` (`AND`, `EOR`, `SUB`, `RSB`, `ADD`, `ORR`, `MOV`, `BIC`, `MVN`) when `rn`, `rd` and `rm` are not `r15`. Because `S` is clear and bit 4 is clear, no shifter-carry output or NZCV update is required. When the shift field (`bits[11:4]`) is zero (`14.94%` of active steps), `rm` is used directly without entering the shift switch; otherwise (`14.16%`), the immediate shift (`LSL`, `LSR #1..32`, `ASR #1..32`, `ROR #1..31`, or `RRX` using incoming `CPSR.C`) computes the right operand directly.

Flag-setting operations, carry-dependent arithmetic (`ADC`, `SBC`, `RSC`), test/compare instructions, register-specified shifts (`bit 4 == 1`), PC operands, conditional instructions and debugging continue through `stepARM`. Interrupt and instruction-fetch checks remain ahead of the shortcut.

## Short checks

All 58 CPU checks and 87 timing checks pass (`0 failure(s)`). The constant-shift reference matrix in [`tests/cpu_regression.cpp`](../tests/cpu_regression.cpp) now covers all nine supported opcodes (`AND`, `EOR`, `SUB`, `RSB`, `ADD`, `ORR`, `MOV`, `BIC`, `MVN`), all 16 incoming NZCV patterns, all four shift types (`LSL`, `LSR`, `ASR`, `ROR`/`RRX`), all 32 shift amounts (`0..31`), boundary operand values, PC and non-PC sources, and both `S=0` and `S=1` against the general interpreter.

## One active-scene pair

Reference (`1849572904e1e92f66569ccc2503392e4aaf591cec40f8a883453789be9d8e62`) and candidate (`0270efe6fc12e73d2bbdf60e93dcf76bdd40b14dea689958f470ac7107e050e0`) use matching GCC `-O3 -march=native` unity builds. Both replay `Superstar Chefs [G] (EN).bin` for `4,000,000,000` instructions at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. Throughput is evaluated on the active `3,000,000,000` to `4,000,000,000` instruction window. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes are identical between baseline and candidate and match the established active-scene reference. The [paired results](validation/2026-10-16_register_noflags.csv), [baseline intervals](validation/2026-10-16_register_noflags_baseline.csv) and [candidate intervals](validation/2026-10-16_register_noflags_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 42.7237 MIPS | 49.3262 MIPS | +15.45% |
| Active-section elapsed time | 23,406.2 ms | 20,273.2 ms | -13.39% |
| Section CPU/bus elapsed time | 23,224.8 ms | 20,106.1 ms | -13.43% |
| Section GUI time excluding wait | 146.843 ms | 135.313 ms | -7.85% |
| Whole-run interval throughput | 40.5018 MIPS | 48.0386 MIPS | +18.61% |
| Whole-run process CPU time | 65.7656 s | 55.0938 s | -16.23% |

Note that overall host clock speeds during this battery/power state were lower than in earlier desktop sessions (reference ran at `42.72` MIPS versus `~81.5` MIPS previously), which also caused unpaced audio queue starvation in both runs (`1,121` empty-queue events in reference versus `396` in candidate, `0` dropped samples). Because both executables ran back-to-back in the same pair under identical conditions, the relative comparison remains valid: the candidate reduces whole-run CPU time by `16.23%` (`10.67 s`) and improves active-section throughput by `15.45%` while producing identical guest state. As always, one normal-build pair on a single host does not establish a universal speedup across all cartridges, PGO builds or target low-end hardware.

## Decision

Retain `executeRegisterNoFlags` and the expanded constant-shift regression matrix. Combined with `executeImmediateNoFlags` (`22.93%`), direct flagless ALU shortcuts now cover `52.04%` of instructions in the active Superstar Chefs gameplay window.
