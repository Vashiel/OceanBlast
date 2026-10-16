# SUBS and CMP Simple-ALU Experiment

## Candidate

After retaining the [register-ALU no-flags shortcut](68_register_noflags_shortcut.md), the [word-transfer shortcut](69_word_transfer_shortcut.md) and the [halfword-transfer shortcut](70_halfword_transfer_shortcut.md), the remaining calls into `ARM920T::executeSimpleDataProcessing` are dominated by loop-counter `SUBS` (`29.59M` immediate, `~15M` register) and `CMP` (`~30M` immediate, `18.56M` unshifted register), which together represent roughly `75%` of the remaining `~12.5%` instruction slice that enters `executeSimpleDataProcessing`.

This isolated candidate tested an early exit at the top of `ARM920T::executeSimpleDataProcessing` for `SUBS` (`opcode == 2`) and `CMP` (`opcode == 10`) with `S == 1`, `rn != 15`, `rd != 15` and either an unrotated 8-bit immediate (`rotation == 0`) or an unshifted register (`bits[11:4] == 0, rm != 15`). The [archived patch](validation/2026-10-16_subs_cmp_simple_alu.patch) applies to revision `8502713` with `git apply --unidiff-zero --ignore-space-change`.

## Short checks

All 58 CPU checks and 87 timing checks pass (`0 failure(s)`).

## One active-scene pair

Reference (`037fdb721f11cf45364ad590469fde9f0d05369c3c477a70e9d00d45a73cc8b7`) and candidate (`e419f7182350b1d26e5fb07558421c5eaa7103b07d44566fe54e7ab383d536dc`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_subs_cmp_simple_alu.csv), [baseline intervals](validation/2026-10-16_subs_cmp_simple_alu_baseline.csv) and [candidate intervals](validation/2026-10-16_subs_cmp_simple_alu_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 49.3320 MIPS | 49.6976 MIPS | +0.74% |
| Active-section elapsed time | 20,270.8 ms | 20,121.7 ms | -0.74% |
| Section CPU/bus elapsed time | 20,105.6 ms | 19,953.3 ms | -0.76% |
| Section GUI time excluding wait | 135.275 ms | 138.016 ms | +2.03% |
| Whole-run interval throughput | 45.0775 MIPS | 48.6418 MIPS | +7.91% |
| Whole-run process CPU time | 60.6875 s | 56.7969 s | -6.41% |

In the active `3B..4B` gameplay window, the throughput difference is only `+0.74%` (`49.33` versus `49.70` MIPS, `152.3 ms` difference across one billion steps), which is within run-to-run noise and too small to justify extra branching in `executeSimpleDataProcessing`. Because `executeSimpleDataProcessing` already handles `SUBS` and `CMP` inline with only a small switch over rotation/shift and opcode, the additional pre-check saves almost no work.

## Decision

Reject the candidate and keep `executeSimpleDataProcessing` unchanged. With the main single-instruction shortcuts (`executeImmediateNoFlags`, `executeRegisterNoFlags`, `executeSingleDataTransfer` word fast path and `executeHalfwordTransfer` halfword fast path) already covering `~68%` of active-scene instructions, further single-instruction special cases inside the scalar interpreter show diminishing returns.
