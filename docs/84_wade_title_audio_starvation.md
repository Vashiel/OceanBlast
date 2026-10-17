# Wade Title Audio Starvation

## Report and reproduction

Local playback feedback reports visibly faster emulation with intermittent title music: sound recovers, then stutters again after several seconds. The affected cartridge is Wade Hixton's Counter Punch. The previous short paced Chefs check did not establish paced Wade audio acceptance.

One initial scale-4 attempt closes before the requested limit and is excluded. Three subsequent short title runs complete three billion instructions each, with ratio 6, the normal 120-MIPS cap, GUI/audio, plain scale 2, fresh frontend settings and the same copied device EEPROM. No buttons are pressed. The EEPROM and guest dumps remain outside source control. All three runs produce identical final CPU, SDRAM and framebuffer hashes.

The installed executable repeatedly exhausts its audio queue while missing the pacing target. This reproduces an output-supply problem; it does not prove that every audible defect has the same cause. A larger buffer can bridge temporary scheduling delays but cannot supply a sustained shortage of generated samples.

## One build-profile candidate

The preceding PGO executable was trained with Chefs and Rayman 3. One candidate retains those two 900-million-instruction training runs and adds three billion Wade instructions. All training uses legacy ratio 3; the actual Wade title checks use its normal ratio 6. Compiler, O3, unity and conservative x86-64 settings remain the same. Runtime source is unchanged from `759f567`.

The profile is generated with an isolated copy of the build script using the additional per-cartridge limit. The maintained script now accepts equivalent limits through `StepsPerRom`:

```powershell
./tools/build_profiled.ps1 -Roms @(
    'roms/games/Superstar Chefs [G] (EN).bin',
    'roms/games/Rayman 3 [G] (M10).bin',
    "roms/games/Wade Hixton's Counter Punch [G] (EN).bin"
) -StepsPerRom @(900000000,900000000,3000000000) `
  -CpuStepsPerTick 3 -Optimization O3 -Architecture x86-64 -Unity
```

Compilation/training and playback checks are sequential. The task allowance is 20 minutes. Because host throughput changes substantially, one return to the previous executable follows the candidate. No further profile variants or benchmark repetitions are tried. AC/Balanced status is observed during diagnosis; continuous power/clock telemetry is not recorded for these title runs. The results are bounded observations, not a controlled universal speedup claim.

| Completed title run | Sampled throughput | Late title mean | Observed empty queues | Dropped samples |
| --- | ---: | ---: | ---: | ---: |
| Previous executable, first run | 99.99 MIPS | 110.12 MIPS | 138 | 0 |
| Additional Wade training | 119.94 MIPS | 120.88 MIPS | 6 | 0 |
| Return to previous executable | 115.63 MIPS | 116.07 MIPS | 25 | 0 |

Late means include one-second rows whose ending instruction count is at least 1.4 billion; they are not identical fixed-scene timing windows. Short catch-up intervals can exceed the 120-MIPS cap. Audio counts are cumulative at the last recorded interval, not guaranteed complete-run counts. [Summary](validation/2026-10-17_wade_audio_summary.csv), per-run CSV/JSON records and [build metadata](validation/2026-10-17_wade_audio_pgo_build.json) retain executable and fixture identities.

The candidate has fewer observed empty queues than either surrounding reference run. Its last new empty-queue event occurs before the late title interval, followed by no additional observed events through the end. Nevertheless, six events remain: the startup stutter is not fixed, and no listening or audiovisual-sync acceptance is claimed. Host variability prevents attributing the entire initial improvement to PGO.

## Decision and remaining work

Retain the additional Wade training after the short paced Chefs counter-check. It improves this observed title sequence without changing guest state or runtime semantics. The new local executable's SHA-256 is `ea41ea3cdc16b63b64759d18b4ea951fd09d2c84110f7effa6dce0de7a4bee44`; its predecessor is backed up before replacement. No new release package is uploaded.

The [Chefs counter-check](validation/2026-10-17_wade_trained_chefs.json) replays the existing four-billion-instruction movement/A/B/C fixture at normal ratio-3 pacing. It averages 59.9697 MIPS with zero empty queues, zero drops and matching CPU/SDRAM/framebuffer hashes. This check exits automatically; no new visible inspection or listening check is performed. Compilation, the short title observations, this counter-check and publication take about ten minutes within the allowance. The preceding executable remains in ignored `build/launcher-backup-2026-10-17-before-wade-training/`.

Treat Wade's remaining startup/audio starvation as an open P0 issue, ahead of further throughput experiments or weak-hardware acceptance. The next bounded audio task should distinguish guest audio gaps/rate changes from host scheduling stalls, then evaluate startup/recovery buffering against those gaps without obscuring sustained CPU deficits or introducing unmeasured sound latency. Existing interpreter speed improvements remain separate evidence.
