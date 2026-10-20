# Chefs battery memory-path review

Date: 2026-10-20. Authorized budget: 40 minutes. The block exceeded its wall-clock budget; further runtime experiments were stopped. Three bounded memory-path candidates were evaluated and removed. The installed player and earlier manual-test package remain unchanged. No improved test executable is accepted.

## Controls

The completed GUI runs use the same Chefs ROM, initial difficulty-1 EEPROM, replay, settings, legacy ratio 3, 60-MIPS host cap, scale 3, audio output and affinity mask 4. All builds use GCC 16.2, Release O3, conservative x86-64 and PGO OFF in the same build tree. Windows reports battery operation throughout recorded checks; energy saver remains off. Battery percentages are status observations, not energy measurements. Timing calibration remains unverified.

The initial reference attempt produced no performance rows and was forcibly stopped after the 50-second startup watchdog plus its five-second close allowance. Its startup cause was not established. A bounded one-instruction headless diagnostic succeeded; one replacement GUI reference then completed. The failed attempt is excluded from comparisons. Follow-up GUI watchdogs stop a missing startup after ten seconds.

## Completed GUI observations

| Build | Process CPU | Wall time | Empty queues | Dropped samples |
| --- | ---: | ---: | ---: | ---: |
| Unchanged reference | 25.437500 s | 30.618382 s | 7 | 0 |
| Direct SDRAM stores with live page flags | 27.031250 s | 32.279409 s | 30 | 0 |
| Register retention across single-memory operations | 30.500000 s | 35.410572 s | 12 | 0 |

These are single-order observations, not robust estimates of intrinsic engine cost. In particular, GUI startup CPU varies substantially: at the first 60.05-million-step observation the reference has consumed about 2.19 seconds, versus 6.91 for register retention. The larger whole-process difference must not be attributed entirely to the emitter change. None of these runs passes the zero-empty-queue delivery gate.

All three completed GUI runs match architectural register dumps, complete SDRAM and framebuffer. SDRAM SHA-256 is `a28db7797d70311b64c4a6ff92562afd7bd8368923a26f6cd224553609f8d130`; framebuffer SHA-256 is `d880de22570ce11fd9351b5685a8d338d1a1e1e4958e49b1896d2fdbbb8ed6b3`. Matching guest work does not establish stable host delivery or correct original-console tempo.

## Candidate decisions

1. **Direct stores:** a compact live per-page flag table combines code and capture protection before direct ordinary SDRAM writes. This avoids the separate capture-range and code-page guards of the earlier rejected implementation in [103](103_native_coverage_and_sampled_compilation.md). Thirteen suites, including dynamic code watches, capture changes and cached permission checks, pass in 46.52 seconds. The GUI check supplies no performance or audio benefit, so the implementation and public bus additions are removed.
2. **Register retention:** materialize dirty guest state before memory helpers while retaining unaffected cached values; invalidate loaded destinations and writeback bases. Thirteen suites pass in 29.82 seconds. To separate variable GUI initialization from execution, one headless 600-million-instruction pair records 8.109375 versus 8.218750 process CPU seconds (+1.35%), with identical canonical CPU/RAM state and dispatch counts. This supplies no gain large enough to retain the extra state-management contract. Removed.
3. **Multiple-transfer translation reuse:** reuse a checked ordinary-RAM mapping only within one instruction and one 1-KiB permission span, rechecking the first word of each new span. Existing behavior and the focused boundary test agree with the interpreter. One headless 600-million-instruction pair records 5.765625 versus 5.937500 CPU seconds (+2.98%), with identical canonical state and dispatch counts. Removed without another GUI sweep. Absolute timings across separate pairs vary and must not be combined.

The final restored implementation plus this regression test passes all 13 suites in 30.81 seconds. The independent regression test remains: LDM/STM crossing a small-page 1-KiB permission boundary must retain earlier partial effects, report the faulting address and suppress writeback; a fully permitted mapping must complete. It checks architectural behavior and does not require the rejected optimization.

## Host profile and next boundary

A separate 3.5-second reference sample, started three seconds after launch, uses the exact reference executable and its own symbols. It contains 819 samples, including 69 outside the executable module. Nearest-text-symbol attribution is approximate: `ARM920T::step` accounts for 16.97% of all samples, JIT `run` 14.04%, `stepARM` 6.59%, `executeNative` 6.23%, `Bus::tick` 5.25%, prefix materialization 3.42% and checked multiple transfer 2.69%. Generated native code and libraries outside the main module remain unclassified. Sampling suspends the target and is excluded from timing results.

This points to shared reference/native dispatch and event-boundary work as a more useful investigation boundary than another standalone direct-store variation. It does not prove a proposed replacement is faster. The accepted [common timing plan](110_common_timing_model_and_calibration.md) and [clocked-native opt-in](113_clocked_native_timeline.md) remain in force; remaining timing costs, calibrated speed and production audio reserve are still open. No preset, timing ratio, audio-buffer or system power-setting change is used to mask the failure.

The [manifest](validation/2026-10-20_chefs_battery_memory.json) and companion CSVs preserve hashes, power observations, the invalid attempt, completed checks and profile limitations. External cartridges, saves, memory dumps and candidate executables remain outside version control.
