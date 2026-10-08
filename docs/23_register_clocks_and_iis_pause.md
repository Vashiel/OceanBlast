# Register Clocks and IIS Playback Control

Timer 4 and IIS playback previously assumed a constant 45 MHz peripheral clock. IIS rates close to a standard audio rate were rounded to that rate. DMA also continued consuming PCM while the guest disabled or paused the IIS transmit path. These approximations can change timer deadlines, playback pitch and buffer ownership independently of host output buffering.

## Clock Tree

The board model uses a 12 MHz crystal. Samsung S3C2410A clock-register behavior is implemented in `src/memory/clock_tree.h`:

- Before the first MPLLCON write, FCLK uses the input crystal.
- MPLL output is `Fin * (MDIV + 8) / ((PDIV + 2) * 2^SDIV)`.
- CLKSLOW selects the crystal divided by twice SLOW_VAL; zero selects the undivided crystal.
- CLKDIVN selects the HCLK/PCLK dividers, including the S3C2410A special 1:4:4 mode.
- Timer 4 follows the resulting PCLK without reloading its active count when the clock changes.

For MPLLCON `0x52011`, CLKSLOW `4` and CLKDIVN `3`, the calculated clocks are FCLK 180 MHz, HCLK 90 MHz and PCLK 45 MHz. These frequencies describe programmed hardware clocks, not achieved interpreter throughput. Peripheral advancement still uses the nominal 20-million-tick time base; CPU instruction-cycle timing, cache effects, PLL lock delays and CLKCON peripheral gating remain incomplete.

## IIS and DMA

IIS prescaler A, its enable bit and IISMOD bit 2 determine the integer source sample rate in the modeled master mode. For 45 MHz PCLK, prescaler A = 7 and 256fs, the rate is 21972 Hz rather than 22050 Hz. Host resampling converts that rate to the configured output rate. Standard-rate snapping is removed.

Clock and IIS divider changes retime an active DMA transfer while preserving its progress. Completed stereo frames staged before a clock change are submitted using the preceding source rate. A partial stereo pair can cross the boundary. Progress rescaling rounds to a peripheral tick.

DMA to the IIS FIFO pauses when IIS is disabled, transmit DMA requests are disabled, TX idle is asserted, or transmit mode is not selected. Resuming preserves the source pointer and remaining transfer. Non-IIS DMA destinations are not paused by these controls. Unconfigured ROM-free DMA fixtures retain their previous default rate; they do not establish complete FIFO behavior.

IRQ entry now advances the peripheral budget supplied to `ARM920T::step`, matching ordinary and prefetch-abort steps. Diagnostic zero-tick CPU steps remain available.

## Validation

ROM-free checks cover reset clock selection; 180/90/45 MHz operation; the special divider; slow mode; exact IIS rates; prescaler bypass; Timer 4 phase across a PCLK change; active DMA progress across a rate change; IIS pause/resume; and peripheral advancement during IRQ entry. All nine regression suites pass after the changes.

Snapshots include FCLK, HCLK, PCLK, MPLLCON, CLKSLOW, CLKDIVN and IISCON. GUI performance CSV adds FCLK, PCLK and CPU steps per tick. `runtime_probe` records clock-register values and the inferred IIS rate; PCM capture events add IISCON, FCLK and PCLK. These fields separate programmed clocks, modeled time and wall-clock throughput.

[Wade clock-state measurements](validation/2026-10-08_wade_clock_states.csv) cover 600 million scheduled ticks with two CPU steps per tick and the same initial device image used in the earlier startup comparison. The early divider write produces 12/6/3 MHz before PLL selection; subsequent operation is 180/90/45 MHz. The initial configured IIS stream uses 87890 Hz, followed by 21972 Hz from scheduled tick 157000000 onward. The unconfigured 22050-Hz fallback also appears before IISPSR is programmed. Polling can miss brief intermediate register states. The run records 361 sampled framebuffer memory changes and 1810592 PCM samples; these totals do not establish smooth gameplay or acceptable sound.

Full IIS FIFO request thresholds, externally supplied slave clocks, CS43L43 processing, 8-bit PCM and hardware-equivalent CPU timing remain unresolved. Correct register behavior and continuous host queues do not establish correct animation speed or audible acceptance.

## Sources

- Samsung S3C2410A User's Manual: [PLL equation, section 7-4](https://laundry.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=222), [reset clock selection, section 7-2](https://tv.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=220), [slow mode and dividers, section 7-22](https://tv.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=240).
- Samsung S3C2410A User's Manual: [IIS control, section 21-5](https://tv.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=471), [IIS mode, section 21-6](https://tv.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=472), [prescalers, section 21-7](https://tv.manualsonline.com/manuals/mfg/samsung/s3c2410a.html?p=473).
- [ARM920T Technical Reference Manual, chapter 12](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb): instruction timing assumes cached accesses and requires additional system-dependent costs for external memory accesses.

## Optional Profile-Guided Build

`tools/build_profiled.ps1` builds an instrumented executable, runs supplied local cartridges without GUI or playback, and rebuilds the same output using GCC execution profiles. Source code remains independent of those profiles. Optional device settings are copied for each training run. ROMs, logs, profiles and guest dumps remain under ignored `build/`; the script does not replace the launcher executable. Missing compiler profiles cause a build failure.

```powershell
./tools/build_profiled.ps1 -Roms @("roms/games/Wade Hixton's Counter Punch [G] (EN).bin", "roms/games/Superstar Chefs [G] (EN).bin") -DeviceSettings bin/sessions/board.nvram
```

Use `-Compiler` when GCC is not on PATH. Evaluate the returned executable against the normal O2 build with identical input and final-state checks. Training is workload-specific; an instruction-rate increase is not proof of correct guest timing. A grouped ARM decode experiment produced mixed cartridge timings and was not retained.

[Matched build comparisons](validation/2026-10-08_profiled_build_comparison.csv) use 1.2 billion CPU steps, ratio 2, no GUI or host audio, and a freshly copied identical EEPROM image for every run. Training used 600 million steps each of Wade, Superstar Chefs and Rayman. Pitfall was not included in training. Each cartridge's O2 and profiled runs produce matching SHA-256 hashes for the complete 32-MB SDRAM and active framebuffer.

| Cartridge | O2 elapsed seconds | Profiled elapsed seconds |
| --- | --- | --- |
| Wade | 23.39, 27.04 | 22.29, 21.28 |
| Superstar Chefs | 28.09 | 23.00 |
| Rayman 3 | 26.22 | 22.09 |
| Pitfall | 25.96 | 20.79 |

The profiled runs take less wall time in these comparisons. Counts are limited and Wade's repeated O2 timings show host variability. These results support additional interpreter headroom, not a universal performance percentage, hardware-equivalent timing or gameplay acceptance. Build artifacts and compiler profiles are not published with source.

## Windows Playback Check

[The profiled Wade GUI trace](validation/2026-10-08_wade_register_clock_gui.csv) uses sound, 3x scaling, ratio 2, a 40-MIPS target, persistent device settings and scripted Start/A/direction presses. The 3.6-billion-step run exits normally. Local framebuffer snapshots show the engine and Inferno logos, the title, and a name-entry prompt following button input. This establishes startup rendering and menu input, not sustained fighting gameplay.

The audio source is 21972 Hz during the later intervals. The final trace reports zero dropped samples and zero empty-queue observations. Later input waits produce unchanged framebuffer samples; that is not evidence of a rendering stall. A repeated baseline previously also sustained 40 MIPS without queue starvation under lighter host load, so these counters cannot attribute all improvement to the compiler optimization. Listening and original-hardware timing acceptance remain pending.

A separate Pitfall PCM capture executes 1.6 billion steps at ratio 2 without host playback or persistent EEPROM input. It produces 807281 stereo frames at a resampled 22050-Hz output rate. The first nonzero frame occurs at 14.9184 output seconds; a subsequent ten-second excerpt peaks at 13150 with zero clipped samples. This excludes clipping in that excerpt, not crackling, incorrect pitch or guest production delays. The WAV and guest memory remain local.
