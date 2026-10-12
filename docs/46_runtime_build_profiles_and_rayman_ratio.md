# Runtime Build Profiles and Rayman 3 CPU Ratio

## Runtime Build Performance

The normal developer executable and the installed gameplay executable use different compiler profiles. A standard `-O3 -march=native` unity build is suitable for development, while gameplay should use the profile-guided optimization (PGO) unity build produced by `tools/build_profiled.ps1`.

On the same host, bounded 600-million-step runs produced the following approximate throughput:

| Cartridge | Developer build | PGO build |
| --- | ---: | ---: |
| *Superstar Chefs* | `77.4 MIPS` | `113.0 MIPS` |
| *Rayman 3* | `78.1 MIPS` | `119.9 MIPS` |

The live *Superstar Chefs* comparison used the same 60-modeled-second run, ratio `3`, audio enabled, and `tests/chefs_auto_confirm.txt`:

| Metric after startup | Developer build | PGO build |
| --- | ---: | ---: |
| Mean emulated speed | `96.93%` | `100.00%` |
| Cumulative empty audio-queue observations | `55` | `0` |
| Dropped output samples | `0` | `0` |

The measured presented frame rates averaged about `19.7` and `19.8` FPS respectively. The material difference was pacing and audio-queue stability: the developer build fell below the requested `60 MIPS` during parts of the run, while the PGO build maintained its target. Use `-Install` when a PGO build should replace `bin/oceanblast.exe`; plain `make` now writes `build/oceanblast-dev.exe` and cannot overwrite the installed launcher.

## Rayman 3 Automatic Ratio

Live GUI and audio runs with the same ROM, PGO executable, display mode, and host showed that ratio `3` did not provide enough guest work for the desired frame rate. Raising only Rayman 3 to ratio `4` improved the presented frame rate in the intro/title interval while retaining real-time pacing:

| CPU ratio | CPU target | Mean presented FPS (`16–34` modeled seconds) | Mean emulated speed | Empty audio queues | Dropped samples |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `3` | `60 MIPS` | `17.7` (`18` nonzero samples) | `100.00%` (minimum `99.95%`) | `1` | `0` |
| `4` (automatic) | `80 MIPS` | `30.5` (`19` nonzero samples) | `100.00%` (minimum `97.06%`) | `4` | `0` |

At automatic ratio `4`, the cumulative empty-queue counter reached `1` during startup and `4` by modeled second `33`; no output samples were dropped. Audio listening acceptance remains open. Ratio `4` is a host-tested compatibility setting, not a measurement of the original console's exact instruction throughput.

The complete live metrics are available in:

* [Superstar Chefs developer build](validation/2026-10-12_chefs_developer_live.csv)
* [Superstar Chefs PGO build](validation/2026-10-12_chefs_profiled_live.csv)
* [Rayman 3 ratio 3](validation/2026-10-12_rayman_ratio3_live.csv)
* [Rayman 3 ratio 4](validation/2026-10-12_rayman_ratio4_live.csv)
* [Rayman 3 installed automatic profile](validation/2026-10-12_rayman_auto4_live.csv)

## Reproduction

Build a PGO executable from representative game cartridges and install it:

```powershell
./tools/build_profiled.ps1 `
  -Roms @('roms/games/Superstar Chefs [G] (EN).bin', 'roms/games/Rayman 3 [G] (M10).bin') `
  -Steps 2100000000 -CpuStepsPerTick 3 -Optimization O3 -Unity -Install
```

The installed launcher applies the per-cartridge automatic ratio. Explicit CPU-ratio command-line options and manual launcher settings continue to override automatic selection.
