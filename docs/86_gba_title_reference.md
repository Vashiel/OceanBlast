# GBA Title Reference: Blink Timing and Audio Waveform

## Source and scope

The supplied [Wade Hixton's Counter Punch GBA playthrough](https://www.youtube.com/watch?v=1JeaEPX1ue4), titled "Wade Hixton's Counter Punch (GBA) full playthrough" by Reinc, supplies a useful cross-platform reference. Only its first 45 seconds are retrieved for local analysis. The title/music starts around video second 11.6; the START-button measurement excludes the preceding logos and the later menu input.

This is a recorded GBA playthrough, not a calibrated digiBLAST hardware capture. Its physical/emulated capture setup and any production timing changes are unverified. Similar artwork or music does not establish identical timing across the two versions. The reference-analysis allowance was 15 minutes and was exceeded; the task ends with this report rather than additional playback experiments.

Video, audio, ROM-derived frames and generated comparison figures remain in ignored local build directories. [Provenance and measurements](validation/2026-10-17_gba_reference_provenance.json) identify the supplied source, selected stream and local analysis inputs without publishing signed download URLs or media.

## START-button measurement

The GBA clip is 480 by 320 pixels. Although extractor metadata rounds its rate to 30 fps, the downloaded video stream reports exactly 23891/800 fps (29.86375); that rate is used for frame timestamps. It is downscaled to the common 240-by-160 LCD size. White, nearly neutral pixels in the START-text rectangle (x=84..158, y=131..140) separate visible/hidden states into two clusters. Representative frames from both states are inspected for each source. Period estimates fit same-direction edges across multiple cycles rather than relying on one adjacent-frame difference.

The OceanBlast comparison reuses the restored PGO executable `57fac1d7b69e91c61337ac3ca08927bcce6cb67eaab420d3b1e935e2de828410`, ratio 6, normal 120-MIPS cap, fresh frontend settings, the same copied EEPROM and no input. One three-billion-instruction GUI/audio run records 751 decoded LCD frames at 30 fps. Compilation and other emulator runs do not overlap this recording. It reaches its requested limit.

| Source | Stable title window | Full visible/hidden cycle |
| --- | --- | ---: |
| GBA playthrough | 12.5-22.0 video seconds; detected edges end near 20.9 | 0.5358 s |
| OceanBlast recording | 14.5-24.5 recording seconds | 0.5122 s |

The observed OceanBlast blink rate is about 4.6% higher in these windows. This is not evidence of a grossly slower title animation. Individual transitions have one-frame sampling granularity (about 33.5 ms in the reference and 33.3 ms in the emulator recording); fitted values do not imply sub-millisecond capture accuracy. This one animation does not establish gameplay, audio-clock or original-console accuracy. No emulator timing/preset change is made from this cross-platform difference.

## Audio reference

A mono 22,050-Hz analysis copy of the reference and the existing offline OceanBlast PCM are plotted as waveform envelopes, with a second figure aligning their estimated music onsets to zero without time stretching. A spectrogram pair retains their frequency/time structure. Reference music onset is detected near video second 11.57; OceanBlast's generated file contains about 10.21 seconds of preceding digital silence. Those absolute offsets belong to different boot sequences and are not a music-speed ratio.

Waveforms differ in amplitude and detailed structure. An exploratory RMS-envelope alignment across a 0.8-1.2 time-scale range reaches only r=0.249. A pitch-class comparison also supplies no convincing match. These weak fits must not be converted into a purported tempo correction, pitch correction or emulator-speed factor. Different mixing/arrangement, source behavior and recording effects remain possible explanations; the plots alone do not select one.

The [measurement record](validation/2026-10-17_gba_reference_measurements.json) retains transition times, fitting windows and exploratory search limits. The reference is useful for the title-animation cadence and further annotated audio comparisons. Current recurring live stutter remains an open issue; neither this reference extraction nor the rejected buffer candidate resolves it. The installed executable stays at the restored version.
