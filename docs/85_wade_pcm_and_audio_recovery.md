# Wade PCM Comparison and Audio Recovery

## Playback rejection and rollback

Listening feedback rejects the [additional-Wade PGO candidate](84_wade_title_audio_starvation.md): music sounds severely incorrect and repeatedly stutters. Lower observed empty-queue counts did not establish acceptable music. The rejected executable is preserved outside source control, and the previous local executable (`57fac1d7b69e91c61337ac3ca08927bcce6cb67eaab420d3b1e935e2de828410`) is restored before further work. This does not accept the preceding executable's Wade sound.

The diagnostic allowance is 20 minutes. No Chefs replay, broad game sweep or throughput optimization is part of this audio investigation.

## Generated audio comparison

Two isolated normal GCC 16.2.0 O3 x86-64 unity probes compare the execution core from `6c7870f`, before the retained shortcuts, with current runtime source at `19d5518`. Critical archived reference sources are checked against their Git revision. Each probe uses the same ROM, copied EEPROM, three-billion-instruction limit, ratio 6 and 4,096-instruction batches through `Machine`/`ExecutionScheduler`. No host audio device, window or pacing is involved. The [probe source](validation/2026-10-17_wade_pcm_probe.cpp) and [metadata](validation/2026-10-17_wade_pcm_comparison.json) retain the method and identities.

| Output | Result |
| --- | --- |
| Raw guest PCM | 2,464,052 bytes, byte-identical |
| PCM after the existing 22,050-Hz stereo conversion | 2,011,956 bytes, byte-identical |
| Audio callback/register observations | 4,823 callbacks, identical |
| Final SDRAM and stdout/CPU dump | Identical |

The converted file contains 502,989 stereo frames (22.8113 seconds), including about 10.2135 seconds of exact digital silence at its beginning. No samples reach signed 16-bit clipping limits. The [signal statistics](validation/2026-10-17_wade_pcm_statistics.json) and [callback events](validation/2026-10-17_wade_pcm_events.csv) retain these observations. PCM, WAV files, EEPROM and RAM dumps remain local.

An offline WAV made from these samples is auditioned: the reported beginning stutters, then the music sounds normal; the long initial silence is also confirmed. This separates a reported startup defect in generated audio from the severe recurring live-output problem. Identical PCM does not establish original-console music fidelity, nor does this normal-build comparison verify the exact installed PGO executables. It establishes no new generated-PCM difference from the recent core optimizations in this fixture.

## Actual launcher observation

A copy of the restored executable is started through its actual launcher and embedded child, using cloned preferences at scale 4 with FPS logging enabled and a separate EEPROM copy. The title is observed without buttons. The [recorded intervals](validation/2026-10-17_wade_embedded_audio.csv) cover 121.026 seconds at a weighted 119.4053 MIPS, against a 120-MIPS target. They report 11 empty queues and zero discarded samples; six empty events are already counted before recorded playback reaches the nonzero PCM region. The launcher is then closed. This manually ended diagnostic has no instruction-limit or final-state acceptance.

Listening feedback describes short recurring bursts of stutter/compressed-sounding playback with normal sections between them. Neither the near-target mean nor the low drop count accepts that output. The [launcher metadata](validation/2026-10-17_wade_embedded_audio.json) records the configuration. Persistent user preferences and saves are not changed.

## Rejected recovery candidate

Only device initialization pauses playback until two complete output blocks are available. After a later empty queue, the next single block restarts immediately, leaving little reserve against another scheduling delay. An isolated candidate pauses the empty device and requires two newly submitted blocks before restarting. Its frame threshold is relative to the current submission count, rather than the initialization count.

The proposed change preserves PCM values, sample rate, pitch, maximum queue capacity and the existing two-block startup reserve. It adds no sleep to the emulation thread. Rebuilding that reserve after starvation can add a refill delay; it does not supply missing sustained CPU throughput or repair incorrect guest-generated music.

Four [device checks](validation/2026-10-17_audio_recovery_checks.txt) pass: initial playback consumes every submitted frame, one refill block stays paused after starvation, the second block resumes and consumes every refill frame without discards, and reopening resets playback position. Direct Wade listening feedback then reports **worse** output. The candidate is rejected and never replaces the restored executable. All three production audio/test files are restored to their preceding content.

The [rejected patch](validation/2026-10-17_audio_recovery_rejected.patch) is archived with [build/decision metadata](validation/2026-10-17_audio_recovery_rejected.json). It uses zero context (`git apply --unidiff-zero` for an isolated reproduction). The attempted window close fails during the interrupted turn; the retained CSV consequently contains 166 intervals, not the intended short controlled acceptance window. It is an interrupted diagnostic and must not be treated as a matched performance comparison or acceptance result. Negative listening feedback is sufficient for rejection; no further playback variant is tried. Startup generated-audio quality and live audio delivery remain open P0 work.
