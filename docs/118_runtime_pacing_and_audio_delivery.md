# Runtime Pacing and Audio Delivery in v0.4.1

## Reproduction

The Windows x64 v0.4.1 executable matches a fresh Release build from tag `v0.4.1` (`c0a9674`) with GCC 16.2.0. Its SHA-256 is `60bee484fc1d1bc566629749dd2c7a2a3a46080907bc6d820a9e4de787e6ed80`. The cartridge is Wade Hixton's Counter Punch [G] (EN), CRC32 `eba2a2c3`. Runs use the 6:1 instruction ratio, fresh frontend settings, a zero-filled 2,048-byte EEPROM, the same [input fixture](../tests/wade_ratio6_fight.txt), and a 4,096-step batch. GUI runs use the plain window at scale 2 and the normal high-resolution host timer under Windows 11. The runs execute sequentially with the same default process affinity. Processor frequency and background load are uncontrolled.

At 6:1, the legacy model needs 120 million guest steps per wall second to advance one modeled second per second. The ratio determines guest work per device tick. The host limiter schedules that configured target. The guest IIS sample rate depends on the programmed clock and dividers. Each number measures a different part of the system.

A 1.8-billion-step replay on the release executable selects each CPU engine in turn with the Wade title shortcut disabled. The two final register dumps, complete SDRAM and active framebuffer agree. Measurements from the one-second reporting intervals:

| Engine | Weighted modeled speed | Peak modeled speed | Observed empty audio queues |
| --- | ---: | ---: | ---: |
| Interpreter | 64.74% | 76.52% | 265 |
| JIT | 77.30% | 107.75% | 109 |

Most empty queues occurred before the JIT recovered close to the 120-MIPS target. The JIT's late intervals ending after 1.2 billion steps averaged 98.76% model speed. In the longer replay the early interval from roughly 0.55 to 1.04 billion steps often ran at 52–57%, despite about 95% native instruction coverage and few new translations per second.

## Host pacing

In the release, up to 250 ms of accumulated host delay could be repaid when a slow scene gave way to a faster one. The correction bounds that repayable delay to 16 ms. The schedule rebases its wall deadline after a longer stall. CPU steps, device ticks, programmed clock rates and the cartridge ratio retain their original accounting. The timing suite checks a short delay, a longer stall and resumption in register-clock mode. `host_discarded_lag_ms` records the lag removed from the wall deadline.

A paced 8.5-billion-step pair uses the same JIT configuration and reaches the Rocco fight. The register dump, complete SDRAM and framebuffer hashes match:

| Measure | v0.4.1 | Bounded pacing |
| --- | ---: | ---: |
| Peak one-second modeled speed | 110.54% | 100.87% |
| Intervals over 102% | 4 | 0 |
| Fixed fight interval (7.0–8.5 billion steps) | 119.99 MIPS | 119.99 MIPS |
| Empty audio queues at end | 131 | 136 |
| Dropped output samples | 0 | 0 |

The slower part of startup still exhausts the output queue. The fixed fight interval adds no empty queues in either run. Host variability limits the interpretation of the five-event difference. The pacing improvement reduces recovery bursts. The title bar identifies the effective execution engine and labels the percentage as model speed. The release's `LCD` figure was calculated as `0.6 × model speed percent`.

A build with bounded pacing and the audio counters below reaches a 100.81% peak and 119.99 MIPS in the fixed fight interval. It records 150 empty audio queues before that interval and zero additional events during it. The final architectural register dump, complete SDRAM and active framebuffer agree across all three fight runs. [Run summaries](validation/2026-11-19_wade_timing.csv) and [one-second intervals](validation/2026-11-19_wade_pacing_intervals.csv).

## Audio cost and diagnostics

The original executable reaches 92.17 MIPS with sound and 110.66 MIPS with audio capture disabled in one unpaced startup comparison. The final architectural register dump, complete SDRAM and active framebuffer agree. This comparison combines guest PCM capture and host output cost.

A second check on a single build isolates the capture stage. Each mode runs 1.8 billion steps without host pacing. The fixed measurement window spans steps 0.75–1.8 billion:

| Audio path | Throughput | CPU/bus interval | GUI interval excluding wait |
| --- | ---: | ---: | ---: |
| Capture disabled | 116.29 MIPS | 8.967 s | 45.9 ms |
| Guest PCM captured with `--audio-capture-only` | 110.83 MIPS | 9.408 s | 50.4 ms |
| Capture, resampling and Windows output with `--sound` | 105.64 MIPS | 9.869 s | 53.3 ms |

The capture-only run sees 395,837 guest stereo frames over the entire replay. All three modes match final registers, SDRAM and framebuffer. These are sequential single runs and the differences are estimates for this workload. The DMA sampler keeps its device deadlines even if its callback only counts frames. The remaining cost includes resampling, queueing, the audio worker and OS scheduling. The GUI interval is under 1% in these unpaced runs. [Audio-path comparison](validation/2026-11-19_wade_audio_path.csv).

In that build's fight, the output path produces about 22,050 frames per wall second at a configured 22,050 Hz. The estimated queue depth from the software ring and the reported device playback position corresponds to 85–102 ms of audio. The estimate covers queued frames before the output endpoint. Audible quality, end-to-end latency and A/V synchronization still require a playback comparison.

`audio_produced_frames` counts output-rate frames before queue drops. `audio_pending_frames` counts frames in the software ring. On Windows, `audio_submitted_frames` now advances on successful `waveOutWrite`, and `audio_remaining_frames` subtracts the reported playback position from that submitted count. Counting software-ring writes as device submissions previously mixed the two queues. The device position resets on open or reset. [Windows waveform position documentation](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutgetposition).

All 13 native CTest suites pass. The Windows device test feeds one incomplete block followed by enough data to complete 4,096 stereo frames. It checks production, buffering, device submission, playback position and reset on reopening. The replay tools hash the final register dump separately from the preceding JIT statistics. Sound can change native-window and translation counts even when the final CPU registers match.

## Kernel Idle Loop and Native Execution

A five-second host instruction-pointer capture of the pacing-and-audio build begins after step 552,250,000. The last complete reporting interval at the end of sampling is step 843,100,000. `X64ExecutionEngine::run`, `Impl::executeNative` and `Impl::stepWindow` account for 1,008 of 1,952 samples (51.6%). The PE function ranges and symbol table come from the sampled executable. Another 332 samples are outside the main executable and remain unassigned. In those reporting intervals the engine retires about 3.17 generated guest steps per native window entry. The same measure exceeds 800 in the fixed fight. Window entries include all blocks chained within a single bridge call. [Host sample counts](validation/2026-11-19_wade_startup_host_pc.csv).

The guest PC repeatedly lands in `0xc001afb8`–`0xc001afc0`. The routine sets bit 2 in `CLKCON`, reads it repeatedly and clears it before returning. Its virtual register address `0xf040000c` resolves to `0x4c00000c`. The sequence matches the S3C2410 idle routine in the nearby upstream kernel release. On hardware the bit requests entry to IDLE, and software clears it after wake. The current OceanBlast model stores the written bit. [Linux v2.6.12 idle routine](https://raw.githubusercontent.com/torvalds/linux/v2.6.12/include/asm-arm/arch-s3c2410/system.h) and [Samsung S3C2410A User's Manual, pp. 7-17 and 7-21](https://bitsavers.org/components/samsung/S3C204x/S3C2410/21-S3-C2410A-032004_S3C2410A_Users_Manual_1.0_200403.pdf).

The prior JIT ended a native window after each read. The word read returns a stored value and has no timed device side effects in the current model. The JIT can therefore finish the block under the existing device deadline. It still performs each read, including its diagnostic counter. Translation faults, writes, other device reads and scheduled IRQ boundaries retain their existing handling. ROM-free tests check the read count, register state and a Timer 4 expiry in legacy and register-clock modes.

Two alternating pairs with audio enabled and pacing disabled measure the fixed 0.55–1.04-billion-step interval. Throughput rises from 60.56–62.20 MIPS to 149.33–150.59 MIPS. From reset through 1.04 billion steps the native bridge entries fall from 191.83 million to 27.49 million. The canonical CPU checkpoints, complete SDRAM and active framebuffer match in all four runs.

A paced pair uses the same two executables through 8.5 billion steps. Both reach the Rocco fight with identical canonical CPU, SDRAM and active framebuffer hashes. The reporting intervals within the previously slow startup range deliver 48–53% modeled speed before the change and 99.87–100.03% after it. They add 75 empty-queue observations before the change and zero after it. The complete runs record 145 and 60, respectively, with zero samples dropped by the output layer. The 7.0–8.5-billion-step fight interval runs at 119.98–119.99 MIPS and adds zero empty queues in both runs. The candidate's peak reporting interval is 101.14%. [Paired native-read measurements](validation/2026-11-19_wade_clkcon.csv) and [reporting intervals](validation/2026-11-19_wade_pacing_intervals.csv).

## Remaining timing questions

- Measure startup PCM delivery and live queue behavior separately. The earlier [guest PCM analysis](89_wade_music_startup_delivery.md) found a difference in the generated opening before output. The [single-buffer investigation](90_wade_music_single_buffer.md) identified the transition to a second buffer. Calibration of the original title mix requires a console recording.
- Profile the remaining startup intervals without changing the DMA sample lifetime. Preserve source reads at the time the guest consumes each sample, including writes to a running buffer. A larger output queue can absorb short scheduling interruptions. Sustained underproduction requires enough execution capacity to supply the configured rate.
- Model `CLKCON` IDLE entry and interrupt wake-up on the shared CPU/peripheral timeline. The reduced cost of the Linux loop preserves its instruction stream. A hardware IDLE model also has to advance peripherals while the CPU clock is stopped and pace that idle time against the host.
- Compare recorded intervals from known original hardware before treating 100% as original-console tempo. Register-clock mode remains an experimental CPU cycle estimate and normally uses the interpreter. The opt-in native path shares the modeled timeline. [Common timing model](110_common_timing_model_and_calibration.md) and [clocked native execution](113_clocked_native_timeline.md).
- Use Wade first for paced acceptance, including startup sound and the fight. Follow with Superstar Chefs when Wade meets the audio and pacing targets. Verify actual listening, device latency and audiovisual synchronization on more than one host before generalizing.
