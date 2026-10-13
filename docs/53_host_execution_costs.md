# Host Execution Costs

OceanBlast's step rate alone cannot identify the host bottleneck. The optional `--host-cost-profile <start-step>` diagnostic records wall time spent in guest CPU execution, including memory and peripheral work, separately from GUI processing and host pacing. It writes `host_cost.csv` in the current session directory. Normal runs do not collect these timings.

## Method

The diagnostic times execution batches and scalar CPU steps, the GUI update block, and the host pacing wait. `cpu_and_bus_ms` includes instruction fetch, ARM decode and execution, MMU and memory access, interrupts, timers, DMA, and other work performed by `Bus::tick`. `ui_excluding_wait_ms` includes event processing, framebuffer hashing and presentation. `other_ms` is the remaining main-thread loop time. Audio callback work on a separate host thread is not included. Timing calls and the normal non-PGO build affect absolute throughput; these columns are diagnostic categories, not cycle-accurate CPU samples.

The Wade measurement starts at step 7.0 billion and ends at 8.5 billion in the Rocco fight, with the [ratio-6 input fixture](../tests/wade_ratio6_fight.txt), GUI, sound, no host MIPS limit, and a blank EEPROM. The Superstar Chefs run covers the first 1.2 billion steps with the [automatic input fixture](../tests/chefs_auto_confirm.txt), GUI, sound, no host MIPS limit, and a blank EEPROM. Both use the same normal `-O3` unity development build. The raw [host-cost records](validation/2026-10-13_host_cost.csv) are:

| Cartridge and measured interval | CPU + bus | GUI | Host wait | Other |
| --- | ---: | ---: | ---: | ---: |
| Wade, 7.0–8.5 billion steps, 18.413 s | 18.307 s (99.4%) | 0.087 s (0.5%) | 0 | 0.019 s (0.1%) |
| Superstar Chefs, 0–1.2 billion steps, 16.440 s | 16.303 s (99.2%) | 0.096 s (0.6%) | 0 | 0.041 s (0.3%) |

An independent [Wade output comparison](validation/2026-10-13_wade_host_output_comparison.csv) used the installed PGO x64 build (SHA-256 `97203e3e51264a569bc6b36e39ecf87ab1184e63f6c07a24e11571166462a666`), the same 8.5-billion-step input and EEPROM initialization, and disabled host pacing. Headless without sound took 70.90 s, GUI without sound 68.45 s, and GUI with sound 71.12 s. All three produced the same final active-framebuffer SHA-256, `3c2ddd71d41804001c6f240373bc4976cb9f56c7894ce114f6786877afbf6ea9`. These single-run elapsed times vary by more than the apparent GUI difference, so they do not establish a precise graphics or audio overhead. They are consistent with the instrumented result that host presentation is not the dominant cost in these scenes.

This measurement narrows the performance work to guest CPU execution and its memory/peripheral operations. It does **not** separate ARM dispatch from instruction fetch, MMU, RAM writes, timers, or DMA within `cpu_and_bus_ms`. It also does not establish a hardware-accurate instruction budget or generalize the measured percentages to every cartridge. The [host CPU sample profile](54_host_cpu_samples.md) resolves the main functions active inside this inclusive bucket. Raising a cartridge's MIPS preset would not reduce its host execution cost.
