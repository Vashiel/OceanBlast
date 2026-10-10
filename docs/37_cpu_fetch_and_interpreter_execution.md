# CPU Fetch and Interpreter Execution

## Shared Execution Limits

Slow animation has two distinct execution limits. A small instruction allowance can leave insufficient guest work for drawing, even when the host meets its pacing target and the audio queue stays filled. A higher allowance requires more host throughput; falling below that target can empty the audio queue. The [Chefs workload measurements](29_gameplay_work_and_execution_cost.md) identify software mixing and drawing as substantial guest workloads. These observations do not identify the cause of every cartridge's graphics defect.

The following changes reduce interpreter cost without increasing the selected instruction allowance, changing programmed clocks, or changing guest cycle estimates.

## Instruction Fetch

The bus retains the translated SDRAM address for the current 1 KB virtual instruction region. ARM words and Thumb halfwords are read from RAM on every fetch. Instruction contents are never cached, so code modified by the guest is visible on the next fetch.

Changing user/supervisor access, the translation table, MMU enablement, or domain access control discards the retained address. Guest TLB invalidation and bus reset also discard it. Unaligned fetches and addresses outside physical SDRAM use the existing physical access functions. The 1 KB region does not cross a small-page access-permission subregion; the underlying TLB still has its preceding permission-model limitations.

This is a host address lookup optimization. It does not implement the ARM920T instruction cache, cache maintenance timing, memory wait states, or bus contention.

## ARM Execution and Peripheral Calls

ARM decoding selects the major instruction class before testing overlapping control, multiply, and halfword encodings. Common loads and branches avoid the preceding sequence of unrelated comparisons.

The shorter ALU path now includes immediate operands and register operands with constant LSL, LSR, ASR, ROR, and RRX shifts, with or without flag updates. TST, TEQ, CMP, and CMN update flags without writing a destination register. Arithmetic carry/overflow and logical shifter carry retain the general implementation's behavior. Carry arithmetic, register-controlled shifts, control-register encodings, and PC destinations retain the general path. `--simple-alu off` remains available for comparison.

Zero-time bus calls return after the level-triggered UART check. They do not revisit timer, DMA, ADC, or I2C countdowns. Positive-time advancement is unchanged, and clearing the UART source can still reassert its active level during a zero-time call.

## Validation

The ROM-free core suite passes 266 reported checks. Differential CPU cases include 10,400 flag-setting operations, 40,960 constant-shift combinations, 8,192 mixed data-processing cases, and all 256 condition/NZCV combinations. Comparisons retain registers, CPSR, and instruction cycle counts. Fetch checks cover modified code, ARM/Thumb reads, region and SDRAM-end boundaries, unaligned/SRAM fallbacks, reset, translation changes, and user-mode permission faults. Interrupt and timed-device suites retain their existing coverage.

## Controlled Throughput

The tested candidate combines these changes with O3 unity compilation and fresh profile-guided training on Chefs, Wade, and DigiQUAD. Training uses 2.4 billion instructions per cartridge, ratio 4, `tests/chefs_ratio4_confirm.txt`, and independent erased EEPROM images. The baseline is the preceding installed optimized build, SHA-256 `526e09c1aa4d0446e25b2e237cbcfb95284b3b491b5bb618b1b803f8d384021c`; the candidate is `80981e4eb6325ae0f92456084f473d0d8ed99a36dc79b59526ed8a25037176bb`. These measurements describe the combined build, not the ordinary O2 build or an isolated benefit of each change.

Paired headless comparisons use the same 2.4-billion-instruction budget, ratio, input, and initial EEPROM state. Two repetitions reverse execution order. No other cartridge run or compilation runs concurrently. Complete SDRAM, final CPU, framebuffer, and penultimate snapshot hashes match in all six pairs. [Detailed records](validation/2026-10-10_cpu_fetch_execution.csv) retain wall and process CPU times and content identities.

| Cartridge | Mean baseline MIPS | Mean candidate MIPS | Throughput increase |
| --- | ---: | ---: | ---: |
| Superstar Chefs | 68.01 | 79.69 | 17.17% |
| Wade Hixton's Counter Punch | 62.35 | 90.63 | 45.37% |
| DigiQUAD | 62.31 | 89.06 | 42.93% |

These bounded replays cover startup and their reached scenes. They are instruction-throughput measurements, not game frame rates or original-console speed measurements. Wall time varies between repetitions; process CPU time is included to distinguish execution cost from host scheduling.

## Chefs GUI and Audio

A separate isolated GUI comparison runs both builds with sound, the high-resolution host timer, identical input and initial EEPROM state, ratio 4, and 4.8 billion instructions. Both reach the first level, and their final CPU, complete SDRAM, and framebuffer hashes match.

| Build | Mean MIPS | Modeled pacing target reached | Sampled changes per wall second | Empty-queue observations | Dropped samples |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 69.49 | 86.86% | 14.35 | 77 | 0 |
| Candidate | 78.63 | 98.29% | 16.23 | 10 | 0 |

The candidate records seven empty queues by modeled second 30 and ten at the end. Residual queue starvation remains. [GUI records](validation/2026-10-10_cpu_fetch_chefs_gui.csv) include the run parameters and hashes. Framebuffer-change counts are not complete game frames. This sound-enabled comparison is separate from the headless throughput measurements; host audio delivery can expose bottlenecks that a headless run omits.

## Wade Introduction and First Fight

A sound-enabled recording uses the candidate, a manual 4x ratio, the default 80-MIPS limiter, and an erased EEPROM image. [The input script](../tests/wade_intro_ratio4.txt) confirms menus and name entry, then releases all buttons at instruction 9,040,000,000. It supplies no further input. The introduction advances through its complete sequence and reaches the first fight against Rocco McScrub. With no fighting input, the player loses and the game displays its knockout screen.

The capture is stopped deliberately after that match. Its last reporting interval reaches instruction 51,254,850,000 and modeled second 640.686; the configured 64-billion-instruction limit is not completed. The decoded LCD recording contains 19,710 frames over 657 wall seconds at 30 fps. It contains no audio, although sound remains enabled in the emulator. Capture files remain local; [per-interval measurements](validation/2026-10-10_wade_intro_ratio4.csv) retain input, executable, cartridge and initial EEPROM identities. Recording, snapshot writes and small inspection reads add host overhead to this diagnostic run.

The active-fight interval at wall seconds 440 through 470, before the knockout transition, measures:

| Measurement | Result |
| --- | ---: |
| Reporting duration | 30.009 seconds |
| Time-weighted instruction throughput | 79.33 MIPS |
| Time-weighted legacy pacing target reached | 99.16% |
| Sampled framebuffer changes per wall second | 21.09 |
| Empty-queue counter at first/last interval | 396 / 405 |
| Dropped samples | 0 |

The fight remains visibly slow despite reaching the legacy pacing target. In recorded frames at wall seconds 440, 450, 460 and 470, the round counter reads 90, 88, 85 and 82 respectively. This is an observation of the displayed counter; its update logic and original-console timing have not been established. The knockout screen remains static while the CPU continues executing. Continuing past that screen is untested.

The snapshot at instruction 36 billion records PCLK 45 MHz, `TCFG0=0x200`, `TCFG1=0`, and `TCNTB4=0x927b`. The current timer model therefore expires at 200 Hz: `45,000,000 / (3 * 2 * 37,500)`. This establishes the modeled period, not delivered interrupt frequency or guest timekeeping. Timer expirations, serviced IRQs and guest clock progression must be measured separately before changing programmed timer frequencies.

Reaching a fight at manual 4x does not validate the provisional automatic 2x profile, player controls, correct animation speed, or sustained sound quality.

## Cartridge Regression Audit

All eleven game cartridges complete a two-billion-instruction headless run using automatic cartridge settings and `tests/start_buttons.txt`. Every process exits with code zero; none times out or reports a kernel panic or guest segmentation fault. [Audit results](validation/2026-10-10_cpu_fetch_games.csv) retain cartridge hashes and framebuffer observations. These starts do not validate sustained gameplay, audio or player input.

Gormiti Agguato nella Valle and Gormiti Lotta Oscura each report two SquashFS messages while attempting to mount `/data`. Repeating the three Gormiti cartridges with the baseline executable produces the same messages, final PCs, complete SDRAM hashes and active framebuffer hashes. The [baseline comparison](validation/2026-10-10_gormiti_baseline.csv) establishes that these messages precede the CPU optimization; their cause remains unresolved. Agguato's final framebuffer is black in both builds.

## Reproduction

```powershell
make test
./tools/build_profiled.ps1 -Roms @(
  "roms/games/Superstar Chefs [G] (EN).bin",
  "roms/games/Wade Hixton's Counter Punch [G] (EN).bin",
  "roms/games/DigiQUAD [G] (EN).bin"
) -Steps 2400000000 -CpuStepsPerTick 4 -Unity -Optimization O3 `
  -InputScript tests/chefs_ratio4_confirm.txt -DeviceSettings initial.nvram
```

`initial.nvram` is an independent 2,048-byte device-state image; the measurements use all bytes set to `0xff`. Use the returned executable for paired comparisons. Generated profiles, cartridge contents, captures, and independent device-state copies remain local.

```powershell
python tools/compare_execution.py --exe candidate.exe --baseline-exe baseline.exe `
  --comparison executable --rom "roms/games/Superstar Chefs [G] (EN).bin" `
  --steps 2400000000 --ratio 4 --repeat 2 --output build/execution-comparison `
  --input-script tests/chefs_ratio4_confirm.txt --nvram initial.nvram
python tools/compare_gui.py --exe candidate.exe --baseline-exe baseline.exe `
  --rom "roms/games/Superstar Chefs [G] (EN).bin" --steps 4800000000 --ratio 4 `
  --output build/gui-comparison --input-script tests/chefs_ratio4_confirm.txt `
  --nvram initial.nvram
candidate.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" `
  --gui --sound --profile --cpu-steps-per-tick 4 --steps 64000000000 `
  --snapshot-interval 4000000000 --exit-on-limit `
  --input-script tests/wade_intro_ratio4.txt --nvram initial.nvram `
  --record-frames wade.rgb
```

## Remaining Accuracy Work

Higher host throughput does not supply a hardware-equivalent CPU timing model. In legacy mode, one peripheral tick is supplied per selected number of instructions. The default host limiter is 20 MIPS multiplied by that ratio. Raising only the host limiter changes the wall-clock rate of CPU and peripheral advancement together; changing the ratio changes the guest work available per peripheral tick. The internal 20-MHz peripheral time unit is not the ARM920T's physical CPU clock.

The experimental register-clock mode converts estimated instruction cycles using the CP15-selected execution clock. Its pipeline, instruction/data cache, memory-wait and bus-contention modeling remain incomplete. The next timing investigation should compare timer expirations, serviced interrupts, guest timekeeping, PCM consumption and complete frame progress in the same active scene. Cache and memory costs need hardware evidence. Software mixing can still consume much of a low instruction allowance.

The current idle counter measures bounded wait-for-interrupt advancement in experimental clocked mode. Legacy-mode zeroes do not establish a fully occupied guest, and active polling loops need separate classification. Guest jiffies must be sampled alongside modeled time and host time: guest timekeeping alone cannot independently validate the timer interrupts that advance it. A jiffies observation requires a verified address for the loaded kernel.

Timer 4 and DMA already advance from the shared `Bus::tick` time supplied by the selected execution mode. Host display sampling and Windows vertical-blank presentation follow host time. They do not generate guest LCD interrupts. Programmed LCD scanline progression and vertical-blank interrupt generation are not implemented, so a Windows presentation count must not be reported as a guest VSync count. Any scene waiting on LCD status needs guest access and code-path measurements before its effect can be established.

Game framebuffer changes are samples of live RAM and can include partial redraws. The complete-video-sweep capture path does not establish game LCD scanline or vertical-blank behavior. Queue continuity does not establish waveform quality, pitch, or audiovisual synchronization. Scene-specific comparisons and original-console evidence remain necessary for those questions.
