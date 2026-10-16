# Zero-Tick Peripheral Bypass and SRCPND UART Reassertion

## Candidate

The [post-shortcut host profile](77_post_shortcut_host_profile.md) revealed that `ARM920T::step`, `InterpreterExecution::runLegacy` and `Bus::tick` account for `41.7%` of all in-module CPU samples. At the default ratio 3, two out of every three instructions (`666,666,667` steps per billion) pass `peripheralTicks = 0` to `step(0)`, and `step(0)` called `bus.tick(0)` at the end of every instruction.

In `Bus::tick(0)`, the zero-cycle path existed only to reassert UART0 TX level interrupts if the guest had cleared `SRCPND` (bit 28) while the transmit buffer level remained empty (`uart0TxLevelActive`). However, `SRCPND` is only ever modified inside `Bus::writeMmio(0x4A000000, val)`.

This change:
1. Reasserts `uart0TxLevelActive` immediately upon clearing `SRCPND` in [`src/memory/bus.cpp`](../src/memory/bus.cpp) (`Bus::writeMmio`).
2. Introduces `Bus::hasLevelInterruptSource()` in [`src/memory/bus.h`](../src/memory/bus.h) (`uart0TxLevelActive || usbHostEnabled`).
3. In [`src/cpu/arm920t.cpp`](../src/cpu/arm920t.cpp) (`ARM920T::step`), skips calling `bus.tick(peripheralTicks)` entirely when `peripheralTicks == 0` and no level-triggered interrupt source is active.
4. In `Bus::tick`, checks `!cycles` upfront and returns immediately if no level interrupt is pending.

## Short checks

All UART interrupt (`uart_interrupt.exe`, including level trigger reassertion across main acknowledgment, submasking and zero-time tick), USB storage/OHCI (`usb_storage.exe`), 58 CPU (`cpu_regression.exe`) and 87 timing (`emulation_timing.exe`) checks pass (`0 failure(s)`).

## One active-scene pair

Reference (`e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa`, from [docs/73](73_mmu_bus_fastpath.md)) and candidate (`120da4df3fc952442d9fdabc526d3fc017d6f808b469b92e0dccd7141ee3cc11`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_zerotick_bypass.csv), [baseline intervals](validation/2026-10-16_zerotick_bypass_baseline.csv) and [candidate intervals](validation/2026-10-16_zerotick_bypass_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 53.1276 MIPS | 55.4014 MIPS | +4.28% |
| Active-section elapsed time | 18,822.6 ms | 18,050.1 ms | -4.10% |
| Section CPU/bus elapsed time | 18,645.1 ms | 17,879.1 ms | -4.11% |
| Section GUI time excluding wait | 143.048 ms | 139.201 ms | -2.69% |
| Whole-run interval throughput | 51.3001 MIPS | 53.7611 MIPS | +4.80% |
| Whole-run process CPU time | 42.1562 s | 39.4062 s | -6.52% |

Bypassing `bus.tick(0)` on 2 out of every 3 guest instructions improves active-section throughput from `53.13` to `55.40` MIPS (`+4.28%`, saving `766.0 ms` of CPU/bus time in the `3B..4B` section) and lowers whole-run process CPU time from `42.16 s` to `39.41 s` (`-6.52%`) with identical guest state.

## Decision

Retain the zero-tick peripheral bypass and SRCPND UART reassertion.
