# Remaining execution overhead and delivery gate

Date: 2026-10-18. Follows [multiple transfers and profile builds](105_arm_multiple_transfers_and_pgo.md).

## Short-budget cache probe

A targeted fixture first translates one instruction, resets PC, then requests 512 steps of a two-instruction loop. The current cache can retain the short prefix instead of forming a complete native loop. A candidate tags instruction/byte-budget-capped entries and excludes them from a later larger lookup. Its focused fixture reaches 255 internal backedges, and all six suites pass in 11.15 seconds.

One matched 2.4-billion-step Wade pair nevertheless records 29.343750 versus 29.937500 seconds process CPU time and 29.520 versus 30.264 seconds wall time. CPU, complete SDRAM and snapshot hashes match. Compilations increase from 5,547 to 5,698 and peak emitted bytes from 5,622,080 to 7,077,797. No relevant performance advantage is established. The candidate and its test are removed from the active source; no further live check follows. This remains a diagnostic case for future region selection, not a delivered optimization.

## Direct installed-player comparison

An initial attempt is excluded because the developer executable starts the JIT by default while the installed executable starts the interpreter. The explicit corrected candidate uses `--engine interpreter --title-blit auto`; common ROM/EEPROM/step/ratio/language/snapshot arguments otherwise match the installed reference. The already completed installed-reference run is reused instead of adding another reference repetition.

At 2.4 billion steps, complete SDRAM, framebuffer and all dumped registers match. The installed image uses 22.890625 seconds process CPU time and 23.203 seconds wall time. The profiled candidate uses 27.218750 seconds process CPU time and 29.813 seconds wall time. Scheduling/frequency variation limits causal attribution, but there is no basis for delivering this candidate as an improvement over the installed player. It is not installed or repackaged as ready. No listening or gameplay sweep is appended.

The [manifest](validation/2026-10-18_delivery_gate.json) records exact identities, the excluded attempt and corrected comparison. The installed executable retains SHA-256 `858c64effd03fb43738d3bb12b1e98c98063b463d4f6d112293c907607942ab2`.

## Next structural boundary

The retained work expands native ARM semantics and reduces measured costs in earlier JIT-to-JIT pairs. It does not satisfy real-time reserve or stable live audio. More instruction-level shortcuts, cache-size changes or compiler-profile sweeps are not the next delivery strategy.

The next slice must execute several already validated native blocks within one device-event window, sharing run-state setup and timing accounting. It must preserve partial budgets, memory faults, MMIO exits, code-write invalidation, mapping changes and successor lifetime across cache eviction. This window linking has not been delivered or measured here; the exploratory source sketch was removed before compilation. Passing semantic fixtures or the earlier JIT-to-JIT percentages must not be substituted for the failed live and installed-player gates.

The technical source is published; usable improved test-EXE readiness remains open.
