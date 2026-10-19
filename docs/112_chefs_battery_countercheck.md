# Chefs battery counter-check

Date: 2026-10-19. Repeats the [installed/JIT resource comparison](111_chefs_resource_comparison.md) on battery at the user's request. Budget: 10 minutes, one pair. No rebuild, settings retuning, additional cartridge or repeated performance sweep.

## Result

**The packaged JIT fails the short battery audio-delivery gate.** It records 23 empty-queue events while the installed interpreter records zero. Both finish the same instruction budget and preserve the same guest state. Lower whole-process CPU time does not compensate for the observed delivery failure.

| Power / executable | Whole-process CPU | Whole-process wall time | Empty queues | Dropped samples |
| --- | ---: | ---: | ---: | ---: |
| AC, installed interpreter | 23.6094 s | 33.8768 s | 0 | 0 |
| AC, packaged JIT | 20.3281 s | 29.1750 s | 0 | 0 |
| Battery, installed interpreter | 23.1875 s | 31.3169 s | 0 | 0 |
| Battery, packaged JIT | 21.8594 s | 30.1246 s | 23 | 0 |

In the battery pair, the JIT uses 5.73% less total CPU time. This single-pair package observation includes startup and shutdown, with different compiler profiles; it does not establish a steady-state engine saving. The earlier favorable AC result remains valid for its recorded workload, but it did not demonstrate sufficient reserve or consistent audio delivery on battery.

## Controls and state agreement

Windows reports AC-line status 0 throughout both runs. Battery status changes from 97% before the pair to 96% afterward; the energy-saver flag remains 0 at the recorded status points. This coarse percentage is not an energy measurement. The active power-scheme GUID remains Balanced, matching the AC pair. No system power policy is changed by the test.

Executable, ROM, initial EEPROM, settings and input hashes match the AC pair. Order remains installed then packaged JIT. Both use 1.6 billion instructions, legacy ratio 3, a 60-MIPS host cap, GUI/audio, plain scale-3 presentation and affinity mask 4. Normal saves and installed/user-test binaries are unchanged.

All dumped architectural registers, complete SDRAM and active framebuffer match between both battery runs and the preceding AC pair. The framebuffer therefore retains the previously inspected first-level image with five lives. State agreement establishes matching guest work in this fixture; it does not negate missed host audio deadlines.

## Where delivery fails

The JIT empty-queue count first rises to 4 at the 338.60-million-step report, then to 6, 7, 20 and finally 23 at 552.25 million steps. Reported model-speed intervals during those increments include 72.03%, 108.38%, 79.88%, 73.79% and 93.90%. The bursts occur before the scripted first Start event at 700 million steps. The counter does not rise again through the end of the replay; the final four model-speed intervals are 98.18-101.95%.

The installed battery run has no empty queues. Its final four model-speed intervals are 96.55-101.41%. The observation narrows the failure to early JIT delivery in this run. It does not establish whether translation, fallback/device work, host scheduling, pacing or another cause is responsible. Neither average CPU time nor a later 100% model-speed interval proves that an earlier deadline was met.

The [manifest](validation/2026-10-19_chefs_battery.json), [installed intervals](validation/2026-10-19_chefs_battery_installed.csv) and [JIT intervals](validation/2026-10-19_chefs_battery_packaged_jit.csv) record the evidence. Additional process-CPU observations are polled when CSV rows become visible; their boundaries are approximate and do not replace fixed guest-step scene measurements. No listening, full-game, calibrated-console-speed or weak-device acceptance is claimed.

## Consequence

Keep the installed player as the user baseline. The JIT remains a developer test package with an open audio-delivery failure. The common timing contract and cycle-accounted interpreter/JIT timeline remain the next implementation gates. Retain this battery prefix as an additional short acceptance case when that implementation is ready; do not replace it with a lower average-CPU target alone.
