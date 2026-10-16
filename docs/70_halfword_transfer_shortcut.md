# Pre-Indexed Immediate Halfword Transfer Shortcut

## Candidate

The [active-scene guest instruction mix](67_active_scene_instruction_mix.md) identifies `LDRH_STRH_LDRSB_LDRSH` as the fourth-largest category in active gameplay (`73,833,858` steps, `7.38%`), driven primarily by the second-hottest basic block (`0x0000b9e4`, `28.89%` of all active steps), which repeatedly executes pre-indexed immediate `LDRSH`, `LDRH` and `STRH` instructions without writeback (`0xe1d810b0`, `0xe1d8e0b2`, `0xe1d740b0`, `0xe1d750b2`).

This candidate adds a fast check at the entry of `ARM920T::executeHalfwordTransfer` in [`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp) for pre-indexed immediate halfword transfers without writeback (`(instr & 0x01600000u) == 0x01400000u && (op & 1u)`, covering `LDRH`, `LDRSH` and `STRH`). It computes the 8-bit immediate offset (`((instr >> 4) & 0xF0u) | (instr & 0xFu)`), issues a single `bus.read16(targetAddr)` or `bus.write16(targetAddr, ...)`, checks `bus.getLastFault()`, and returns without evaluating register offsets, signed-byte branches (`LDRSB`) or post-index/writeback updates.

## Short checks

All 58 CPU checks and 87 timing checks pass (`0 failure(s)`).

## One active-scene pair

Reference (`c95819e52c53b56269484d59ceb68b5e06875586994490780f44700050c0ca43`, containing the word-transfer shortcut from [docs/69](69_word_transfer_shortcut.md)) and candidate (`037fdb721f11cf45364ad590469fde9f0d05369c3c477a70e9d00d45a73cc8b7`) use matching GCC `-O3 -march=native` unity builds. Both replay `Superstar Chefs [G] (EN).bin` for `4,000,000,000` instructions at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_halfword_transfer_no_wb.csv), [baseline intervals](validation/2026-10-16_halfword_transfer_no_wb_baseline.csv) and [candidate intervals](validation/2026-10-16_halfword_transfer_no_wb_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 55.0961 MIPS | 57.9089 MIPS | +5.11% |
| Active-section elapsed time | 18,150.1 ms | 17,268.5 ms | -4.86% |
| Section CPU/bus elapsed time | 18,027.9 ms | 17,151.4 ms | -4.86% |
| Section GUI time excluding wait | 98.387 ms | 95.414 ms | -3.02% |
| Whole-run interval throughput | 43.1250 MIPS | 56.2790 MIPS | +30.50% |
| Whole-run process CPU time | 69.9688 s | 60.4219 s | -13.64% |

In the active `3B..4B` gameplay window, throughput increases by `5.11%` (`55.10` to `57.91` MIPS, saving `876.5 ms` of CPU/bus time), which is proportional to the `7.38%` instruction share of halfword transfers. Whole-run process CPU time decreases by `13.64%` (`9.55 s`). As in previous unpaced runs on this machine, host clock/power state varies across the multi-minute run, so the single-pair numbers are specific to this host and run rather than a universal guarantee.

## Decision

Retain the pre-indexed immediate halfword transfer fast path in `ARM920T::executeHalfwordTransfer`. Together with the retained immediate ALU (`22.93%`), register ALU (`29.11%`) and word transfer (`8.76%`) fast paths, specialized execution paths now cover roughly `68%` of instructions in the active Superstar Chefs gameplay window.

## Portable local PGO build

The source at `62029d2` (including the retained register-ALU, word-transfer and halfword-transfer shortcuts) was built as an `-O3` unity PGO executable with `-march=x86-64`, using `Superstar Chefs [G] (EN).bin` and `Rayman 3 [G] (M10).bin` training runs of 900 million instructions each at ratio 3.

The resulting executable, SHA-256 `9d3fea8f6a0bd04bc28bd77e4e560960b59685f8291d447c77a5fae9fe7a5f5c`, replaces the local `bin/oceanblast.exe`. Its predecessor (`aefd4e59cc6a14a74796703e914be25642c8a91729a0e730b9aa3ff445db6d59`) is backed up under ignored `build/launcher-backup-2026-10-16-before-pgo/oceanblast.exe`. In the 1.2-billion-instruction GUI/audio title check ([intervals](validation/2026-10-16_pgo_title.csv)), final SDRAM (`c3ce3acc5bd6e7b93da652ffd4d1175b85488a18832ae5c41ce5fb22b10e3057`), active framebuffer (`c85cd821b24007196739577c87d7664ef27126832d3ecfb2c3bc0a9fd76f222c`) and CPU state (`PC = 0x5dc30`, zero dropped samples) match the previous PGO title check exactly, while running under the lower host power limit (`~46-48` MIPS, `176` empty-queue events). No new GitHub release archive was published.

