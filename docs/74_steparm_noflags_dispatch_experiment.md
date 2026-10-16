# Step-to-StepARM No-Flags Dispatch Experiment

## Candidate

In [`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp), `ARM920T::step` dispatches unconditional flagless immediate (`executeImmediateNoFlags`, `22.93%` of active-scene steps) and register (`executeRegisterNoFlags`, `29.11%` of active-scene steps) ALU instructions before calling `ARM920T::stepARM`. This experiment tested moving both shortcuts from `ARM920T::step` into the entry of `ARM920T::stepARM` under a shared top-6-bit mask (`(instr & 0xFC000000u) == 0xE0000000u`) so that `ARM920T::step` itself would contain only interrupt/fetch handling and a direct call to `stepThumb` or `stepARM` ([patch](validation/2026-10-16_steparm_noflags_dispatch.patch)).

## Short checks

All 58 CPU checks (`cpu_regression.exe`) and all 87 timing checks (`emulation_timing.exe`) pass (`0 failure(s)`).

## One active-scene pair

Reference (`e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa`, from [docs/73](73_mmu_bus_fastpath.md)) and candidate (`f2ebc9c432e1a73ce03b007df6dd072a15c76638e3e57807e4611af5895bf186`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_steparm_noflags_dispatch.csv), [baseline intervals](validation/2026-10-16_steparm_noflags_dispatch_baseline.csv) and [candidate intervals](validation/2026-10-16_steparm_noflags_dispatch_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 57.9603 MIPS | 54.5992 MIPS | -5.80% |
| Active-section elapsed time | 17,253.2 ms | 18,315.3 ms | +6.16% |
| Section CPU/bus elapsed time | 17,095.4 ms | 18,147.1 ms | +6.15% |
| Section GUI time excluding wait | 125.659 ms | 134.446 ms | +6.99% |
| Whole-run interval throughput | 55.9614 MIPS | 53.2280 MIPS | -4.88% |
| Whole-run process CPU time | 42.9844 s | 42.3125 s | -1.56% |

Because `ARM920T::stepARM` is a large out-of-line function whereas `executeImmediateNoFlags` and `executeRegisterNoFlags` are inlined directly into `ARM920T::step`, moving the `52.04%` flagless ALU fast path inside `stepARM` forces every flagless ALU instruction through a call/prologue into `stepARM` instead of retiring directly inside `step`. Active-section throughput drops from `57.96` to `54.60` MIPS (`-5.80%`) and whole-run interval throughput drops from `55.96` to `53.23` MIPS (`-4.88%`).

## Decision

Reject and revert the `step`-to-`stepARM` dispatch move; keep `executeImmediateNoFlags` and `executeRegisterNoFlags` inlined directly inside `ARM920T::step`.
