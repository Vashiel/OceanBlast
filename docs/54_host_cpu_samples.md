# Host CPU Sample Profile

The [host-cost measurements](53_host_execution_costs.md) place more than 99% of measured main-thread time inside guest CPU execution and its memory/peripheral work in two tested scenes. A user-mode instruction-pointer sampler further separates this inclusive block without adding timers to every guest instruction.

## Method and scope

`tools/sample_host_cpu.cpp` attaches to an already running 64-bit Windows emulator process, selects its highest-CPU-time thread, and samples the thread's instruction pointer with `SuspendThread` and `GetThreadContext`. `tools/analyze_host_samples.py` resolves offsets through the exact executable's DWARF symbols, including inline frames, and groups the innermost project-owned source location. The sampler requests a 1-ms timer interval; the observed rate was roughly 550 samples/s. Sampling briefly pauses the thread and may change throughput, so sample shares are approximate hotspot estimates rather than an instruction or cycle count.

Build the helper with `make build/sample_host_cpu.exe`. Launch a DWARF-enabled 64-bit development executable with `--host-cost-profile <start-step>`; its `[HostCost] Sampling window starts` log line marks the chosen scene. While it runs, use `build/sample_host_cpu.exe <pid> 16000 1 > samples.csv`, then `python tools/analyze_host_samples.py build/oceanblast-dev.exe samples.csv --addr2line <addr2line-executable>`. Keep the exact executable used for the run: rebuilding it can shift code offsets, even when the source changes only slightly.

Both runs used the same normal `-O3 -march=native` unity development executable (SHA-256 `4af1da940dada8d29f90da6e8743f1e1278f695acc1c5f6cb52622e1f2130e0b`), GUI, sound, disabled host pacing, a blank EEPROM, and deterministic input fixtures. The Wade sample begins at step 7.0 billion in the Rocco fight and runs for 16 s within the 7.0–8.5-billion-step interval. The Superstar Chefs sample begins at step 1.0 billion and runs for 20 s before the 3.0-billion-step limit. The exact end step of that sample was not recorded. Both emulator runs completed at their step limits; Wade's final active framebuffer SHA-256 matches the previous replay (`3c2ddd71d41804001c6f240373bc4976cb9f56c7894ce114f6786877afbf6ea9`).

The raw [Wade samples](validation/2026-10-13_wade_host_pc_samples.csv), [Superstar Chefs samples](validation/2026-10-13_chefs_host_pc_samples.csv), and [summary](validation/2026-10-13_host_sample_summary.csv) give these in-module shares:

| Project source group | Wade fight, 8,750 samples | Superstar Chefs, 10,926 samples |
| --- | ---: | ---: |
| ARM CPU (`src/cpu`) | 68.0% | 69.9% |
| Memory and peripherals (`src/memory`) | 24.1% | 22.9% |
| Execution batch (`src/core`) | 7.4% | 6.8% |
| Display, main, audio, or unresolved | 0.5% | 0.4% |

The sampler also recorded 39 Wade and 69 Superstar Chefs PCs outside the executable module, less than 1% of each sample set. These are excluded from the table. Audio callback work on a separate thread is outside this CPU-thread sample.

In Wade, the largest resolved functions are `ARM920T::stepARM` (23.4%), `ARM920T::step` (16.8%), `executeSimpleDataProcessing` (11.0%), `Bus::translate` (11.0%), `runLegacyInstructions` (7.4%), `executeSingleDataTransfer` (5.7%), `Bus::tick` (5.0%), and `Bus::readInstruction` (4.5%). In Superstar Chefs, `stepARM` (26.0%), `executeSimpleDataProcessing` (17.3%), `step` (16.0%), `Bus::tick` (7.5%), `runLegacyInstructions` (6.8%), `Bus::translate` (6.3%), and `Bus::readInstruction` (3.8%) lead. Percentages attribute the sampled host PC to one innermost resolved function; inlining can move code between source groups, so these are not exclusive hardware costs.

The primary host work is therefore ARM execution and decoding, followed by guest address translation, peripheral ticking, and instruction fetch. A display optimization alone cannot substantially raise these two scene throughputs. The data supports benchmarking the ARM execution path and TLB/address-translation path first, while preserving identical guest state and actual gameplay behavior. It does not justify changing the emulated CPU ratio or claim that either game's speed matches original hardware.

## Source-line resolution

The optional `--top-lines` report resolves the same samples to source lines. It narrows the next benchmark targets without adding instrumentation to the guest hot path:

| Source location | Wade fight | Superstar Chefs | Operation |
| --- | ---: | ---: | --- |
| `src/memory/bus.h:112` | 6.5% | 4.0% | Direct-mapped TLB hit check (`valid` and virtual page match) |
| `src/memory/bus.h:111` | 2.2% | 1.1% | TLB entry lookup |
| `src/memory/bus.h:304` | 2.8% | 4.5% | Per-tick UART0 TX level-interrupt reassert check |
| `src/memory/bus.h:115` | 0.7% | — | User-mode TLB page-permission check |
| `src/memory/bus.h:105` | 0.7% | — | MMU-disabled fast-path check |

These shares are percentages of all in-module samples for the named scene, not cycle costs. The UART check is a plausible optimization target because it is reached from the repeated peripheral tick path. A deferred-reassertion candidate preserved guest-state and framebuffer hashes in paired runs, but its throughput advantage was not repeatable under the observed host load; the candidate was discarded. No behavioral or default-timing change is justified by this measurement. The TLB hit path remains the clearest next target for isolated, paired benchmarking.
