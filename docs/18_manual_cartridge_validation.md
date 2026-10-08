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
