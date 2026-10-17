# digiBLAST Soundtrack Reference

## Source boundary

The supplied [Wade Hixton's Counter Punch (digiBLAST) - full soundtrack](https://www.youtube.com/watch?v=KRqyABxFj2I), uploaded by iDog Hacker, describes its source as "Converted directly from the XM files in the game data". It is therefore a conversion of the game music assets, not a recording of the console running the game. The converter, settings and hardware playback timing are not established. It can test musical correspondence but cannot establish original-console animation, audio scheduling or live output stability.

Only the first 45 seconds of its AAC audio stream are retrieved for local signal analysis. No video or additional game runs are needed. The task budget is 15 minutes. The OceanBlast input is the existing normal-build generated PCM from [the earlier diagnosis](85_wade_pcm_and_audio_recovery.md), not a loopback recording of the installed PGO executable. Media remains in ignored local build directories.

## Comparison

The reference is decoded to mono 22,050-Hz PCM. The same 20-ms RMS blocks, three-block smoothing and normalized sliding-envelope search used for [the local GBA comparison](87_local_gba_wade_comparison.md) are applied to OceanBlast music seconds 0.2-10. Searching scale 0.5-2 over the available 45-second reference identifies a matching section around reference second 32, with scale 1.000 and correlation 0.890. This offset identifies a corresponding musical passage; it is not a boot-time or missing-audio measurement.

A local refinement gives the following independent windows:

| OceanBlast music interval | Reference seconds per OceanBlast second | Envelope correlation |
| --- | ---: | ---: |
| 0.2-5.0 s | 1.001 | 0.784 |
| 5.0-10.0 s | 0.998 | 0.993 |
| 0.2-10.0 s | 0.998 | 0.895 |

The matched structure therefore proceeds at approximately the same tempo, within about 0.2% in this short fit. Those fit increments are not a hardware clock calibration. The early mismatch is larger than the later one and is retained as an unresolved difference, not hidden by selecting only the strongest window.

The fixed-time spectral-peak check finds its best median correlation of 0.557 at an OceanBlast/reference frequency ratio of 0.998, compared with 0.376 at exactly 1.0. This is consistent with similar pitch and does not indicate a large uniform pitch shift. Different mixing, conversion and startup behavior prevent a claim of sample equality or complete musical correctness.

## Updated diagnosis

The earlier approximately 8.7% tempo difference against the GBA port remains a measured cross-version observation. The new digiBLAST asset conversion matches OceanBlast at approximately unity tempo, so the GBA result is insufficient evidence for an OceanBlast tempo defect. A blanket 8.7% CPU or sample-rate correction would not be supported. Original digiBLAST hardware timing remains unverified by either reference.

The reported recurring live stutter remains an open P0 issue. Direct listening feedback specifically identifies the different-sounding beginning as the stuttering/strange section. The envelope comparison independently shows its largest mismatch during approximately the first two music seconds. This makes startup sample generation and the first live playback blocks the next diagnostic priority; the later strong match must not hide that failure. A converted soundtrack cannot by itself decide whether the startup difference is missing/repeated notes, mixing, source conversion or emulator behavior.

Agreement between later offline generated PCM and an independently converted soundtrack does not accept the live output. The next bounded task should compare the actual live output against generated PCM in those first music seconds and correlate compressed/repeated/missing audio with queue consumption and guest sample delivery, before widening to the recurring later bursts. No implementation, preset or installed executable is changed by this analysis.

## Evidence

[Provenance](validation/2026-10-17_digiblast_soundtrack_provenance.json) retains the public source description, interval and input hashes. [Measurements](validation/2026-10-17_digiblast_soundtrack_measurements.json) retain the search and split-window results; [envelope samples](validation/2026-10-17_digiblast_soundtrack_envelope.csv) contain only derived RMS scalars.

The [search](validation/2026-10-17_digiblast_soundtrack_search.py), [refinement](validation/2026-10-17_digiblast_soundtrack_refine.py) and [spectral](validation/2026-10-17_digiblast_soundtrack_pitch.py) scripts require Python, NumPy and Matplotlib. Restore them as `search.py`, `refine.py` and `pitch.py` in the ignored `build/2026-10-17-digiblast-soundtrack-reference` directory; provide `audio.wav` and the existing private OceanBlast input at the paths in their source, then run in that order. No media or ROM is published.
