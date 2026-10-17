# Local GBA Wade Comparison

Follow-up: the [digiBLAST XM soundtrack comparison](88_digiblast_soundtrack_reference.md) matches the generated PCM at approximately the same tempo. The GBA discrepancy below is not sufficient evidence for an OceanBlast tempo defect or a speed correction.

## Scope and inputs

The supplied GBA version of Wade Hixton's Counter Punch is run in the supplied mGBA 0.10.5 win32 application. The application, portable configuration and ROM are copied to an ignored working directory with a separate save. The original ROM, save and mGBA configuration are left in place. The task allowance is 20 minutes, including capture, analysis and reporting; title-screen evidence is sufficient, so no fight or second cartridge is tested.

mGBA's built-in lossless recording captures native 240-by-160 video and WavPack audio. Recording begins on the title screen, followed by one reset within the recording to capture the music introduction. The START measurement uses the initial title interval before that reset. The audio measurement uses the reset music sequence. This is a local emulator reference for the GBA port, not an original digiBLAST hardware recording or proof that both ports intentionally use identical timing.

OceanBlast reuses the known title capture from [the previous reference task](86_gba_title_reference.md): installed executable `57fac1d7b69e91c61337ac3ca08927bcce6cb67eaab420d3b1e935e2de828410`, ratio 6, normal 120-MIPS cap, fresh frontend settings, copied EEPROM and no input. Its audio comparison uses the existing normal-build generated PCM from [the PCM diagnosis](85_wade_pcm_and_audio_recovery.md), not a loopback recording of the installed PGO executable. These two OceanBlast sources must remain distinct.

[Input provenance](validation/2026-10-17_local_gba_provenance.json) records hashes. ROMs, saves, audiovisual recordings and derived artwork remain outside source control.

## START animation

White pixels in the START rectangle (x=84..158, y=131..140) distinguish visible and hidden states. Linear fits to rising and falling edges estimate the complete blink period. Native mGBA frame timestamps use 23891/400 fps; the OceanBlast capture samples wall time at 30 fps.

| Source | Measurement interval | Complete blink cycle |
| --- | --- | ---: |
| Local GBA in mGBA | 0-10 video seconds | 0.53577 s, 32 GBA frames |
| OceanBlast | 14.5-24.5 capture seconds | 0.51219 s |

OceanBlast's observed blink frequency is about **4.6% higher**. This agrees with the earlier supplied-video reference. Frame sampling limits individual transition accuracy; the fitted decimals are not a claim of equivalent capture precision. This is one title animation, not a general game-speed benchmark.

## Music structure and timing

Both sources are analyzed as mono 22,050-Hz PCM. RMS envelopes use 20-ms blocks and a three-block moving average. Comparing only the detected first audible samples is misleading: the corresponding musical section has an offset of about two seconds between versions. An initial narrow offset search produces a weak boundary fit and is superseded by a broader sliding search.

The broader search compares OceanBlast music seconds 0.2-10 against the available post-reset GBA music, allowing 0.5-2 reference seconds per OceanBlast PCM second. It finds an envelope correlation of **0.865** near a scale of **1.085**. A local refinement and separate half-window fits give:

| OceanBlast music interval | GBA seconds per OceanBlast second | Envelope correlation |
| --- | ---: | ---: |
| 0.2-5.0 s | 1.088 | 0.763 |
| 5.0-10.0 s | 1.087 | 0.950 |
| 0.2-10.0 s | 1.087 | 0.864 |

The refined correspondence is approximately `GBA music time = 1.973 + 1.087 * OceanBlast music time`, measured from each detected music onset. Thus 9.8 seconds of generated OceanBlast music correspond to about 10.65 seconds in the local GBA reference: the matched rhythmic structure advances roughly **8.7% faster** in OceanBlast PCM. The offset is an alignment parameter; it does not establish that audio was lost or that startup silence is itself a tempo error. Different port introductions or arrangements remain possible.

A separate exploratory spectral-peak comparison uses fixed envelope-aligned times, 4096-sample Hann windows, 150-7000 Hz and subtraction of the local broad spectral shape. Its best median correlation is 0.446 at an OceanBlast/GBA frequency ratio of 0.996, versus 0.223 at exactly 1.0. This does **not** support an 8.7% uniform pitch increase alongside the tempo difference. It is not an exact tuning measurement or proof that every note/channel is correct. No playback-rate correction is justified from the envelope result alone.

The recording's container duration tags are inconsistent: audio reports 139.268 seconds, video 107.572 seconds, while decoded audio contains exactly 107 seconds. The 214 decoded audio frames contain 7,012,352 samples at 65,536 Hz, with continuous half-second timestamps and no gaps over 2 ms. Audio feature comparison uses decoded sample counts, not container duration tags. No global audiovisual synchronization conclusion is drawn across the reset.

## Result and next action

The local reference supplies a much stronger musical alignment than the earlier narrow supplied-video search. It establishes a reproducible cross-version tempo discrepancy to investigate, alongside the smaller title-blink discrepancy. It does not establish whether either difference is an emulation defect or an intentional port difference, and it does not explain the reported recurring live stutter by itself.

The next bounded diagnosis should inspect the relationship between guest music/event timing, DMA sample production and live queue consumption in the corresponding title interval. Keep tempo generation and live output interruptions as separate acceptance questions. Do not apply a blanket 8.7% sample-rate or CPU-speed adjustment: the animation and music differences disagree, and the spectral check does not show the same pitch shift. A digiBLAST hardware reference would be needed to determine the intended port timing conclusively.

No implementation, timing preset or installed executable is changed. Live Wade audio remains an open P0 issue; this comparison is diagnostic progress, not a sound fix or another CPU speedup.

## Reproduction data

[Measurements](validation/2026-10-17_local_gba_measurements.json) retain blink edges, search ranges, split fits, spectral result and sample/timestamp checks. [Aligned envelope samples](validation/2026-10-17_local_gba_envelope.csv) contain only derived scalar RMS values.

The archived scripts are [initial frame/audio analysis](validation/2026-10-17_local_gba_analyze.py), [broad audio search](validation/2026-10-17_local_gba_search.py), [split-window refinement](validation/2026-10-17_local_gba_refine.py) and [spectral check](validation/2026-10-17_local_gba_pitch.py). They require Python, NumPy and Matplotlib, and expect the private files under the ignored build paths shown in their source. The refinement and spectral scripts read the search script's setup from that working directory. Restore the four scripts there as `analyze.py`, `audio_search.py`, `refine.py` and `pitch_check.py`, then run in that order. The initial script recreates measurements; retain the published sample/timestamp metadata separately if rerunning. FFmpeg prepares the mono analysis WAV and raw RGB frames. These data permit review of the analysis without publishing game content.
