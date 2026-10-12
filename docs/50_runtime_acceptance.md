# Runtime Acceptance of Reviewed Frontend Changes

## Test Conditions

The reviewed source was built as an optimized PGO unity executable, trained on Superstar Chefs and Rayman 3 with 2.1 billion steps each at ratio 3. Candidate SHA-256: `eeed4b323b34406c578b724d6df0f5dbb0f64fcaf146ca3b51c171ac1a739867`.

Live replays ran sequentially with GUI, sound, profiling, a plain 2x window and a separate EEPROM image. Automatic CPU presets selected Chefs/Wade ratio 3, Crazy Jack ratio 2 and Rayman ratio 4. Language was set to `keep` and the scripted scene replays started with blank EEPROM. Framebuffer snapshots were inspected to establish the reached scene; a title or selection screen was not counted as gameplay.

[Scene-specific measurements](validation/2026-10-12_review_scenes.csv) exclude earlier menus and startup. Each linked per-replay CSV retains the original performance fields. The `speed_percent` value measures the configured instruction-pacing target. It does not establish original-console speed. `present_fps` counts host image submissions, not independently measured game frames or display scans. Audio-queue observations do not measure sound fidelity or audiovisual synchronization.

## Game Scenes

| Cartridge | Verified scene | Ratio | Mean submissions/s | Empty queues in scene | Dropped samples in scene |
| --- | --- | ---: | ---: | ---: | ---: |
| Superstar Chefs | First level | 3 | 15.22 | 0 | 0 |
| Wade Hixton's Counter Punch | First fight and knockdown | 3 | 13.38 | 0 | 0 |
| Crazy Jack | First level | 2 | 26.99 | 0 | 0 |
| Rayman 3 | First level | 4 | 40.18 | 0 | 0 |

All four measured sections sustain approximately 100% of their configured pacing targets and the bounded runs terminate normally. Rayman's 16-billion-step run includes scripted A presses and one manual Enter press during the story dialog. It is functional scene evidence, not a deterministic paired replay. The replay fixture continues dialogue confirmation and movement through the first level; it does not assert a fixed wall-time arrival.

Rayman still displays a magenta background in the first level. The same defect appears in the existing project preview. It remains a graphics limitation, independently of the measured throughput and queue behavior. Wade's submission rate remains low and still requires hardware-speed calibration; reaching the pacing target does not resolve the original complaint of slow combat.

A separate early Crazy Jack replay samples the first 600 million steps every 20 million steps. The captured logo, console splash and publisher image are centered and decoded coherently. This does not prove every intermediate scanout is correct or dismiss previously reported startup artifacts.

Replays: `tests/chefs_auto_confirm.txt`, `tests/wade_gameplay_confirm.txt`, `tests/crazy_gameplay_confirm.txt`, and `tests/rayman_gameplay_confirm.txt`.

## Paired Build Comparison

A [controlled Superstar Chefs comparison](validation/2026-10-12_chefs_review_comparison.csv) runs the previous installed PGO executable and the candidate for four billion steps with identical ROM, input script, starting EEPROM, CPU ratio and host pacing. Both sustain 60.00 MIPS and record zero empty queues or dropped samples. Final SDRAM, active framebuffer and displayed CPU-register hashes match exactly. The small difference in sampled host image submissions is not a guest execution change or a general performance claim.

## RealVideo Output

At automatic ratio 3, Sonic X (IT/ES) delivers 588 complete source sweeps in the measured playback section, about 11.76 submissions/s. Winx (IT/ES) delivers 641, about 12.81/s. Both use the native 160-row capture path and report zero abandoned partial sweeps in those intervals. Sonic records one empty audio-queue observation; Winx records none. Neither drops samples.

A Sonic diagnostic run at manual ratio 4 produces more raw submissions, about 24.83/s, but fails to activate native complete-frame capture and records three empty queues during playback. This is not evidence of more complete decoded movie frames. The automatic decoder-export probe depends on mapped guest metadata and did not identify the decoder in that run. Higher CPU allowance therefore remains a diagnostic setting rather than an accepted universal video preset. The ratio-3 preset is retained.

One independently extracted Winx episode identifies as RV40, 240x160, at 15 fps according to `ffprobe`. This establishes the cadence of that episode only. A 15-fps source cannot supply 60 distinct frames per second; source cadence, incomplete guest rendering and audio starvation must be measured separately. These tests do not establish smooth, synchronized playback across all cartridges.

Dedicated media input is exercised during a Sonic episode using `tests/video_dedicated_media_controls.txt`. Play/Pause and held seek change the playback output, and Stop is followed by cessation of frame changes and temporary audio submission. A return to the episode selection screen is not established by this replay. GPIO/interrupt routing and host pulse behavior have separate ROM-free regression coverage.

## Remaining Compatibility Boundaries

- Rayman first-level background rendering requires palette/render-path analysis.
- Wade combat cadence requires an original-hardware reference and scene-specific timing calibration.
- Native RealVideo capture recognition must remain reliable across CPU ratios, process mappings and languages. Other video cartridges require their own playback and listening checks.
- Sound quality and audiovisual synchronization require listening/reference acceptance; zero queue events alone cannot establish either.
- The damaged international Winx combined image remains unmodified and unsupported where its filesystem cannot be read.
- MP3 firmware expects USB/SCSI music storage. OHCI enumeration, persistent storage and original player playback remain unimplemented; see [MP3 Firmware](48_mp3_cartridge_firmware_probe.md).
