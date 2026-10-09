# Windows Host Pacing

## Short Waits and Audio Scheduling

Legacy GUI pacing checks every 50,000 instructions. At 40 MIPS the interval is 1.25 ms, including CPU work. Ordinary `sleep_until` can resume substantially after such a short deadline. The guest clock then falls behind while Windows audio playback continues, which can contribute to an empty output queue. This host scheduling mechanism is separate from an insufficient guest instruction allowance or incorrect hardware timing.

The GUI now uses a reusable high-resolution Windows waitable timer when available. The timer is set to a relative one-shot deadline and the execution thread waits without spinning. Its handle is closed at session exit. A failed timer creation, activation or wait falls back to ordinary `sleep_until`. Headless execution does not create a timer. Guest instruction ratios, modeled clock conversion and audio sample rates are unchanged.

`--host-wait sleep` retains the preceding method for comparison; `--host-wait timer` is the default. The session log records the active method. Microsoft documents high-resolution waitable timers for Windows 10 version 1803 and later; unsupported systems use the fallback. No system-wide timer-resolution request is made. References: [CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw), [SetWaitableTimer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimer).

## Deadline Probe

`tools/probe_host_wait.cpp` measures 400 consecutive deadlines at 1.25 ms intervals for each method, without a cartridge or guest CPU. The [recorded probe](validation/2026-10-09_host_wait.csv) reports:

| Method | Mean lateness | 95th percentile | Maximum lateness |
| --- | ---: | ---: | ---: |
| Standard sleep | 8.158 ms | 16.633 ms | 24.131 ms |
| High-resolution timer | 0.283 ms | 0.507 ms | 1.167 ms |

This confirms a substantial reduction in host deadline jitter on the tested system. Other emulator validation processes were running during the probe. It does not establish correct game speed, audio fidelity or synchronization, and a timer cannot supply missing CPU throughput.

## GUI State and Load Test

Two 30-second modeled-time Superstar Chefs replays use ratio 2, identical initial EEPROM images and `tests/chefs_ratio2_confirm.txt`, changing only the wait method. Both exit normally and produce identical final CPU, complete SDRAM and framebuffer hashes. [The paired observations](validation/2026-10-09_host_wait_gui.csv) record those hashes and load conditions.

Four cartridge audits run concurrently during this pair. The standard-wait run averages 13.108 MIPS and 32.77% modeled speed, with 574 empty-queue observations. The timer run averages 12.149 MIPS and 30.37%, with 576 empty-queue observations. Neither drops submitted samples, but both repeatedly starve the audio queue. These heavily loaded runs establish unchanged guest execution and normal cleanup; they do not demonstrate an audible improvement or permit an isolated speed comparison. The deadline probe verifies wait precision separately.

The [unloaded pair](validation/2026-10-09_host_wait_gui_unloaded.csv) repeats those settings after all concurrent cartridge tests have stopped. Standard wait averages 39.981 MIPS and 99.954% modeled speed; the timer averages 40.000 MIPS and 100.000%. Both report zero empty queues and zero dropped samples, and both match the same final CPU, SDRAM and framebuffer hashes. Sampled framebuffer changes remain only 2.512/s and 2.548/s respectively. Thus uninterrupted host audio output at the 2x pacing target does not resolve the slow animation, and the probe's jitter improvement cannot be described as a demonstrated audible improvement in this unloaded scene. Listening and original-hardware timing acceptance remain open.

The installed profile-guided executable has SHA-256 `2c545567da9fd8d9188b9e333a738c0e807e8fe4bfe13fb7d885fc969b73b90e`. All ten regression executables pass with 212 checks. The GUI comparisons exit normally and close their windows.

## Higher Guest CPU Allowance

An additional unloaded Chefs run uses ratio 4, the high-resolution timer and `tests/chefs_ratio4_confirm.txt`. Its input instruction positions are doubled relative to the ratio-2 script to retain the same nominal peripheral-time positions. The [recorded interval](validation/2026-10-09_chefs_4x_gui.csv) spans 41.016 wall seconds, averaging 58.400 MIPS, 73.000% of this mode's pacing target and 9.850 sampled framebuffer changes/s. It ends with 465 empty-queue observations and zero dropped samples. The run reaches its 30-second modeled-time limit and exits normally.

More guest execution permits substantially more image updates than the ratio-2 run, but this host does not sustain the 80-MIPS target in the tested scene and audio repeatedly runs out. Selecting the higher ratio automatically would therefore exchange slow animation for broken audio rather than resolve the cause. The 2x setting remains a useful diagnostic for uninterrupted output in this scene, not a hardware speed or per-cartridge optimum. Further work must address CPU execution cost and the clock model together rather than choose a ratio from framebuffer activity alone.

```powershell
make build/probe_host_wait.exe
build/probe_host_wait.exe
bin/oceanblast.exe "roms/games/Superstar Chefs [G] (EN).bin" `
  --gui --sound --profile --cpu-steps-per-tick 2 --host-wait timer `
  --steps 9999999999 --emulated-seconds 30 --exit-on-limit `
  --input-script tests/chefs_ratio2_confirm.txt
```

Use an independent copy of the same EEPROM image for each paired run. Host load, output-device scheduling, initial device state and selected scene remain relevant to interpreting audio queue and framebuffer observations.
