# Interpreter Execution Boundary and Short Checks

The subsequent [machine-core changes and decoder experiment](59_machine_core_and_decoder_experiment.md) extend this first slice. The result below records the earlier build.

## Scope

The first execution refactor introduces `InterpreterExecution` in `src/core/execution_batch.h`. Both the single-step path and bounded batches in `main.cpp` now use its legacy-ratio or register-clock step. Each step still calls `ARM920T::step`; instruction decoding, exceptions, memory translation and cartridge presets are unchanged.

This removes duplicate time-advancement logic from the application loop. It is the first P1 slice in the [roadmap](../ROADMAP.md). Architectural CPU state still belongs to `ARM920T`; alternative execution engines and a shared event scheduler remain future work. The refactor makes no performance-improvement claim.

## Routine check

Budget: 5-10 minutes including the relevant test build. Reuse existing tools; do not train a new PGO profile for this structural check.

1. Build the developer executable and timing check with the project toolchain:

   ```sh
   make -j2 build/emulation_timing.exe build/cpu_regression.exe build/oceanblast-dev.exe
   ./build/emulation_timing.exe
   ./build/cpu_regression.exe
   ```

2. In a separate directory below `build/`, use a fresh EEPROM path and set `OCEANBLAST_SETTINGS_PATH` to a local settings file. Run the developer executable with the supplied Superstar Chefs dump:

   ```text
   oceanblast-dev.exe <cartridge.bin> --gui --sound --profile --steps 1200000000 --cpu-steps-per-tick 3 --scale 2 --window-mode plain --nvram board.nvram --exit-on-limit
   ```

3. Inspect the first useful visible screen. Stop immediately for an evident relevant display, pacing or audio failure. Otherwise inspect the title menu briefly and close the window once the question is answered. The instruction limit is a fallback, not a requirement to keep watching. If no useful screen appears within 30 seconds of execution, stop and report the startup failure. Do not enter gameplay for this check.

The explicit ratio fixes the check configuration. For a performance comparison, use matching compiler profiles and the same host, dump, EEPROM state and settings. A developer executable and the installed PGO launcher are not a valid pair for attributing throughput changes to this refactor.

## 2026-10-15 result

The normal GCC `-O3 -march=native` unity developer build completed. All 76 emulation-timing checks and 55 existing CPU checks passed. The timing check now exercises a transition from 37 single steps into a batch and compares registers, peripheral phase, timer and pending interrupts against independent scalar execution. Existing checks also cover register-clock deadlines, wait-for-interrupt, IRQ entry/return and DMA audio sample equivalence.

One visible Superstar Chefs run reached the title menu with no obvious display corruption and exited normally at 1.2 billion steps (20 modeled seconds). The last four sampled intervals reported 35-39 host image submissions/s and 97.69-102.32% of the configured pacing target. All recorded intervals reported zero empty audio queues and zero dropped samples. These are short functional and queue checks; they do not establish listening quality, gameplay cadence, original-hardware speed or a throughput gain. No further scenes or repeated pairs were run.

| Item | Recorded value |
| --- | --- |
| Host CPU | Intel Core i7-1185G7 @ 3.00 GHz |
| Candidate executable SHA-256 | `0498f24a97ea2ad9a3758816d5e7549685f7febe55436503cc6ec5556c9cc292` |
| Cartridge SHA-256 | `465ddcd7f9722d73b4fd748d3b8d822bc99c06ea91c301bd768f583a1d52b2ff` |
| Cartridge identity | Superstar Chefs, CRC32 `93209877` |
| Execution mode | Legacy ratio 3, default batch 4096, 60 MIPS pacing |
| Initial persistent state | Separate initially absent EEPROM image and settings file |
| Input | No scripted input; title screen only |
| Local executable | `build/oceanblast-dev.exe`; installed launcher was not replaced |

The [short run metrics](validation/2026-10-15_execution_boundary_title.csv) retain the original samples. Local logs and guest dumps remain under ignored `build/execution-boundary-title/`. A weak target CPU has not yet been selected or tested.
