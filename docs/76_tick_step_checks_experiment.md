# `Bus::tick` and `ARM920T::step` Branch-Hint Experiment

## Candidate

This experiment tested removing the zero-cycle `usbHostEnabled && usbHost.irq()` check before `if (!cycles) return;` in `Bus::tick` ([`src/memory/bus.h`](../src/memory/bus.h)) and adding `__builtin_expect(..., 0)` hints to the rare per-instruction checks (`uart0TxLevelActive`, `usbHostEnabled`, `state.halted`, `state.waitingForInterrupt`, pending IRQ, `fetchFault` and `thumb`) in `Bus::tick` and `ARM920T::step` ([`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp)) ([patch](validation/2026-10-16_tick_step_checks.patch)).

## Short checks

All UART interrupt (`uart_interrupt.exe`), USB storage/OHCI (`usb_storage.exe`), 58 CPU (`cpu_regression.exe`) and 87 timing (`emulation_timing.exe`) checks pass (`0 failure(s)`).

## One active-scene pair

Reference (`e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa`, from [docs/73](73_mmu_bus_fastpath.md)) and candidate (`1e6f8c73d23fdfd2f7ea671ea91a7b3201bbbd088853ebf2177df13d0547bc3c`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_tick_step_checks.csv), [baseline intervals](validation/2026-10-16_tick_step_checks_baseline.csv) and [candidate intervals](validation/2026-10-16_tick_step_checks_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 51.5834 MIPS | 51.6508 MIPS | +0.13% |
| Active-section elapsed time | 19,386.1 ms | 19,360.8 ms | -0.13% |
| Section CPU/bus elapsed time | 19,205.2 ms | 18,128.3 ms | -5.61% |
| Section GUI time excluding wait | 146.900 ms | 1,199.260 ms | +716.38% |
| Whole-run interval throughput | 52.7682 MIPS | 51.9012 MIPS | -1.64% |
| Whole-run process CPU time | 44.5469 s | 42.7500 s | -4.03% |

Active-section throughput is virtually unchanged (`51.58` versus `51.65` MIPS, `+0.13%`, with a 1.05 s GUI stall in the candidate window and `-1.64%` whole-run interval throughput). Under the roadmap's single-pair rule, an inconclusive `+0.13%` active-section difference does not justify retaining the change.

## Decision

Reject and revert the `Bus::tick` and `ARM920T::step` branch-hint candidate.
