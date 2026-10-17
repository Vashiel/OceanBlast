# Wade Music Startup: Generated PCM and DMA Delivery

## Question and scope

Listening feedback identifies the beginning of the title music as stuttering and strange. The [XM soundtrack comparison](88_digiblast_soundtrack_reference.md) shows a large envelope mismatch in approximately the first two music seconds, followed by a close match. This task asks whether the startup problem is already present before Windows playback, and whether DMA delivery or CPU availability explains it. Its allowance is 20 minutes including compilation, one timing capture, one diagnostic CPU-budget variation and reporting. No gameplay or other cartridge is tested.

The normal-build PCM already contains the reported startup difference without an audio device. That excludes Windows buffering as the sole explanation for this particular beginning; it does not exclude additional live-output problems. An asset conversion is not a console recording, so source mixing, title effects and conversion behavior remain possible contributors to differences. A subsequent ring-buffer trace below identifies one actual repeated-audio event independently of that reference.

## DMA timing capture

An isolated normal GCC 16.2.0 O3 x86-64 unity probe uses current runtime source, the same Wade ROM, copied EEPROM, ratio 6 and 4,096-instruction execution batches as the previous PCM diagnosis. Each audio callback records the emulated peripheral tick counter, raw sample offset, count, rate, DMA registers and PC. There is no window, host pacing or audio device. The capture stops after three billion instructions.

The raw PCM is byte-identical to the previous capture. Thus instrumentation does not introduce a new generated-audio variant in this fixture. The first nonzero block is callback 2659, near guest second 12.393 and concatenated PCM second 10.214. Those times differ because the PCM file concatenates delivered samples; it does not insert silence for periods without audio callbacks. They must not be treated as a calibrated live AV timeline.

The rate changes from 87,890 to 21,972 Hz near guest second 3.823, about 8.57 seconds before music becomes nonzero. It stays at 21,972 Hz throughout the examined music startup. Resampling therefore does not change configuration at music onset.

| Music interval in guest time | Callbacks | Largest delivery interval beyond the current block's audio duration | Intervals over 1 ms beyond block duration |
| --- | ---: | ---: | ---: |
| First 2 seconds | 343 | 0.138 ms | 0 |
| Seconds 2-5 | 515 | below 0.001 ms | 0 |
| Seconds 5-10 | 859 | below 0.001 ms | 0 |

The first two seconds accumulate about 10.98 ms of positive interval excess. There are no music delivery gaps over 1 ms in the recorded run. The callback clock is observed within instruction execution, before the current peripheral tick counter increment; this introduces at most one 50-ns tick of observation offset. These figures concern emulated delivery timing, not host scheduling or waveOut starvation.

The first approximately 2.5 PCM seconds contain 431 callback blocks, of which 430 have distinct raw hashes. This does not support a simple repeated-whole-block explanation. Partial stale samples, guest mixing errors and repeated musical notes are not excluded by block hashes.

## One diagnostic CPU-budget variation

A second isolated probe retains peripheral timing and audio rates but increases legacy CPU steps per peripheral tick from 6 to 12. It ends at guest second 16, sufficient for startup and a short later musical section. This intentionally changes emulated CPU availability; it is not a host optimization benchmark, hardware calibration, proposed default or semantically equivalent replay. It runs without a host audio device and does not replace the installed executable.

For each capture, the corresponding soundtrack passage is aligned using music after second 2, excluding the startup being investigated. The refined reference/PCM time scale is 0.998 for both captures, with later envelope correlation near 0.991. The alignment offset differs between captures and must not be interpreted as a lost-audio duration.

| Music window | Existing ratio 6: envelope correlation | Diagnostic ratio 12: envelope correlation |
| --- | ---: | ---: |
| 0-0.5 s | 0.225 | -0.385 |
| 0.5-1 s | 0.295 | -0.288 |
| 1-2 s | 0.142 | 0.975 |
| After 2 s | 0.991 | 0.991 |

The higher CPU budget does not repair the beginning: its first second still fails the reference comparison. It does make the subsequent 1-2-second window closely match, where the existing profile remains different. A 0.5-second sliding correlation window (20-ms increments, threshold 0.9) has its last below-threshold start at 1.66 seconds for ratio 6 and 0.98 seconds for ratio 12 within the first three seconds. This local metric supports CPU-sensitive startup behavior; it is not listening acceptance or proof of a particular mixer defect.

## Ring-buffer read/write trace

An isolated copy of the runtime instruments guest RAM writes and DMA sample reads in the observed two-buffer physical range `0x30350000..0x303507ff`. Each byte records its latest guest write generation and the generation consumed by the previous DMA read. Reading a byte again without an intervening write is marked stale even if the copied PCM block as a whole is different. This is not inferred from equal sample values alone. An initial narrower trace covers one buffer; the retained trace extends the same check to both buffers. Both remain diagnostic copies, with no production hooks installed.

The instrumented three-billion-instruction replay preserves byte-identical raw PCM and final SDRAM. In the first two music seconds it observes **1,454 stale bytes**, 173,196 freshly written bytes and no never-written bytes. A further 190 callback bytes originate outside the tracked range, so that initial window is not claimed to have complete physical-buffer coverage. Music seconds 2-5 and 5-10 have full callback-byte coverage in the tracked buffers and **zero stale bytes**.

| Callback | Guest seconds after music starts | Stale bytes in 512-byte callback |
| --- | ---: | ---: |
| 2971 | 1.82818 | 412 |
| 2972 | 1.83400 | 512 |
| 2980 | 1.87210 | 428 |
| 2981 | 1.87793 | 102 |

Callback 2972 is also byte-identical to callback 2970, and contains no zero-valued samples: a complete already-consumed audible fragment is repeated. That block contains 128 stereo frames, about 5.83 ms at 21,972 Hz. Across the four callbacks, the tracked stale bytes are equivalent to about 16.54 ms of stereo audio storage; this is not a claim that every stale byte forms complete stale stereo frames or one contiguous pause.

This supplies concrete evidence of old ring data being replayed during the reported startup, without relying on a converted soundtrack or Windows playback. It does not prove whether guest CPU availability, producer scheduling, DMA/interrupt semantics or another cause makes the producer late. Nor does this localized event explain all of the earlier first-second waveform mismatch or later live bursts.

## Decision

The generated beginning and the recurring live bursts need separate treatment. A Windows-only buffer adjustment cannot correct a difference already in offline PCM. The measured DMA cadence does not show large startup gaps or a contemporaneous rate switch. Extra guest CPU availability shortens the mismatching startup interval but leaves an initial failure, so no ratio/preset change or executable is installed.

The next investigation should explain the missed ring-buffer renewal around music second 1.83: correlate producer writes, reload addresses and DMA interrupt handling before changing timing or discarding old samples. The separate first-second mismatch may still involve guest mixing or title effects; a soundtrack-only conversion cannot settle that distinction. Inspect this startup boundary before further live-buffer variants; then correlate the separate recurring live bursts with actual output and queue delivery.

This task localizes the reported startup difference to a stage before host playback and demonstrates CPU-budget sensitivity. It does not establish the exact cause, fix the sound, accept ratio 12 or claim new performance gains. The installed executable remains `57fac1d7b69e91c61337ac3ca08927bcce6cb67eaab420d3b1e935e2de828410`.

## Evidence

[Timing summary](validation/2026-10-17_wade_startup_timing.json), [CPU-budget comparison](validation/2026-10-17_wade_startup_budget.json), [startup block observations](validation/2026-10-17_wade_startup_blocks.json) and [provenance](validation/2026-10-17_wade_startup_provenance.json) contain numeric results and hashes only. [Callback timing](validation/2026-10-17_wade_startup_callbacks.csv), [ring generations](validation/2026-10-17_wade_startup_ring.csv) and [ring summary](validation/2026-10-17_wade_startup_ring_summary.json) retain delivery and reuse observations. The [capture source](validation/2026-10-17_wade_startup_capture.cpp), [budget-variation source](validation/2026-10-17_wade_startup_budget_probe.cpp), [timing analysis](validation/2026-10-17_wade_startup_analyze.py) and [reference comparison](validation/2026-10-17_wade_startup_compare.py) document the method. The [ring preparation script](validation/2026-10-17_wade_startup_prepare_ring_trace.py) creates the isolated runtime copy and probe from the capture source; [ring analysis](validation/2026-10-17_wade_startup_analyze_ring.py) checks byte generations and matching PCM blocks. The C++ probes include the bus and CPU implementations and compile with `-Isrc`; both take ROM, EEPROM and output-prefix arguments, and the budget probe adds the ratio argument. The ring probe uses its copied runtime include path instead. Restore diagnostic files under the ignored startup build directory using the short filenames referenced in the scripts. Python scripts require NumPy and Matplotlib and the ignored private inputs named in their source. ROMs, EEPROM, PCM, WAV, RAM dumps and derived game images remain local.
