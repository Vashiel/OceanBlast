# LDM RAM-Span Experiment

## One bounded candidate

ARM LDM instructions load several registers from consecutive word addresses. The existing implementation translates each address independently. This isolated experiment tries one translation when an LDM loads at least two words entirely within one aligned, readable 1-KB SDRAM subregion. It borrows a pointer only for that instruction and copies each word in the existing register loop. No pointer survives instruction completion.

The 1-KB limit preserves small-page permission boundaries. Unaligned addresses, subregion crossings, device memory and translation failures use the original per-word path. A failed span lookup does not raise an early data abort: individual loads still run in order and retain their existing partial-completion behavior. Register order, addressing modes, bank selection, PC/CPSR handling, cycle accounting and writeback retain the existing interpreter code. STM, Thumb multiple transfers and other memory instructions are unchanged.

The [candidate patch](validation/2026-10-15_block_read.patch), including its focused tests, applies to revision `44ef03e` with `git apply --unidiff-zero --ignore-space-change`. It was built in an isolated source copy and was not adopted in production.

## Targeted checks

The candidate passes 10 MMU, 61 CPU and 87 timing checks. Three added CPU/bus checks cover:

- LDM register order and base writeback in all four addressing modes, using addresses inside and across 1-KB boundaries.
- A user-mode LDM whose first word is readable and next subregion is denied: the first register changes, the next remains unchanged, writeback does not occur, and the data-abort address is the denied word.
- RAM-span acceptance for aligned SDRAM and rejection of unaligned addresses, boundary crossings, device addresses and invalid lengths.

These are focused regressions, not a complete ARM memory-model validation.

## One active-scene comparison

Reference and candidate use normal GCC `-O3 -march=native` unity builds. Both replay the [same active Superstar Chefs fixture](62_active_scene_acceptance.md), blank EEPROM and fresh settings at ratio 3. GUI and sound remain enabled, host pacing is disabled, execution ends at four billion instructions, and the measured section runs from three to four billion. No compilation or other emulator replay ran concurrently. There was one pair and no repeated attempt to obtain a different outcome.

Both stopped windows show 760 points and four lives. Final CPU, SDRAM and framebuffer hashes match each other and the prior active-scene result. The [paired record](validation/2026-10-15_block_read.csv), [reference intervals](validation/2026-10-15_block_read_baseline.csv) and [candidate intervals](validation/2026-10-15_block_read_candidate.csv) retain executable/input hashes, section timers and audio counters.

| Observation | Reference | Candidate |
| --- | ---: | ---: |
| Active-section throughput | 81.2348 MIPS | 80.3523 MIPS |
| Active-section elapsed time | 12,310.0 ms | 12,445.2 ms |
| Section CPU/bus elapsed time | 12,227.8 ms | 12,344.0 ms |
| Whole-run interval throughput | 82.0297 MIPS | 80.8120 MIPS |

The candidate's section throughput is 1.09% lower in this pair. This supplies no useful gain; it does not establish a repeatable slowdown or its cause. The previous title-screen host sample does not establish how often eligible LDM instructions execute in this active scene. Neither fewer translations nor a successful correctness check guarantees a net speed benefit.

Whole-process timings include manual end-screen inspection and are excluded from the performance decision. Unpaced runs discard 731,648 and 700,422 audio samples, respectively, with zero empty queues. These are throughput runs, not normal-paced audio or listening acceptance; lower discard counts do not establish better sound.

## Decision

Reject the optimization and keep the current production interpreter and installed executable. Archive the patch and measurements rather than leave extra fast-path code without a demonstrated benefit. No PGO rebuild or additional gameplay sweep was performed. Before another memory candidate, profile the active section itself to identify the cost and frequency of actual data accesses; the older startup profile is insufficient to choose further LDM work confidently.
