# Short Active-Scene Acceptance

## Fixed replay

The [Superstar Chefs fixture](../tests/chefs_active_scene.txt) confirms startup menus, then stops pressing Start before the measured section. From step 2.9 billion it moves right and left and exercises guest A, B and C, separately and with a direction held. These correspond to the default Z/X/C host actions; guest masks avoid keyboard-layout differences. The replay does not assert a particular game-specific effect for every button.

Conditions: fresh settings, zero-filled 2,048-byte EEPROM, legacy ratio 3, normal 60-MIPS pacing, GUI and sound. Stop at four billion instructions. Measure host costs from three to four billion instructions: about 16.7 modeled seconds within an approximately 67-second replay. Both stopped end screens showed the first level, 760 points and four lives. The earlier menu-confirmation fixture left the starting score unchanged and was not used for this pair.

This is an active acceptance scene, not a demonstrated performance failure on this host. No slower hardware was tested.

## One paired result

The [paired record](validation/2026-10-15_chefs_active_scene.csv) compares the immediate-ALU candidate with its original reference. Both use normal GCC `-O3 -march=native` unity builds, not PGO. Executable, cartridge, input and EEPROM hashes are recorded. No compilation or other replay ran concurrently. There was one pair, with no repeats to seek a larger difference.

| Observation | Reference | Candidate |
| --- | ---: | ---: |
| Paced throughput | 60.0004 MIPS | 59.9998 MIPS |
| Section CPU/bus elapsed time | 14,236.9 ms | 12,500.0 ms |
| Section GUI time excluding wait | 78.563 ms | 102.184 ms |
| Whole-run empty audio queues | 0 | 0 |
| Whole-run dropped samples | 0 | 0 |

Final CPU, SDRAM and framebuffer hashes match. The [reference intervals](validation/2026-10-15_chefs_active_baseline.csv) and [candidate intervals](validation/2026-10-15_chefs_active_candidate.csv) retain pacing and audio counters. Host presentation/change rates vary with sampling; their difference is not an increase in game FPS.

The CPU/bus timer measures elapsed time inside execution calls, including host scheduling delays, not process CPU time. It is about 12.2% lower in this section. Both builds already reach the pacing cap. This supports a possible increase in execution reserve, but does not establish repeatable throughput, smoother gameplay, better sound or old-device performance. Whole-process CPU and elapsed times in the archived pair include manual end-screen inspection of different durations and must not be compared as build performance. Subsequent tool output leaves `cpu_seconds` blank during inspection and labels the inclusive value separately.

The earlier 4.4% unpaced startup result remains separate. Do not add these percentages or present either as a general gameplay speedup. This task did not replace the installed PGO executable.

## Bounded procedure

```powershell
python tools/compare_gui.py `
  --exe <candidate.exe> --baseline-exe <reference.exe> `
  --rom 'roms/games/Superstar Chefs [G] (EN).bin' `
  --input-script tests/chefs_active_scene.txt --nvram <blank-2048-byte-eeprom> `
  --output <new-directory> --steps 4000000000 --ratio 3 `
  --host-cost-start 3000000000 --inspect-end --timeout 120 `
  --max-empty-queues 0 --max-dropped-samples 0
```

Inspect the earliest screen that can expose the relevant failure. If it fails, close the window immediately. The tool checks instruction-limit completion before reading result artifacts, so early closure prevents the next replay. At normal completion, inspect the stopped level and close its window to continue. Inspection belongs inside the 120-second per-process limit. Optional audio thresholds reject a completed run before the next replay; they are not continuous monitoring or listening acceptance. A timeout kills the child and stops the comparison. Historical longer scenes need an explicit timeout up to 600 seconds within a separately agreed budget.

Four cartridge-free subprocess checks cover matching results, early closure, an exceeded audio threshold and timeout handling: `python tests/test_compare_gui.py`.

## Next decision

Use this fixture for the next CPU candidate. Define the benefit first: more unpaced execution reserve in this active section, or a reproducible pacing/audio failure. Use matching build profiles and one short pair. A capped 60-MIPS pass alone is compatibility evidence. Listening, motion smoothness and original-hardware speed remain separate checks.
