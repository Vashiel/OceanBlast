# Gameplay Work and CPU Execution Cost

## Superstar Chefs Work Distribution

Superstar Chefs can maintain uninterrupted host audio at a 2x instruction allowance while producing few image updates. The guest continues computing. Increasing host presentation frequency cannot supply the execution needed by its software mixer and drawing routines.

Two headless replays use the same initial EEPROM image and proportionally timed button scripts, with 2x and 4x instruction allowances. Each covers 60 seconds of the existing peripheral-time model. The [gameplay samples](validation/2026-10-09_gameplay_work.csv) cover guest seconds 40 through 60, after both captures show the first level. Sampling occurs every 1,009 instructions and records the current translation table as well as the program counter. Different ratios produce different guest states; these are workload observations rather than matched-frame speed measurements.

| Guest code page | Observed operation | 2x instruction samples | 4x instruction samples |
| --- | --- | ---: | ---: |
| `0x5d000` | Signed PCM interpolation and mixing | 60.75% | 32.35% |
| `0xb000` | Drawing path including four RGB565 source pixels averaged into one output pixel | 11.46% | 31.85% |
| `0x11000` | Drawing path including color-keyed halfword copies | 5.01% | 13.95% |

The sampled translation table is `0x30f0c000`. Local instruction inspection identifies the averaging loop around `0xb8c8` and the conditional pixel-copy loop around `0x11ac0`. Whole-page counts include surrounding routines. They are not cycle counts or complete function attribution.

At 100 framebuffer polls per modeled second, the same gameplay intervals record 3.40 and 18.85 memory changes per second respectively. Partial drawing can produce multiple changes, and a poll can miss updates. These numbers are not complete game frames or LCD refresh rates. Startup and asset decompression materially affect earlier profiles: at 2x the captured loading screen remains visible at 22 modeled seconds, whereas the 4x replay has reached the level by that point.

## Execution Changes

The subsequent [fetch and interpreter optimization](37_cpu_fetch_and_interpreter_execution.md) extends the ALU path to flag updates and constant register shifts. The measurements in this document describe the earlier build below.

Common ARM data-processing operations without flag updates now have a shorter execution path. It covers immediate operands and unshifted register operands for AND, EOR, SUB, RSB, ADD, ORR, MOV, BIC and MVN, with a general-register destination. Operations involving flags, carry arithmetic, shifted registers or a PC destination retain the ordinary implementation. PC source operands retain the existing pipeline offset.

Every instruction still performs its normal memory fetch and condition evaluation. Interrupt checks, MMU permissions, exception boundaries and peripheral advancement are retained. This is an interpreter optimization, not a guest instruction cache, code substitution or hardware timing adjustment. `--simple-alu off` selects the general execution path for comparison.

The profile-guided builder also supports `-Unity` and `-Optimization O3`. A unity build compiles the existing source files in one translation unit, allowing optimization across CPU, bus and execution-loop calls without requiring compiler LTO support. Training can include `-InputScript` and `-CpuStepsPerTick` so that gameplay contributes to the profile. Generated source, profiles, EEPROM copies and cartridge captures remain under ignored `build/`.

The tested executable combines the shorter ALU path, O3 unity compilation and training on Chefs, Wade and DigiQUAD. The measurements below apply to that combination; they do not isolate the benefit of each component or describe the ordinary `make` build.

## Controlled Comparisons

The baseline executable has SHA-256 `2c545567da9fd8d9188b9e333a738c0e807e8fe4bfe13fb7d885fc969b73b90e`; the optimized executable is `b8c450c03fc76c9cdec933ca5a242ec4e2fab310e8011eb4690fa32f6ca7ea3d`. Each comparison repeats twice, reversing execution order, with identical instruction budgets, input scripts and independent copies of the initial EEPROM state. No other cartridge test runs concurrently.

| Cartridge | Allowance | Mean baseline MIPS | Mean optimized MIPS | Throughput increase |
| --- | ---: | ---: | ---: | ---: |
| Superstar Chefs | 4x | 57.11 | 72.55 | 27.05% |
| Wade Hixton's Counter Punch | 2x | 60.82 | 75.25 | 23.73% |
| DigiQUAD | 2x | 62.73 | 76.00 | 21.15% |

Chefs executes 2.4 billion instructions with `tests/chefs_ratio4_confirm.txt`; Wade and DigiQUAD execute 1.2 billion with `tests/start_buttons.txt`. All six paired repetitions produce identical complete SDRAM, final CPU, framebuffer and penultimate snapshot hashes. [Chefs records](validation/2026-10-09_execution_chefs4.csv) and [Wade/DigiQUAD records](validation/2026-10-09_execution_wade-digi.csv) include process CPU time, wall time and content identities.

Bounded GUI/audio comparisons use the same guest inputs and the high-resolution host timer. Their final CPU, complete SDRAM and framebuffer hashes also match across builds.

| Chefs allowance | Build | Modeled speed target reached | Sampled changes per wall second | Empty-queue observations | Dropped samples |
| --- | --- | ---: | ---: | ---: | ---: |
| 4x | Baseline | 65.87% | 9.18 | 599 | 0 |
| 4x | Optimized | 89.12% | 12.09 | 35 | 0 |
| 2x | Baseline | 100.00% | 2.57 | 0 | 0 |
| 2x | Optimized | 100.00% | 2.53 | 0 | 0 |

The [4x GUI records](validation/2026-10-09_execution_gui4.csv) show a substantial reduction in observed audio starvation, with residual empty queues. The [2x records](validation/2026-10-09_execution_gui2.csv) retain uninterrupted queues. The `seconds` field in `performance.csv` is an individual reporting interval, not a cumulative clock; aggregate rates must use the sum of those intervals. `tools/compare_gui.py` uses this convention and closes both windows at their instruction limits.

All ten regression executables pass with 214 checks. Added CPU comparisons cover 8,192 data-processing cases, PC operands, flag/cycle preservation and every condition against all NZCV combinations. The clocked execution and interrupt/DMA suites retain their existing coverage.

The [optimized-build cartridge audit](validation/2026-10-09_execution_games.csv) repeats the eleven game cartridges at two billion instructions with the generic button script. All exit normally without guest segmentation faults or kernel panics. These bounded checks retain the preceding compatibility limitations; they do not establish playable or synchronized gameplay for every cartridge.

## Reproduction and Remaining Work

```powershell
make test
./tools/build_profiled.ps1 -Roms @(
  "roms/games/Superstar Chefs [G] (EN).bin",
  "roms/games/Wade Hixton's Counter Punch [G] (EN).bin",
  "roms/games/DigiQUAD [G] (EN).bin"
) -Steps 2400000000 -CpuStepsPerTick 4 -Unity -Optimization O3 `
  -InputScript tests/chefs_ratio4_confirm.txt -DeviceSettings initial.nvram
```

Use the returned executable for paired validation before installing it. `initial.nvram` denotes an independent 2,048-byte device-state image. Compiler selection remains available through `-Compiler`.

```powershell
python tools/compare_execution.py --exe candidate.exe --baseline-exe baseline.exe `
  --comparison executable --rom "roms/games/Superstar Chefs [G] (EN).bin" `
  --steps 2400000000 --ratio 4 --repeat 2 --output build/execution-comparison `
  --input-script tests/chefs_ratio4_confirm.txt --nvram initial.nvram
make build/scene_work_probe.exe
build/scene_work_probe.exe "roms/games/Superstar Chefs [G] (EN).bin" `
  1200000000 4 build/scene-profile initial.nvram tests/chefs_ratio4_confirm.txt
```

The scene probe writes per-interval CPU-mode and code-page observations, individual PC samples and framebuffer-change gaps. PCM sample totals are cumulative. Memory and image captures remain local. Non-user CPU modes are grouped in the `kernel_samples` column; a mode count alone does not identify a kernel routine.

The optimized 4x GUI run remains below its 80-MIPS target. Faster execution does not determine the original console's instruction allowance, complete the cycle model or establish waveform fidelity and synchronization. Automatic register-clock timing remains experimental. Further work should retain scene-specific profiles and matched guest-state checks while improving CPU execution and validating cache, memory and pipeline timing against hardware.
