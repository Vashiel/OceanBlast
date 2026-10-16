# Pre-Indexed Immediate Word Transfer Shortcut

## Candidate

The [active-scene guest instruction mix](67_active_scene_instruction_mix.md) shows that after `REG_ALU_NOFLAGS` (`29.15%`) and `IMM_ALU_NOFLAGS_FASTPATH` (`22.93%`), the third-largest category is `LDR_WORD_IMM` (`75,585,629` steps, `7.56%`), alongside `STR_WORD_IMM` (`24,269,555` steps, `2.43%`). Within those two categories, `69,323,799` word loads and `18,233,843` word stores (`87,557,642` steps total, `8.76%` of the active window) are simple pre-indexed immediate-offset word transfers without writeback and with a non-PC destination/source (`rd != 15`).

This candidate adds a fast check at the entry of `ARM920T::executeSingleDataTransfer` in [`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp) for `(instr & 0x03600000u) == 0x01000000u && rd != 15` (bit 25 = 0 for immediate offset, bit 24 = 1 for pre-indexed, bit 22 = 0 for word transfer, bit 21 = 0 for no writeback). It computes the target address directly, uses `bus.read32(targetAddr)` directly when aligned (`(targetAddr & 3u) == 0`) or `readRotatedWord(bus, targetAddr)` when unaligned, checks `bus.getLastFault()`, and returns without evaluating register-offset shifts, byte branches, `rd == 15` branch mode switches or post-index/writeback updates.

## Short checks

All 58 CPU checks (including unaligned `LDR` word rotation, unaligned `STR` word alignment and MMU data-abort handling) and all 87 timing checks pass (`0 failure(s)`).

## One active-scene pair

Reference (`0270efe6fc12e73d2bbdf60e93dcf76bdd40b14dea689958f470ac7107e050e0`, containing the retained register-ALU no-flags shortcut from [docs/68](68_register_noflags_shortcut.md)) and candidate (`c95819e52c53b56269484d59ceb68b5e06875586994490780f44700050c0ca43`) use matching GCC `-O3 -march=native` unity builds. Both replay `Superstar Chefs [G] (EN).bin` for `4,000,000,000` instructions at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_word_transfer_no_wb.csv), [baseline intervals](validation/2026-10-16_word_transfer_no_wb_baseline.csv) and [candidate intervals](validation/2026-10-16_word_transfer_no_wb_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 42.5630 MIPS | 50.5999 MIPS | +18.88% |
| Active-section elapsed time | 23,494.6 ms | 19,762.9 ms | -15.88% |
| Section CPU/bus elapsed time | 23,280.3 ms | 19,589.1 ms | -15.86% |
| Section GUI time excluding wait | 180.245 ms | 144.114 ms | -20.05% |
| Whole-run interval throughput | 40.5751 MIPS | 44.5018 MIPS | +9.68% |
| Whole-run process CPU time | 63.8750 s | 47.7344 s | -25.27% |

Note that host power/clock fluctuations on this machine continue to affect absolute wall-clock rates between runs (the reference executable here ran at `42.56` MIPS in the active section after running at `49.33` MIPS as the candidate in the previous pair). Consequently, the magnitude of the percentage gain in a single unpaced pair partly reflects host frequency variance alongside the code change. Nevertheless, process CPU time drops from `63.88 s` to `47.73 s` (`-25.27%`), active-section throughput increases in the matched pair, and guest state is preserved exactly.

## Decision

Retain the pre-indexed immediate word load/store fast path in `ARM920T::executeSingleDataTransfer`.
