# Chefs resource comparison: installed player and JIT package

Date: 2026-10-19. Follows the [timing foundation](110_common_timing_model_and_calibration.md) and [manual package](109_manual_jit_package_and_cross_game_checks.md).

## Question and scope

Does the existing JIT test executable need less host CPU for the same configured Chefs work than the installed player? This task is limited to one reference/candidate pair, with a 15-minute total budget. Neither source nor executable is changed. No Wade run, new compilation, reversed repetition or additional cartridge check is appended.

This is a comparison of complete executable packages. The installed interpreter uses GCC 16.2 O3 unity compilation with existing PGO data; the packaged JIT uses GCC 16.2 O3 Release, conservative x86-64 and PGO off. These different profiles prevent attributing the result exclusively to the execution engine.

## Controls

Both runs use the same Chefs ROM, copied difficulty-1 EEPROM, copied settings, step-indexed movement/jump replay, 1.6-billion-instruction limit, GUI/audio, legacy ratio 3, 60-MIPS host cap, plain window at scale 3 and affinity mask 4. AC power remains connected throughout both runs. Normal saves and installed files are preserved. The installed executable starts its interpreter; the candidate explicitly selects JIT with its title shortcut disabled. Final-state comparisons detect differing guest work in this fixture.

The [manifest](validation/2026-10-19_chefs_resources.json) records executable/ROM/input/EEPROM/settings identities, processor, active power scheme, exact options, replay events, order and observations. The [installed intervals](validation/2026-10-19_chefs_resources_installed.csv) and [JIT intervals](validation/2026-10-19_chefs_resources_packaged_jit.csv) retain the raw runtime counters.

## Observations

| Observation | Installed interpreter | Packaged JIT |
| --- | ---: | ---: |
| Whole-process CPU time | 23.6094 s | 20.3281 s |
| Whole-process wall time | 33.8768 s | 29.1750 s |
| Time covered by emitted CSV intervals | 26.1401 s | 26.1677 s |
| Wall time outside emitted intervals | 7.7368 s | 3.0073 s |
| Sampled peak private committed memory | 121.37 MiB | 121.69 MiB |
| Observed working-set high-water mark | 111.45 MiB | 115.88 MiB |
| Empty audio queues / dropped samples | 0 / 0 | 0 / 0 |

Both finish normally at the requested instruction count. All dumped architectural registers, complete SDRAM and active framebuffer match. The SDRAM also matches the retained [EEPROM-fix fixture](91_chefs_eeprom_health.md). The final image is visually inspected and shows the first level with five lives. The last four reporting intervals range from 97.34-103.09% model speed for the installed player and 96.86-102.83% for the JIT. These percentages refer to the existing configured timeline, not verified original-console speed.

The JIT package uses **13.90% less whole-process CPU time in this pair**. Memory does not decrease: sampled private commitment is approximately unchanged, and the working-set high-water mark is about 4.43 MiB higher.

## Interpretation and next gate

The lower CPU total is a favorable observation for this complete short launch-and-play workload. It is not an isolated measurement of CPU savings during the active level. Process CPU time includes initialization, boot, GUI/audio, native-code generation, final dumps and shutdown. The substantially different wall time outside CSV reporting makes initialization, driver/cache and execution-order effects material unresolved factors. The CSV contains no per-interval process CPU samples, so it cannot allocate the saving to gameplay. No additional run is added to turn this single pair into a stronger claim.

No listening, full-game, calibrated-hardware-tempo or weak-device acceptance follows. Keep both existing executables. Further implementation follows the common timing gates T1-T3 before standalone dispatch tuning; future resource comparisons should record process CPU at fixed guest-step scene boundaries to separate startup from active execution.

The subsequent [battery counter-check](112_chefs_battery_countercheck.md) records 23 JIT empty-queue events versus zero for the installed player. The lower AC CPU total does not establish reliable battery audio delivery.
