# Data MMU and Bus Read/Write Fast-Path Streamlining

## Candidate

Every guest memory load and store in active gameplay calls `Bus::read8/16/32` or `Bus::write8/16/32` in [`src/memory/bus.h`](../src/memory/bus.h), which in turn calls `Arm920TMmu::translate` in [`src/memory/arm920t_mmu.h`](../src/memory/arm920t_mmu.h) and checks physical SDRAM bounds. Across the `3,000,000,000` to `4,000,000,000` active gameplay window ([`docs/67_active_scene_instruction_mix.md`](67_active_scene_instruction_mix.md)), single-data, halfword and block transfers account for more than `192` million instructions (over `247` million word/halfword/byte transfers when counting multi-register `LDM`/`STM`).

The hot non-faulting SDRAM path previously performed several redundant operations on every access:
1. In `Arm920TMmu::translate`, user-mode access permissions tested `ap == 0 || ap == 1 || (ap == 2 && isWrite)` as a multi-branch condition without branch-prediction hints on MMU enable, TLB hit or permission fault. Because `ap` is a 2-bit value (`0..3`), `ap <= (isWrite ? 2u : 1u)` expresses the exact same permission check in a single comparison.
2. In `Bus::read8/16/32` and `Bus::write8/16/32`, every non-faulting access wrote `lastFault = MmuFault::NONE` to the `Bus` member even though `lastFault` is already `MmuFault::NONE` unless a data abort occurred, and every data-abort handler in [`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp) (as well as `Bus::reset` in [`src/memory/bus.cpp`](../src/memory/bus.cpp)) explicitly calls `clearLastFault()`.
3. In `Bus::read8Phys/16Phys/32Phys` and `Bus::write8/16/32`, SDRAM bounds were checked with two unsigned comparisons (`pa >= ADDR_SDRAM_BASE && (pa - ADDR_SDRAM_BASE) < ...`). In 32-bit unsigned arithmetic, `(pa - ADDR_SDRAM_BASE) < ADDR_SDRAM_SIZE` (and `<= ADDR_SDRAM_SIZE - width`) wraps on `pa < ADDR_SDRAM_BASE` and tests both bounds in one comparison.

## Short checks

All 8 standalone MMU translation checks (`mmu_translation.exe`), all 58 CPU checks (`cpu_regression.exe`) and all 87 timing checks (`emulation_timing.exe`) pass (`0 failure(s)`).

## One active-scene pair

Reference (`d664045aac6367bf456f320572668bc3ca61777f7e6984cade3bdbc6fe47fedd`, from [docs/72](72_instruction_fetch_check.md)) and candidate (`e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_mmu_bus_fastpath.csv), [baseline intervals](validation/2026-10-16_mmu_bus_fastpath_baseline.csv) and [candidate intervals](validation/2026-10-16_mmu_bus_fastpath_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 54.5271 MIPS | 65.8679 MIPS | +20.80% |
| Active-section elapsed time | 18,339.5 ms | 15,181.9 ms | -17.22% |
| Section CPU/bus elapsed time | 18,167.1 ms | 15,060.7 ms | -17.10% |
| Section GUI time excluding wait | 138.890 ms | 93.433 ms | -32.73% |
| Whole-run interval throughput | 48.0340 MIPS | 60.0473 MIPS | +25.01% |
| Whole-run process CPU time | 53.9688 s | 43.7031 s | -19.02% |

Streamlining `Arm920TMmu::translate` and the non-faulting SDRAM path in `Bus::read/write` improves active-section throughput from `54.53` to `65.87` MIPS (`+20.80%`, saving `3,106.4 ms` of CPU/bus time in the `3B..4B` section) and lowers whole-run process CPU time from `53.97 s` to `43.70 s` (`-19.02%`) in this matched pair.

## Decision

Retain the MMU permission check and `Bus::read/write` fast-path streamlining.
