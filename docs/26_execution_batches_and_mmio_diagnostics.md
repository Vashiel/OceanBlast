# Execution Batches and Guest Register Diagnostics

## Host Execution Overhead

A native thread sample of the preceding Windows executable assigns 19.52% of 2,480 executable samples to `main`, alongside 24.35% to CPU stepping and 21.73% to ARM instruction decoding. The run contains startup and title activity. Attribution uses executable symbol intervals because this binary has no source debug information; it is a coarse host profile, not instruction-level or original-hardware timing.

The execution loop now groups host bookkeeping into bounded batches. Each guest instruction still calls the ordinary CPU step function. Interrupt checks, faults, MMU accesses, timer progression and consumed DMA samples remain instruction-by-instruction. Legacy mode retains its peripheral phase across batch boundaries. Register-clock mode retains its fractional clock conversion and checks the programmed execution clock before every instruction.

Input events, snapshots, display transition probes, instruction limits and GUI polling constrain each batch. Register-clock batches also yield at the modeled GUI deadline, at the modeled stop time and immediately after a wait-for-interrupt instruction. The outer loop retains bounded idle advancement and interrupt wakeup. Debugging, tracing, exception logging, PC sampling and paused execution use the scalar path. Normal execution no longer updates the diagnostic PC history for every instruction; enable debugging when that history is needed.

`--execution-batch 1` selects scalar execution for comparison. The default is 4,096 instructions, reduced by any earlier boundary. This option changes host execution organization, not the selected CPU/peripheral ratio or clock model.

## Controlled Comparison

`tools/compare_execution.py` runs scalar and batched execution with the same executable, cartridge, input script and optional initial EEPROM image. It compares the complete final SDRAM, final CPU dump, effective framebuffer and the preceding snapshot's registers, timer, DMA, interrupt and clock state. Each repetition reverses execution order. Windows process CPU time is recorded separately from elapsed wall time so a suspended or interrupted session can be identified. Runs are headless and do not establish audible or interactive acceptance.

```powershell
python tools/compare_execution.py --exe bin/oceanblast.exe `
  --rom "roms/games/Superstar Chefs [G] (EN).bin" `
  --output build/execution-comparison --steps 1200000000 `
  --repeat 2 --input-script tests/start_buttons.txt
```

Use `--timing auto` for the separate register-clock experiment. ROMs, captures, memory dumps and local session paths remain outside published validation data.

The [legacy comparison](validation/2026-10-09_execution_legacy.csv) uses 1.2 billion instructions, ratio 2, `tests/start_buttons.txt`, default volatile device initialization and executable SHA-256 `e296a90c1dfb916772c880dd262911fbcf3a5f3bf67d7c701c34116594a4ee59`. All four compared state hashes match for each pair.

| Cartridge | Scalar wall seconds | Batched wall seconds | Scalar CPU seconds | Batched CPU seconds | Wall throughput increase |
| --- | ---: | ---: | ---: | ---: | ---: |
| Superstar Chefs | 42.3834 | 34.7407 | 15.2969 | 11.8906 | 22.0% |
| Wade Hixton's Counter Punch | 39.4826 | 33.6736 | 14.2656 | 11.9844 | 17.3% |
| Crazy Jack | 41.8198 | 34.2809 | 18.2500 | 11.5469 | 22.0% |

These are single paired executions, including initialization, logging and final dumps. They measure execution organization within one binary, not a hardware speed comparison or sustained gameplay FPS. An earlier interrupted experiment is excluded. The [register-clock Chefs pair](validation/2026-10-09_execution_auto.csv) also matches all state hashes; wall time decreases from 48.7385 to 43.9838 seconds, but CPU time changes only from 15.5781 to 15.4844 seconds. The latter does not demonstrate a material reduction in CPU work for automatic timing.

GUI/audio observations precede the separate [host pacing correction](27_windows_host_pacing.md). The [2x Chefs run](validation/2026-10-09_chefs_2x_gui.csv) averages 32.78 MIPS, 81.94% of its pacing target and 2.08 sampled framebuffer changes/s over 36.03 recorded wall seconds, ending with 169 empty-queue observations and zero dropped samples. The [automatic run](validation/2026-10-09_chefs_auto_gui.csv) averages 28.47 MIPS and 24.26% modeled speed over 65.16 recorded seconds, ending with 307 empty-queue observations. The [2x Wade run](validation/2026-10-09_wade_2x_gui.csv) also has empty-queue observations; compilation overlapped part of it. These results retain the reported audio/performance problem rather than establishing a cure. GUI replay scripts are `tests/chefs_ratio2_confirm.txt` and `tests/chefs_auto_confirm.txt`; these modes use different instruction schedules and do not constitute a matched scene-speed comparison.

## Guest Register Access Profile

`--mmio-profile` writes `mmio_profile.csv` at exit and cumulative `snapshot_<steps>_mmio.csv` files at snapshot boundaries. Addresses are physical register addresses. Counts cover calls into the register decoder from guest memory operations; direct NAND data streaming bypasses this decoder. Split byte accesses may produce more than one decoder call per guest instruction. These counts are not bus-cycle measurements.

Host inspection through `getMmio`, including display polling, audio diagnostics and snapshot collection, is excluded. This distinction permits testing whether guest code repeatedly polls LCD or sound registers without confusing host rendering with guest behavior. Profiling is optional and disabled by default. Its additional counter work makes profiled runs unsuitable for isolated throughput comparisons.

```powershell
bin/oceanblast.exe "roms/games/Superstar Chefs [G] (EN).bin" `
  --steps 2000000000 --snapshot-interval 500000000 --mmio-profile
```

`tools/sample_native.cpp` is an optional Windows x64 host sampler. It samples the emulator's oldest thread, resumes it after every capture attempt and records executable-relative instruction addresses. `tools/summarize_native.py` resolves those addresses using source symbols when available, otherwise symbol intervals. Sampling temporarily suspends the execution thread; do not combine it with an audio-quality or isolated speed measurement.

## Regression Coverage and Limits

All ten regression executables pass, with 212 passing checks. Added checks cover every legacy peripheral phase at ratios 1, 2, 4 and 16; split batch boundaries; timer state; unmasked interrupt entry and exception return; every consumed DMA sample and DMA completion state; register-clock conversion; modeled deadline yield; wait-for-interrupt yield; and exclusion of host reads from MMIO counters.

Reducing host overhead cannot correct an inaccurate CPU cycle model, insufficient guest execution per peripheral deadline, a decoder failing to submit frames, or corrupted source data. The automatic timing model remains experimental. Smooth game animation, waveform fidelity and sustained synchronized episode playback require separate validation.
