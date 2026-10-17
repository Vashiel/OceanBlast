# Wade Music Startup: One DMA Buffer, Then Ping-Pong

Follow-up to [generated PCM and DMA delivery](89_wade_music_startup_delivery.md). That report left open why the ring is not renewed around music second 1.83, and why the first two seconds of generated music do not match the soundtrack even though delivery gaps stay under 1 ms. This note answers both from the existing ratio-6 capture. No ROM is replayed again, and no runtime or installed executable changes.

## What the callback register trace shows

`DCSRC2` in [the reduced callback table](validation/2026-10-17_wade_startup_callbacks.csv) is the current DMA source. `DCON2` is `0xA0900200` for 340 of 343 callbacks in the first two music seconds: transfer count `0x200` halfwords, 1,024 bytes, autoreload enabled (bit 22 clear). The two physical halves of the tracked ring are `0x30350000` and `0x30350400`.

| Music window | Callbacks whose current source is in `0x30350000` | Callbacks in `0x30350400` |
| --- | ---: | ---: |
| 0–1.85 s | almost all | first one at 1.854 s |
| 2–5 s | 258 | 257 |
| 5–8 s | 258 | 257 |

For the first 1.85 seconds DMA reloads the same 1,024-byte buffer. After that the two halves alternate, one 1,024-byte period each. The identical replay at callback 2972 (music second 1.834, same bytes as callback 2970) sits on the last reload of the first half, immediately before the second half is used. The four stale callbacks in [the ring summary](validation/2026-10-17_wade_startup_ring_summary.json) are that handover, about 16 ms of storage, not a two-second loop of old audio.

The other bytes in those two seconds were written by the guest between DMA reads. The startup waveform is therefore new mixer output from one buffer, not a Windows queue and not a repeated DMA block.

## Where the CPU is

The capture records `PC` at each audio callback. In the first two music seconds the frequent addresses are userspace SDL (`0x40111xxx`, with a few samples in `FB_LCD444Update` at `0x40129330`). From second 2 onward the same trace also lands often in the kernel around `0xc00b65xx`, while `DCON2` stays `0xA0900200` and the two buffers alternate.

DMA completion still requests IRQ 19 on every period, and the interpreter takes a pending IRQ on the next instruction when the I bit is clear. The callback `PC` is only a sample, so a short handler can be missed. The change in where those samples land still lines up with the buffer change: the steady ping-pong path is not what is running through the mismatched opening.

## Why this is not a preset or reload change

Doubling the legacy CPU budget shortens the mismatched opening because the guest finishes that startup work in less audio time. It does not remove the opening. At ratio 12 the soundtrack correlation is already high from 1 to 2 seconds and still fails from 0 to 1 second; the first half-second is worse than at ratio 6 ([budget comparison](validation/2026-10-17_wade_startup_budget.json)). Ratio 6 remains the fight-cadence preset: about 0.84 counter units per modeled second, against a programmed 1.024-second unit and a GBA comparison near one unit per second ([docs/52](52_wade_ratio6_and_host_power.md)). Raising it would speed the fight and still leave a wrong opening.

Reloading `DISRC2` before the completion IRQ is the behavior that removed the earlier `loadbuffer` timeouts ([docs/05](05_black_screen_investigation.md)). The guest already keeps the next address in `DISRC2` during a transfer once ping-pong is running. Delaying that reload until the handler runs would change the steady path that matches the soundtrack after second 2. Dropping the 16 ms duplicate would not replace the fresh but musically different samples that fill the rest of the opening.

## Decision

No cartridge preset, DMA reload order, or Windows playback buffer is changed. The installed executable stays `57fac1d7b69e91c61337ac3ca08927bcce6cb67eaab420d3b1e935e2de828410`.

The opening defect is guest mixer output produced while DMA is still looping one 1,024-byte period. The later audible bursts remain a separate live-output problem ([docs/85](85_wade_pcm_and_audio_recovery.md)). The next useful measurement is which guest writer fills `0x30350000` during those 1.85 seconds, and whether its song clock advances once per DMA period or once per frame. That requires a symbol-resolved trace of stores into the buffer, not another ratio trial.
