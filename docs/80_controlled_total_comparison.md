# Controlled Before/After Comparison

## Question and source boundaries

Do the retained changes after the instruction-mix investigation produce a combined benefit when compared with the earlier interpreter under more consistent host conditions? Previous single-pair percentages varied with power conditions and must not be added together.

The reference is revision `6c7870f`, before the instruction-mix work and subsequent retained shortcuts. The current source is `28ef4b3`, whose runtime code is unchanged from `2880b0d`. The difference includes the register no-flags ALU path, word and halfword transfer shortcuts, combined instruction-fetch check, MMU/data-access streamlining and zero-tick peripheral bypass. Both versions already contain the earlier immediate no-flags path. Rejected experiments are absent from both production versions.

## Build and host controls

Both executables were freshly built from separate Git archives using the same GCC 16.2.0 toolchain, normal `-O3 -march=native -g` unity configuration and Makefile inlining parameters. Neither is PGO. The [manifest](validation/2026-10-16_total_wade_manifest.json) records revisions, exact executable/cartridge/input/EEPROM hashes and compiler details. No installed launcher was replaced.

Host: Intel Core i7-1185G7, four physical cores and eight logical processors. Windows initially reported battery operation; the user connected AC before measurement. All four emulator processes inherit affinity mask `4` (logical processor 2) from their common harness. Actual affinity was also read back from the first emulator process. This constrains migration and gives both versions the same processor; it does not simulate an older CPU.

Windows reported AC power and the Balanced scheme (`381b4222-f694-41f0-9685-ff5bb260df2e`) in all 148 [power observations](validation/2026-10-16_total_wade_power.csv) across 416.401 seconds. The observer requested one-second sampling, but actual gaps reached 16.741 seconds; the log therefore documents observed conditions rather than proving that no momentary transition occurred between readings. No power setting was changed. Host aggregate CPU utilization had a median of 13.578% across all eight logical processors; this is not the emulator's own CPU-utilization metric.

Frequency, turbo, temperature and OS scheduling were not locked. Reversed order and repeated results reduce dependence on a simple time trend but do not remove all host variation. No compilation, other emulator replay, screenshot capture or manual scene inspection ran during the timed tests.

## Fixed workload and order

All four runs use Wade Hixton's Counter Punch, ratio 6, `tests/wade_ratio6_fight.txt`, a zero-filled 2,048-byte EEPROM, isolated default settings, GUI/audio enabled and disabled host pacing. The executable stops and exits automatically at 8.5 billion instructions. Only the 7-to-8.5-billion-step fight section defines scene throughput. Whole-process CPU time covers the complete replay and shutdown; it includes no manual inspection wait.

Two predefined pairs run in the order **reference, current, current, reference**. There were no additional repetitions after seeing the results. The fixture confirms menus and reaches the fight, but injects no further player actions during the measured fight section. No fresh visible-motion or listening acceptance is claimed by this performance-only task.

## Results

The [four-run record](validation/2026-10-16_total_wade_pairs.csv) and [summary calculations](validation/2026-10-16_total_wade_summary.json) retain the results.

| Pair and order | Reference fight MIPS | Current fight MIPS | Throughput gain | Reference process CPU | Current process CPU | CPU-time reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1: reference then current | 82.4633 | 107.5492 | 30.42% | 114.4062 s | 86.0625 s | 24.77% |
| 2: current then reference | 80.6990 | 108.0334 | 33.87% | 113.3750 s | 89.5312 s | 21.03% |
| Arithmetic means per version | 81.5812 | 107.7913 | 32.13% | 113.8906 s | 87.7969 s | 22.91% |

The last row compares the mean measurements per version; it does not add individual optimization percentages. Both orderings favor the current code in scene throughput and whole-run process CPU time. Reference scene rates differ by about 2.2%, and current rates by about 0.45%, considerably less than the measured before/after gap in these runs.

All four runs have identical final CPU (`f8c2d85935b413491d89beaccda77d33c3e6eb6a9e9ab06a0b340c853cde0bf7`), SDRAM (`ed7b0b7f20fe5b22973c8561ad8b59d2206504d185fe2845f7fd17cabf7886a5`) and active framebuffer (`3c2ddd71d41804001c6f240373bc4976cb9f56c7894ce114f6786877afbf6ea9`) hashes.

Per-run intervals: [pair 1 reference](validation/2026-10-16_total_wade_0_baseline.csv), [pair 1 current](validation/2026-10-16_total_wade_0_candidate.csv), [pair 2 current](validation/2026-10-16_total_wade_1_candidate.csv), [pair 2 reference](validation/2026-10-16_total_wade_1_baseline.csv).

The reference runs report 1,364 and 1,394 empty-queue events; current runs report 108 and 116. All report zero dropped samples. These counters accompany unpaced execution, not a normal-paced listening test. Even the current fight throughput remains below the configured 120-MIPS target. Fewer empty queues are not a complete sound-quality or full-speed acceptance result.

## Reproduction

Build both archived revisions with the same compiler/options. Start the comparison from a process with affinity mask `4`, keep AC and the Balanced profile, and use the same starting EEPROM bytes:

```powershell
python tools/compare_gui.py `
  --baseline-exe <fresh-reference.exe> --exe <fresh-current.exe> `
  --rom "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" `
  --input-script tests/wade_ratio6_fight.txt --nvram <zero-filled-2048-byte-file> `
  --output <new-directory> --steps 8500000000 --ratio 6 `
  --host-cost-start 7000000000 --unpaced --repeat 2 --timeout 300
```

Omit `--inspect-end` so process CPU/wall times do not include manual waiting. Power logging is separate from `compare_gui.py`; it was collected with Windows `GetSystemPowerStatus`, `PowerGetActiveScheme` and `GetSystemTimes`. The observer aborts its comparison process tree if a sampled power source/profile changes. Both fresh builds finished before measurement began. Total task allowance was 30 minutes; no extra tests or launcher build were added after the predefined comparison.

## Conclusion

The combined retained code changes deliver a useful, repeatable short-run benefit in this Wade scene on this host: approximately 32% more scene throughput and 23% less complete-replay CPU time. This is stronger evidence than the earlier isolated single pairs, but two pairs are not a statistical performance study. It does not verify every individual claimed percentage, establish the same benefit for other cartridges or old hardware, or measure the installed PGO executable. The earlier single-pair reports remain historical observations. The runtime code and installed launcher were left unchanged by this verification task.
