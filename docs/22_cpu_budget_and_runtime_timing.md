# CPU Budget and Runtime Timing

[Register-derived clocks and IIS pause/resume](23_register_clocks_and_iis_pause.md) replace the fixed peripheral-clock and nominal audio-rate assumptions. IRQ entry now advances its supplied peripheral budget; the early-return limitation described in the original probe below no longer applies to new runs. The CPU work ratio remains an approximation.

Wade Hixton's Counter Punch still shows slow and uneven animation after reported audio improvement. Audio quality and animation speed require separate validation. A recent standard GUI session averaged 20.01 MIPS and 11.08 sampled framebuffer changes per second over its last 30.07 seconds, with zero dropped samples and zero empty-queue observations. Reaching the instruction limit does not establish correct console speed.

## Separating CPU Work from Peripheral Time

`--cpu-steps-per-tick N` executes N interpreter steps for each scheduled peripheral tick. The default remains 1; supported values are 1–16. Without an explicit `--clock-mips`, the GUI instruction limit becomes 20 times N MIPS, preserving the nominal 20-million-tick time base when the host keeps up. This is an experimental diagnostic, not a cycle-accurate hardware clock. Existing interrupt-entry early returns can skip scheduled peripheral advancement.

The launcher exposes **CPU Timing Test** with Standard, 2x and 4x options. An explicit `--clock-mips` overrides the automatic instruction limit and can change peripheral speed. Instruction-count limits, snapshots and input scripts count CPU steps, so matched peripheral-time comparisons must scale their instruction budgets.

## Controlled Startup Comparison

[Probe results](validation/2026-10-08_wade_cpu_ratio.csv) use the same cartridge and initial EEPROM image, no input, and 600 million scheduled ticks per run (30 seconds in the nominal model). Framebuffer snapshots at five-second intervals show:

| CPU steps per tick | Presentation at 30 model seconds |
| --- | --- |
| 1 | Engine logo |
| 2 | Inferno logo |
| 4 | Counter Punch title |

Additional CPU work advances startup while timer/DMA time remains approximately fixed. This supports insufficient CPU work relative to peripheral time as a contributor to slow presentation. It does not determine the correct ratio for original hardware. Different startup phases also make cumulative PCM counts and framebuffer-change totals unsuitable as direct quality or gameplay-rate comparisons.

`runtime_probe` polls framebuffer memory and PC pages at 100 Hz of model time. Hash changes can include partial rendering or padding changes; they are not completed frames or guest vertical synchronization. Periodic PC sampling can alias. Raw guest snapshots remain local.

```powershell
make build/runtime_probe.exe
build/runtime_probe.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" 600000000 2 build/wade_ratio2.csv build/initial_board.nvram
```

The optional EEPROM input must contain the same initial device state for each comparison.

## Host Throughput Constraint

[The 2x GUI trace](validation/2026-10-08_wade_ratio2_gui.csv) targets 40 MIPS. Later intervals achieve approximately 27–33 MIPS and accumulate repeated empty audio-queue observations, with zero dropped samples. Raising the CPU budget alone can therefore improve guest progress per peripheral tick while causing real-time audio starvation. The standard setting remains the default.

A matched 600-million-step headless comparison completed in 20.45 seconds with the normal O2 build and 25.12 seconds with an experimental O3 unity build. Final SDRAM and framebuffer hashes matched. These are single-run measurements, not a statistical benchmark; the slower build is not distributed. The available gprof run produced no samples or call data and cannot identify an interpreter hotspot.

Regression checks exercise 1x, 2x and 4x CPU budgets with an identical peripheral-tick budget: DMA output, completion state and Timer 4 results match, while CPU execution advances according to the ratio. All nine regression suites pass. This validates diagnostic scheduling, not cartridge speed or audio acceptance.

## Remaining Correction Work

Establish CPU, peripheral and LCD timing against original-hardware startup and gameplay. Measure interpreter hotspots with a working sampling profiler before optimizing dispatch, memory translation or peripheral processing. Preserve final-state equivalence in matched runs and repeat the GUI comparison with sound enabled. Acceptance requires correct animation duration, responsive controls and continuous audio in the same scene; a higher MIPS or memory-change count alone is insufficient.
