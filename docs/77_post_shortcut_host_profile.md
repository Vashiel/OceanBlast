# Post-Shortcut Active-Scene Host Profile

## Why this measurement

Since the [earlier active-scene host profile](65_active_scene_host_profile.md), five core optimizations were retained:
* [Register-ALU no-flags shortcut](68_register_noflags_shortcut.md) (`29.11%` of active steps)
* [Pre-indexed immediate word transfer shortcut](69_word_transfer_shortcut.md) (`8.76%` of active steps)
* [Pre-indexed immediate halfword transfer shortcut](70_halfword_transfer_shortcut.md) (`7.38%` of active steps)
* [Combined instruction-fetch subpage and alignment check](72_instruction_fetch_check.md) (`97.89%` subpage hit rate)
* [Data MMU and `Bus::read/write` fast-path streamlining](73_mmu_bus_fastpath.md)

After two subsequent dispatch/inlining experiments ([docs/74](74_steparm_noflags_dispatch_experiment.md) and [docs/75](75_step_loads_inline_experiment.md)) and a branch-hint experiment ([docs/76](76_tick_step_checks_experiment.md)) were rejected, this profile re-samples the active Superstar Chefs gameplay window (`3,000,000,000` to `4,000,000,000` steps) on the retained `e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa` executable to measure where host CPU time now concentrates.

## Conditions and boundaries

The unchanged normal GCC `-O3 -march=native` unity executable has SHA-256 `e0cd83d6b4ff69cc1608336d07719e3d200d34abce96c8d59a3622c4bc0160aa`. It runs with GUI/audio enabled, host pacing disabled (`--clock-mips 0`), ratio 3, the [active input fixture](../tests/chefs_active_scene.txt), isolated settings and the blank `0xFF` EEPROM fixture (`d0ff1b294b5288d1ae1421eadf5b2d38a8752b76d472ff30bed9028e25b1c5b8`). The [metadata](validation/2026-10-16_active_host_metadata.json) records the exact hashes and sampling window.

After the `[HostCost] Sampling window starts at step 3000000000` marker, `sample_host_cpu.exe` samples the highest-CPU-time thread for `6,000 ms` at a requested `2 ms` interval (`6.1964 s` elapsed). Sampling finishes before the four-billion-step limit, and the replay completes with matching CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes. Because thread suspension alters runtime behavior, this run is not a throughput benchmark.

## Observations

There are `2,062` in-module samples, `22` outside the executable module and `0` failed samples. The [raw PC offsets](validation/2026-10-16_active_host_pc.csv) and [resolved summary](validation/2026-10-16_active_host_summary.txt) are preserved.

| Resolved source group | Earlier profile ([docs/65](65_active_scene_host_profile.md)) | Current profile |
| --- | ---: | ---: |
| CPU | 67.6% | 64.1% |
| Memory/peripherals | 24.7% | 26.4% |
| Execution scheduling/batching | 7.2% | 8.9% |
| Other groups combined | 0.5% | 0.6% |

| Resolved function | Earlier share | Current share | Samples (`2,062` total) |
| --- | ---: | ---: | ---: |
| Instruction step (`ARM920T::step`) | 17.0% | 26.3% | 543 |
| ARM dispatcher (`ARM920T::stepARM`) | 20.1% | 12.0% | 247 |
| Legacy execution batch (`InterpreterExecution::runLegacy`) | 7.1% | 8.9% | 184 |
| MMU translation (`Arm920TMmu::translate`) | 7.5% | 8.6% | 177 |
| Peripheral advancement (`Bus::tick`) | 7.8% | 6.5% | 134 |
| General simple-ALU routine (`executeSimpleDataProcessing`) | 13.4% | 6.1% | 126 |
| Retained immediate no-flags shortcut (`executeImmediateNoFlags`) | 6.3% | 4.9% | 101 |
| Instruction fetch (`Bus::readInstruction`) | 4.0% | 4.3% | 88 |
| Retained register no-flags shortcut (`executeRegisterNoFlags`) | — | 3.8% | 78 |
| ARM multiple-register transfer (`executeBlockDataTransfer`) | 2.4% | 2.9% | 59 |
| ARM single-data transfer (`executeSingleDataTransfer`) | 1.8% | 2.5% | 51 |
| ARM halfword transfer (`executeHalfwordTransfer`) | 2.0% | 2.2% | 46 |
| Fault check (`Bus::getLastFault`) | 0.3% | 1.8% | 37 |
| Timer 4 advancement (`Timer4::advance` + `isRunning`) | 2.3% | 2.4% | 51 |

## What changed and what now dominates

1. **General decoding and ALU work dropped sharply:** `ARM920T::stepARM` fell from `20.1%` to `12.0%` and `executeSimpleDataProcessing` fell from `13.4%` to `6.1%`, because `52.04%` of active instructions now retire directly inside `ARM920T::step` via `executeImmediateNoFlags` (`4.9%`) and `executeRegisterNoFlags` (`3.8%`).
2. **Per-instruction boundary overhead is now the largest single slice:** `ARM920T::step` (`26.3%`), `InterpreterExecution::runLegacy` (`8.9%`) and `Bus::tick` (`6.5%`) together account for **`41.7%` of all in-module samples**:
   * In `InterpreterExecution::runLegacy` (`8.9%`), every instruction calls out-of-line `cpu.step(1)` or `cpu.step(0)` inside the phase loop, and `cpu.step(0)` (`2/3` of all steps at ratio 3) still calls `bus.tick(0)`.
   * Inside `ARM920T::step` (`26.3%`), lines `279–294` (`lastCycles = 1`, `halted`, `waitingForInterrupt`, `!(cpsr & FLAG_I) && bus.hasPendingIrq()`) and lines `307–318` (shortcut dispatch and `bus.tick(peripheralTicks)` call/return) account for over `16%` of total samples.
   * In `Bus::tick` (`6.5%`), line `244` (`if (uart0TxLevelActive && !(regSrcpnd & (1u << 28)))`) accounts for `3.5%` of total samples solely because `bus.tick(0)` is executed on every zero-tick ratio step (`666.7M` times per billion steps), even though `uart0TxLevelActive` is `false` throughout gameplay.
3. **Why `Bus::tick(0)` exists at all:** `Bus::tick(0)` was retained only so that a diagnostic `cpu.step(0)` or zero-tick ratio step reasserts UART0 TX level interrupts after the guest clears `SRCPND` while `uart0TxLevelActive` is true. However, `SRCPND` (`0x4A000000`) is only ever cleared inside `Bus::writeMmio(0x4A000000, val)`!

## Next candidate

Evaluate eliminating the per-instruction `bus.tick(0)` call on zero-tick ratio steps (or reasserting `uart0TxLevelActive` directly when `SRCPND` is cleared in `Bus::writeMmio` / only calling `bus.tick` when `peripheralTicks != 0` or `uart0TxLevelActive` is active) and specializing the legacy ratio batch loop so zero-tick steps do not pay for `bus.tick(0)`.
