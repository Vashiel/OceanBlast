# Manual Cartridge Validation

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
