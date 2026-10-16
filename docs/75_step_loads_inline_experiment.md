# Inlining Word and Halfword Transfer Shortcuts in `ARM920T::step` Experiment

## Candidate

Following the observation in [`docs/74_steparm_noflags_dispatch_experiment.md`](74_steparm_noflags_dispatch_experiment.md) that `ARM920T::stepARM` is out-of-line whereas `executeImmediateNoFlags` and `executeRegisterNoFlags` are inlined into `ARM920T::step`, this experiment tested inlining the unconditional pre-indexed immediate word (`LDR`/`STR`, `8.76%` of active steps) and halfword (`LDRH`/`LDRSH`/`STRH`, `7.38%` of active steps) shortcuts directly inside `ARM920T::step` ([patch](validation/2026-10-16_step_loads_inline.patch)).

## Short checks

All 58 CPU checks (`cpu_regression.exe`) and all 87 timing checks (`emulation_timing.exe`) pass (`0 failure(s)`).

## One active-scene pair

Reference (`e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa`, from [docs/73](73_mmu_bus_fastpath.md)) and candidate (`fa1f3318de82e009403ff374cc9b065e433e1d5565f64609cc2e1e57305f58af`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_step_loads_inline.csv), [baseline intervals](validation/2026-10-16_step_loads_inline_baseline.csv) and [candidate intervals](validation/2026-10-16_step_loads_inline_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 55.6143 MIPS | 53.4702 MIPS | -3.86% |
| Active-section elapsed time | 17,981.0 ms | 18,702.0 ms | +4.01% |
| Section CPU/bus elapsed time | 17,814.4 ms | 18,520.8 ms | +3.96% |
| Section GUI time excluding wait | 134.487 ms | 147.064 ms | +9.35% |
| Whole-run interval throughput | 54.1574 MIPS | 52.4616 MIPS | -3.13% |
| Whole-run process CPU time | 44.0625 s | 48.4219 s | +9.89% |

Expanding `ARM920T::step` with two additional mask checks and inlined `Bus::read16/32`/`write16/32` bodies (plus MMU translation and frame-capture checks) increases register pressure and code size inside `ARM920T::step`, lowering active-section throughput from `55.61` to `53.47` MIPS (`-3.86%`) and increasing whole-run process CPU time from `44.06 s` to `48.42 s` (`+9.89%`).

## Decision

Reject and revert inlining the word and halfword transfer shortcuts into `ARM920T::step`; keep them at the entry of `executeSingleDataTransfer` and `executeHalfwordTransfer`.
