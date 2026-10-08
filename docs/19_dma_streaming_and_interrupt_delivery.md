# DMA Streaming and Interrupt Delivery

Follow-up: [consumed-sample capture](20_pcm_capture_and_dma_sample_lifetime.md) supersedes the 512-byte source-read granularity described below. The smaller delayed-read window was still sufficient to corrupt DigiQUAD audio.

## 1. Audio Data Lifetime

The preceding DMA implementation read the entire source buffer from SDRAM only at completion. A producer could overwrite an already consumed region before that read, causing the emulator to output replacement data instead of the samples present when the DMA passed the region.

DMA 2 now forwards consumed PCM in approximately 512-byte portions, aligned to complete stereo frames, with the remaining portion delivered at completion. Earlier portions are not reread. Fixed-source transfers repeat their transfer item; DMA destinations other than the IIS FIFO do not produce audio. IRQ completion and reload behavior remain separate from audio delivery.

A ROM-free regression advances half a transfer, overwrites RAM, and completes it. The emitted first half retains its original samples while the second half contains the replacements. Additional checks cover exact transfer length, fixed sources and non-IIS destinations. This establishes the corrected data lifetime; it does not prove the absence of all audible distortion.

The current output model remains 16-bit stereo PCM. Portions are approximations rather than individual hardware FIFO transfers, and the host backend still uses its existing buffer sizes. Eight-bit IIS audio, codec control and a fully modeled FIFO remain open work.

## 2. Pending Interrupt Delivery

Writing INTMSK previously reconsidered only UART delivery. A DMA completion occurring while masked could remain in SRCPND without becoming selected when software unmasked it. The controller now considers all pending unmasked sources when the selected interrupt slot is empty.

An unrelated INTPND write must not replace the selected source with a different pending source. Selection remains latched until acknowledgement; the next pending source is then chosen. Tests reproduce masked DMA completion followed by unmasking, unrelated acknowledgement, and subsequent source selection. The existing simplified lowest-bit priority remains; this change does not implement the complete programmable priority arbiter or FIQ routing.

## 3. Hardware Clock Register Correction

Samsung's S3C2410A manual, section 21-6, places the 256fs/384fs selector in IISMOD bit 2 and the serial-bit-clock selector in bits 1:0. Conflicting definitions in the early Linux header had led the preceding follow-up to use bit 1. Hardware behavior now follows the manufacturer register table, and the tests distinguish these fields. Modes with bit 2 clear, including the commonly observed `0x99`, keep their preceding nominal rate.

Reference: [Samsung S3C2410A User's Manual](https://www.keil.com/dd/docs/datashts/samsung/s3c2410_um.pdf), sections 8 and 21. The nominal-rate mapping, 45 MHz PCLK approximation and 20-million-step time base are otherwise unchanged. Correcting these register and delivery defects does not establish accurate original game speed.

## 4. Validation and Remaining Work

The warning-enabled optimized Windows build and all nine regression suites pass, including the new DMA streaming/interrupt suite. Manual reports for Wade, Superstar Chefs, Spider-Man and Rayman are preserved in [manual cartridge validation](18_manual_cartridge_validation.md); those preceding observations must not be represented as acceptance of this change.

A GUI/audio smoke run of Superstar Chefs completes 1.2 billion steps and exits normally. [Its measurements](validation/2026-10-08_chefs_streaming_smoke.csv) show zero dropped samples but 87 empty-queue observations at the final recorded interval. Starvation therefore remains present. This bounded startup run uses copied device settings, default pacing and no verbose debug logging; it does not establish sustained gameplay, audible quality or a controlled before/after improvement.

Remaining checks include repeat listening tests for crackling, sustained gameplay/input response, DMA production gaps and host queue starvation. Slow animation still requires validation of CPU execution against guest peripheral time. The code contains an unimplemented ARM920T wait-for-interrupt operation and an approximate instruction clock; neither has been silently replaced by a faster arbitrary clock in this change.

A Rayman GUI/audio smoke run also completes 600 million steps and exits normally. [Measurements](validation/2026-10-08_rayman_streaming_smoke.csv) cover startup only; they do not reproduce or resolve the previously reported later stalls. Both bounded windows close through `--exit-on-limit`, rather than a host crash.
