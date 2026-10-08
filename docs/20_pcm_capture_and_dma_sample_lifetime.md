# PCM Capture and DMA Sample Lifetime

## 1. Reproduced Audio Defect

DigiQUAD produces objectionable audio even when the host queue reports no ongoing starvation. A generated-PCM capture taken before Windows playback was also reported to sound similarly defective. This separates at least part of the fault from waveOut scheduling. The capture includes the emulator's PCM interpretation and rate conversion; it is not a recording from original hardware.

The preceding [512-byte streaming correction](19_dma_streaming_and_interrupt_delivery.md) retained a smaller version of the original lifetime defect. Samples were read from SDRAM when a callback-sized portion had elapsed. A game could reuse an earlier, already consumed region within that portion before the delayed read. Batching host submissions must not delay sampling the emulated source memory.

## 2. Consumed-Sample Capture

DMA 2 now copies each consumed 16-bit sample into a separate staging buffer as peripheral time advances. A 32-bit DMA transfer item is captured atomically. Host callbacks still aggregate approximately 512 bytes in complete stereo frames, with the remaining complete frames forwarded at completion. Stopping a transfer flushes samples already captured without reading future source data; an incomplete stereo pair is discarded. This preserves source-data lifetime without submitting a waveOut header for every frame.

A left-channel sample can be consumed before its right-channel partner. A regression overwrites RAM between those events and verifies that the first channel retains its original value. The measured DigiQUAD excerpt is byte-identical whether captured per stereo frame or per 16-bit transfer item; this additional correction does not explain its residual reported crackling.

Repeated ON writes during an active transfer also preserve the current source and progress. A diagnostic counter records these writes. The DigiQUAD capture observes zero such writes, and this guard alone produces byte-identical PCM; it is not the explanation for the measured improvement.

The current model remains an approximation. Bulk `Bus::tick()` calls cannot recover memory values from intermediate times, transfer-item FIFO behavior is not fully implemented, and 8-bit IIS/codec behavior remains unresolved. Normal CPU execution advances peripherals after each step, enabling the finer memory capture.

## 3. Controlled DigiQUAD Comparison

Both captures start from reset with the same cartridge, default device initialization and 1.3 billion CPU steps. The baseline contains 1,224,668 stereo frames at 22,050 Hz, or 55.540499 seconds. The corrected capture contains 1,224,799 frames, or 55.546440 seconds; stop flushing retains 131 additional frames. The final ten seconds use the same 1,722 callback boundaries. Measurements use absolute changes between adjacent left-channel signed 16-bit samples; a change greater than 10,000 is a diagnostic threshold, not an audible-quality standard.

| Measurement, final ten seconds | 512-byte source reads | Consumed-sample capture |
| --- | ---: | ---: |
| Mean absolute change at callback boundaries | 2,901.14 | 1,112.99 |
| Boundary changes greater than 10,000 | 127 | 13 |
| Mean absolute change away from boundaries | 408.33 | 393.52 |
| Non-boundary changes greater than 10,000 | 562 | 452 |
| Peak absolute sample | 24,575 | 24,575 |
| Samples with absolute value at least 32,760 | 0 | 0 |

[Numeric comparison](validation/2026-10-08_digiquad_pcm_comparison.csv). Both captures have identical left/right channels in the measured interval. These properties do not establish the intended channel content or original waveform. Listening feedback for the corrected excerpt is **significantly improved, but still defective**, with residual crackling/distortion rather than tempo identified as the main concern. Full audio acceptance remains open.

## 4. Reproduction Tools

Build the ROM-free regressions and local PCM capture utility:

```powershell
make test
make build/capture_audio.exe
New-Item -ItemType Directory -Force build/diagnostics
build/capture_audio.exe "roms/games/DigiQUAD [G] (EN).bin" 1300000000 build/diagnostics/generated.wav
```

The utility writes 16-bit stereo WAV at 22,050 Hz using the same streaming resampler as the host. Its `.wav.csv` sidecar records the CPU step, output-byte offset, inferred input rate, IIS mode/prescaler, DMA configuration, current source, remaining transfer items and callback sample count. WAVs and sidecars are ignored by Git and must remain local. The utility does not model gaps as silent WAV frames and excludes host underruns, so WAV duration is the duration of emitted PCM rather than a complete guest-time recording.

An optional fourth argument selects CPU steps per peripheral tick for controlled clock-ratio experiments. For example, `2` with 2.6 billion steps approximately preserves the peripheral-time budget of the default 1.3-billion-step run. This can change boot and musical position, so excerpts are not automatically comparable. It has not established an accurate hardware clock or resolved all distortion. The launcher retains its existing one-tick-per-step clock model.

ROM-free tests cover overwriting source RAM inside a callback batch, exact sample count, stereo-channel lifetime, batch-boundary carry during bulk ticks, stop flushing, fixed source, non-IIS destinations, active-transfer re-enabling and pending interrupt delivery. CPU regressions also verify that diagnostic zero-tick steps execute instructions without advancing DMA, while default steps still advance it. All nine regression suites pass.

A bounded Windows GUI/audio run completes 1.3 billion steps and exits normally through `--exit-on-limit`. [Smoke measurements](validation/2026-10-08_digiquad_pcm_gui_smoke.csv) average 19.64 MIPS over the final 30.06 seconds, with zero dropped samples and 12 cumulative empty-queue observations at the last interval. The final row has 2,048 queued frames. The framebuffer remains unchanged during that final interval; this run does not establish ongoing animation or gameplay. These measurements show that finer sampling remains close to the configured instruction target, while host starvation remains unresolved. Scene and inputs are not controlled against the earlier manual session, so this is not an animation comparison or an audible acceptance test.

## 5. Remaining Investigation

[Pitfall's original-hardware reference and generated PCM baseline](21_pitfall_original_hardware_reference.md) provide an additional comparison target. Matching display motifs and numerical PCM checks do not yet establish equivalent sound.

Residual discontinuities and reported distortion remain after the source-lifetime fix. Further work should correlate captured samples with guest mixer writes and DMA reloads, verify IIS/codec interpretation against hardware, and distinguish guest production delays from host queue starvation. The approximate CPU/peripheral time base remains a separate constraint on game speed and synchronization. Increasing instruction throughput alone is not a verified timing correction.
