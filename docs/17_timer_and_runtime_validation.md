# Timer and Runtime Validation

## 1. Timer 4 Clock Coherence

The previous Timer 4 model decremented `TCNTO4` by 64 on each register read while generating interrupts every 100,000 instructions. The guest's elapsed-time calculation and its interrupt source therefore observed different clocks. Polling the timer also changed the timer value.

Timer 4 now advances from the same instruction time base as the peripheral scheduler. `TCFG0` prescaler 1, `TCFG1` divider 4, `TCNTB4`, and `TCON` start/manual-update/auto-reload bits determine its period. Register reads have no side effects. Fractional clock progress and overshoot survive reloads; multiple expirations coalesce into the pending interrupt. Reset clears timer and interrupt state.

The model uses the guest-observed 45 MHz peripheral clock and the existing 20-million-instruction time base. This is a coherent approximation, not a cycle-accurate ARM9 or complete PLL clock model. External TCLK1 is not synthesized. The other PWM channels remain incomplete.

Reference: the [Linux S3C2410 timer implementation](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/mach-s3c2410/time.c) reads `TCNTO4` to determine elapsed time between interrupts. Register definitions are in [regs-timer.h](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/arch-s3c2410/regs-timer.h).

## 2. Video-only Winx Results

The same language-confirmation script was replayed for 3.5 billion instructions on both video-only variants. Neither run reports a guest segmentation fault, kernel panic or invalid PCM pointer.

| Variant | Observation with coherent Timer 4 |
| --- | --- |
| Italian/Spanish | An actual episode image appears by 2 billion instructions; snapshots at 2, 2.5 and 3 billion have the same framebuffer hash. Continued movie playback is unresolved. |
| Netherlands/international | The player opens the media and reaches the playing state, but the final framebuffer remains entirely black. |

[Cartridge identities and bounded results](validation/2026-10-08_timer_winx.csv) distinguish these video-only dumps from the damaged international game/video combination in [ROM integrity](14_rom_integrity.md). The earlier 6.5-billion-instruction black-frame results in [EEPROM startup validation](16_i2c_eeprom_and_player_startup.md) used the preceding timer model.

An optional `--pc-profile N` samples `(TTB, PC page)` every N instructions and writes `pc_profile.csv` at snapshots and exit. A completed Netherlands Winx run sampled every 1,000 instructions: approximately 52.6% of samples were in kernel addresses, 12.2% in the mapped Cook audio decoder and 18.3% in the mapped video decoder. Both decoder libraries execute; this does not prove successful frame submission. Sampling can alias periodic execution and does not measure host CPU cost. [Bounded run result](validation/2026-10-08_runtime_winx_nl.csv).

Remaining investigation targets include decoder thread scheduling, frame delivery through the framebuffer video site, and audio-clock interaction. A larger instruction budget alone has not established continued playback. Preserve the timer configuration, mapped library addresses, PC-page profile and framebuffer hashes when comparing future changes.

## 3. Game and Display Validation

All eleven game cartridges complete a two-billion-instruction input replay without guest segmentation faults or kernel panics. Ten have nonempty final framebuffers; Gormiti Agguato is black at the end of that generic replay. These are startup/rendering observations, not per-title gameplay acceptance. [Results](validation/2026-10-08_runtime_games.csv).

Gormiti Agguato completes separate 3.5-billion-instruction runs with a visible [title when no buttons are pressed](validation/2026-10-08_gormiti_timer_title.csv), and a visible [main menu after one A press](validation/2026-10-08_gormiti_timer_menu.csv). The latter sequence is `tests/gormiti_title_confirm.txt` (A at 1.5 billion instructions, release 20 million instructions later). This distinguishes working initial rendering from the black transition triggered by the generic multi-button experiment. Menu output does not establish tournament gameplay.

Crazy Jack has also been observed in an interactive gameplay scene with moving characters, changing score and changing lives. Its packed RGB444 pixels still require the explicit 480-byte-stride diagnostic decoder despite the guest LCD register selecting RGB565. The launcher offers that override; default decoding continues to follow the LCD configuration. Automatic identification remains unresolved.

The GUI now preserves key-down/key-up transitions between guest input polls. Repeated key-down messages do not accumulate duplicate transitions. Losing window focus releases held buttons; pause synchronizes input rather than replaying stale transitions on resume. These behaviors have ROM-free regression coverage.

A GUI/audio replay of `tests/crazy_play_confirm.txt` completes five billion instructions and exits normally. Snapshots show progression from title to level selection, gameplay and rightward movement; the score changes from 0 to 25. The last 63 measured seconds average 19.12 MIPS and 18.62 sampled framebuffer changes per second. [GUI measurements](validation/2026-10-08_crazy_runtime_gui.csv) include loading intervals; regression compilation overlapped part of the run, so this is functional validation rather than an isolated speed comparison.

Framebuffer polling remains periodic, but an unchanged framebuffer is no longer decoded and submitted again. The cached image remains available for window repaint. `present_fps` now counts actual redraw submissions; `changed_fps` counts observed buffer changes. A stationary image can report zero for both without implying CPU execution has stopped. These are sampled host observations, not guest vertical-sync counters.

## 4. Audio Clock and Queue Diagnostics

The IIS master-clock ratio selector is `IISMOD` bit 1: clear selects 256fs, set selects 384fs. The previous implementation read bit 2. This is corrected and covered by register-level tests. Observed cartridge modes `0x99` and `0xe7` still select 256fs and 22,050 Hz, so the bit correction alone does not explain or cure their reported stuttering. Reference: [Linux regs-iis.h](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/arch-s3c2410/regs-iis.h).

The Windows output backend starts playback after two complete 4,096-byte buffers have been submitted where pause/restart is supported. At stereo 16-bit 22,050 Hz this provides approximately 93 ms of initial buffering. This absorbs initial scheduling jitter at the cost of startup latency; it does not implement dynamic resynchronization. Devices rejecting the pause call retain immediate playback. References: [waveOutPause](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutpause), [waveOutRestart](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutrestart).

The window title, snapshots and GUI performance CSV expose submitted audio frames, queued frames and empty-queue observations alongside dropped samples. Queued frames sum complete buffers marked `WHDR_INQUEUE` and not `WHDR_DONE`, so they are an upper bound on unplayed frames, not an exact playback cursor. Empty-queue observations occur when a PCM submission sees no queued data after playback has started; consecutive observations are coalesced until another buffer is submitted. They do not count every audible interruption. Reference: [WAVEHDR flags](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-wavehdr).

Remaining acceptance requires sustained listening tests for pitch, interruptions and audio/video alignment during each title's gameplay or episode playback. Zero dropped samples is insufficient evidence of uninterrupted sound.

The completed Crazy Jack GUI replay reports zero dropped samples but 380 empty-queue observations. The initial two-buffer prefill therefore does not eliminate starvation throughout gameplay. Investigate intervals where instruction throughput falls below the model's nominal 20 MIPS together with guest PCM production; avoid inferring smooth playback from successful buffer submission alone.

## 5. Reproduction

Extract video archives into a separate directory before auditing; the tool accepts cartridge `.bin` files, not ZIP containers.

```powershell
make test
python tools/audit_games.py --exe bin/oceanblast.exe --roms extracted-video-roms --match "Winx*" --output validation-winx --steps 3500000000 --timeout 420 --input-script tests/video_language_confirm.txt --snapshots 500000000 --pc-profile 1000
bin/oceanblast.exe "roms/games/Crazy Jack [G] (EN).bin" --gui --sound --profile --display-format rgb444 --display-stride 480
```

The ROM-free suites cover CPU/DMA, input, resampling, UART, EEPROM/I2C, framebuffer stride/decoding, Timer 4 and IIS clock selection. Published results contain cartridge identities and observations; cartridge contents, extracted decoder programs and framebuffer images are not distributed.
