# Manual Cartridge Validation

## DigiQUAD

Manual observation: the game runs very quickly, but the sound is severely distorted. Correct animation speed has not been established.

[Captured GUI measurements](validation/2026-10-08_digiquad_manual_sound.csv) show approximately 20 MIPS, 15–21 sampled image changes per second in the final intervals, zero dropped output samples and a stable cumulative count of five empty-queue observations. These counters do not explain the reported distortion.

A generated-PCM excerpt also sounds defective. [Consumed-sample DMA capture](20_pcm_capture_and_dma_sample_lifetime.md) reduces discontinuities in a controlled comparison; subsequent listening feedback confirms significant improvement with remaining errors. This is partial audio progress, not acceptance of the cartridge's speed, gameplay or synchronization.

## Wade Hixton's Counter Punch

Manual observation: the engine splash and Inferno logo animate very slowly; the title animation stutters and music has slight interruptions. This report covers startup and title presentation, not sustained gameplay.

The inspected session uses the current EEPROM/Timer 4 build with 3x scaling, the default 20-MIPS pacing limit, FPS logging, verbose debug logging and persistent device settings. Sound is disabled in this particular session. It therefore cannot validate the reported music interruptions.

[Captured GUI measurements](validation/2026-10-08_wade_manual_debug.csv) cover 187.51 seconds. Mean instruction throughput is 19.70 MIPS, with 5.49 sampled framebuffer changes per second across boot and startup. The last 30.08 seconds average 19.19 MIPS and 0.57 changes per second. These rates count sampled image changes, not guest vertical synchronization or completed game frames. The session log exceeds 16 MB and contains extensive scheduler/syscall diagnostics; logging overhead must be excluded before attributing slow presentation to rendering or CPU execution alone.

The output backend has submitted zero audio frames because this session was launched without `--sound`. Queue and drop counters from it provide no evidence about audible playback quality.

For the next comparison, enable **Sound** and **FPS Log**, disable **Debug Log**, and keep the ROM, scale, pacing limit and device settings unchanged. Record observations separately for the engine splash, Inferno logo, title and gameplay. Preserve each session's log and CSV before launching another cartridge, since the launcher replaces them.

```powershell
bin/oceanblast.exe "roms/games/Wade Hixton's Counter Punch [G] (EN).bin" --gui --scale 3 --sound --profile --nvram bin/sessions/board.nvram
```

The command is intended for execution from the repository root. The launcher's corresponding controls preserve its normal session output directory. Compare submitted audio frames, queued frames, empty-queue observations and instruction throughput alongside listening results; zero dropped samples alone is insufficient acceptance evidence.

### Sound Enabled, Debug Logging Disabled

The repeated manual test still reports slow-motion startup after restarting with sound and FPS logging enabled and debug logging disabled. The process arguments confirm these settings. [Captured measurements](validation/2026-10-08_wade_manual_sound.csv) show 20.00 MIPS and 7.51 sampled framebuffer changes per second over the last 30.08 seconds. The backend has submitted 1,769,472 audio frames, with zero dropped samples and 24 empty-queue observations at capture time. This establishes that verbose logging alone does not explain the reported slow animation. Audible quality in this repeat has not yet been separately reported.

The GUI pacing limit and Timer 4/DMA instruction time base both currently assume 20 million instructions per second. Maintaining that limit does not establish correct hardware speed. A follow-up should distinguish instruction throughput from guest timer delays and compare the same animation under controlled pacing. Simply increasing the host limit also accelerates instruction-based peripheral time and can alter audio production, so it must be treated as a diagnostic rather than a verified timing correction.

## Superstar Chefs

Manual observation: severe music stuttering, very weak response to keyboard game buttons, and slow-motion gameplay. Keyboard input was explicitly confirmed; this is not a report about unsupported mouse controls.

The inspected process runs with sound and FPS logging enabled, debug logging disabled, 3x scaling and persistent device settings. [Captured GUI measurements](validation/2026-10-08_chefs_manual_sound.csv) show 20.00 MIPS but only 3.49 sampled framebuffer changes per second over the final 30.08 seconds. At capture time the backend reports zero dropped samples and 418 empty-queue observations; 92 additional observations occur between the first and last rows of that final window. This indicates repeated queue starvation observations despite reaching the configured instruction-rate target. It does not measure the exact count or duration of audible interruptions.

The host input handler maps keyboard events to console buttons and preserves press/release transitions. The reported poor response remains unverified at the guest-consumption level: distinguish event delivery, guest keypad reads, and delayed visible animation in a focused input trace. Do not infer a keyboard fix from framebuffer activity alone. Startup rendering is established; acceptable gameplay speed, input response and audio continuity are not.

## Spider-Man Mysterio's Menace

Manual observation: strongly metallic/tinny audio with crackling. The picture moves noticeably faster than the preceding tested titles but still stutters. These are separate audio-quality and animation-smoothness defects.

The inspected process uses sound and FPS logging, no verbose debug logging, 3x scaling and persistent device settings. [Captured GUI measurements](validation/2026-10-08_spiderman_manual_sound.csv) average 19.83 MIPS and 17.49 sampled framebuffer changes per second over the final 30.08 seconds. At capture time there are zero dropped samples and 11 empty-queue observations; three additional empty observations occur between the first and last rows of that final window.

The sampled image-change rate exceeds Superstar Chefs' measured rate, consistent with the reported smoother relative presentation. Scene differences prevent treating this as a controlled performance comparison. The audio remains objectionable despite relatively few observed empty queues. Queue starvation alone is therefore not an established explanation for the metallic timbre or all crackling. Further checks should capture the PCM before host output, inspect discontinuities and amplitude clipping, verify DMA sample/channel interpretation, and compare guest IIS settings with the host resampling path. None of those causes has yet been confirmed by this session's metrics.

## Rayman 3

Manual observation: the opening studio animation stutters and repeatedly stalls, with crackling/stuttering audio. The display window subsequently closes unexpectedly, raising suspicion of a crash.

[Captured measurements](validation/2026-10-08_rayman_manual_sound.csv) show 20.00 MIPS and 3.10 sampled framebuffer changes per second over the final 30.05 seconds. There are zero dropped samples and 704 empty-queue observations at the last recorded interval, including 126 additional observations between the first and last rows of that window. This supports investigating repeated audio starvation alongside the observed stalls, without establishing an exact audible-underrun count.

The session log records `Display window closed by user`, followed by normal execution completion at 2,814,150,000 instructions, final diagnostic output and `Sound subsystem closed`. Only the launcher remains running afterward. This is evidence of an orderly window-close path, not a confirmed host crash. The close message does not identify its origin: Escape, a window-close request or launcher Stop can use this path. The user's unexpected-closure report remains recorded; its triggering action has not been established. Sustained gameplay and normal audio are unverified.
