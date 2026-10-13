# Wade Fight Cadence, Host Throughput and the 6× Preset

## Counter and timing reference

The round counter in *Wade Hixton's Counter Punch* is tied to game frames: `_PlayRound` decrements it once for every 64 increments of `GLOBAL_nFrameCounter`. `vblank_wait()` requests at least 16 ms per frame through `SDL_GetTicks`, so one counter unit has a programmed minimum of 1.024 s. It is not an independent one-second hardware timer. The [binary analysis](41_wade_binary_and_s3c2410fb_analysis.md) identifies both code paths, and the [native 12-bpp LCD measurement](42_wade_gph8_lcd_mode.md) finds approximately 144 million interpreter step calls per counter unit during the Rocco fight.

The [GBA playthrough reference](https://www.youtube.com/watch?v=1lI49kjX91c&t=219s) averages approximately 0.93–0.95 counter units/s over the visible `89 → 75` and `89 → 70` intervals. It is a different port, not a measured digiBLAST console clock. Matching its cadence is a comparison target, not proof of hardware-equivalent speed.

## Host capacity and power state

An identical 23.6-billion-step Wade replay used the same installed PGO executable, cartridge, blank EEPROM, input script, GUI, sound, native LCD format, manual ratio 4 and disabled host pacing. The active-fight window was modeled seconds 275–295. Per-second [performance records](validation/2026-10-13_wade_host_power.csv) show:

| Laptop state | Samples in fight window | Mean executed steps/s | Range | Audio queue observation |
| --- | ---: | ---: | ---: | --- |
| Battery session | 22 | 75.0 M | 52.0–81.9 M | 27 additional empty-queue observations |
| Mains session | 12 | 131.8 M | 123.6–139.4 M | Zero additional empty-queue observations; unpaced audio overflowed |

These are host throughput measurements, not guest clock estimates. The power conditions differed, and the comparison does not isolate charging, Windows power settings or thermal behavior. An unpaced run also overproduces audio when it advances guest time faster than wall time; its dropped-sample count is not a playback-quality result. The battery result explains why a 120-MIPS target cannot be sustained on the measured low-power setup even though the same binary runs much faster on mains.

## Paced first-fight validation

The [ratio-6 input fixture](../tests/wade_ratio6_fight.txt) stops Start pulses before the game enters the ring. Replaying later pulses from the 4× fixture pauses the fight at 6×; performance data from that paused scene must not be used as fight evidence. With a blank EEPROM, GUI, sound, native LCD mode, 500-million-step framebuffer snapshots and a 10-billion-step limit, the new automatic 6× build reaches the first Rocco fight. The tested PGO executable has SHA-256 `97203e3e51264a569bc6b36e39ecf87ab1184e63f6c07a24e11571166462a666`. The [per-second profile](validation/2026-10-13_wade_ratio6_performance.csv) records:

| Active-fight step interval | Mean steps/s | Mean pacing | New empty queues | Dropped samples | Minimum queued audio |
| --- | ---: | ---: | ---: | ---: | ---: |
| 7.4–8.5 billion (`9` complete host intervals) | 120.002 M | 100.001% | 0 | 0 | 2,048 frames |

Decoded 12-bpp snapshots show the Rocco fight at 7.5, 8.0 and 8.5 billion steps. The HUD counter reads 84, 80 and 77 respectively. Seven counter units across one billion steps at 120 million steps per modeled second take approximately 8.33 modeled seconds, or **0.84 units/s**. This is about 1.5× the previously measured ratio-4 LCD cadence of 0.556 units/s and approximately 10–12% below the GBA video reference. Counter readings are sampled at only three points and the fight ends shortly afterward, so this is a local scene measurement rather than an average for the whole game. At each of those three step counts, the automatic build's framebuffer SHA-256 matches the manual 6× replay byte for byte.

The automatic ratio-6 run records 60 empty-queue observations before the measured fight, during startup, menus and loading. The fight interval adds none. No listening or original-console comparison was performed for this run. The 6× Wade cartridge preset is therefore a provisional improvement to the active-fight cadence on a host that sustains 120 MIPS, not an audio-fidelity or hardware-speed certification. On a slower or battery-limited host, the manual 4× or lower setting remains available. Other cartridge presets are unchanged.
