# Native coverage and sampled compilation

Date: 2026-10-18. Continues J4 after the rejected region-size candidates in [102](102_dispatch_and_region_candidates.md).

## Problem and implementation

High native instruction coverage hid frequent transitions through the runner and the reference interpreter. A diagnostic-only counter identified 29,503,414 cached scalar exits for eligible ARM register-controlled shifts in the first 600 million Wade steps. Eligible halfword/signed transfers accounted for another 6,290,607 exits. The diagnostic build is separate from all timing builds.

The shared IR now describes the register supplying a shift amount. ARM integer lowering implements LSL, LSR, ASR and ROR using the low byte of that register, including zero, 32, larger shifts, rotations by nonzero multiples of 32, carry preservation, predication and overlapping operands. PC operands remain reference exits. The shift rules follow the [Arm instruction-set shift table, section 4.4.2](https://documentation-service.arm.com/static/5ed11a2dca06a95ce53f8f99); this is instruction-semantic evidence, not a timing calibration.

ARM LDRH, STRH, LDRSB and LDRSH use the checked memory path and live TLB checks. The decoder excludes unsupported PC/writeback and reserved combinations. Signed results are extended before committing the destination. Faults preserve partial execution and suppress destination/base writeback. An odd halfword store crossing into the active code page exits before another translated instruction. Existing odd-address behavior is preserved against the reference; this does not independently certify physical ARM920T alignment behavior. Instruction encodings are documented in the [Arm Architecture Reference Manual](https://documentation-service.arm.com/static/5f8dacc8f86e16515cdb865a).

The player also uses bounded entry sampling before translation. An unseen execution context first runs a normal interpreter batch. Repeated entries become eligible for decoding and promotion. The existing native cache, mapping/code checks and exact interpreter remain authoritative; samples are hints and never substitute guest work. The fixed sample table includes PC, instruction-set/mode, bus privilege and CP15 context. `runToBoundary` retains its explicit boundary behavior. New counters distinguish sampled reference steps and batches from native execution.

## Focused checks

All six CTest suites pass. Added checks include 7,392 register-shift/ALU comparisons with independent MOVS boundary/carry expectations; 192 halfword/signed-access combinations; destination/base fault preservation; halfword self-modification across code pages; sampled cold-to-native transitions; and timer/MMIO stop ticks at every tested legacy phase. The inherited 4,680 native semantic comparisons, all four small-page permission-field checks and existing memory/invalidation/CPU/timing suites remain in place.

## Wade comparisons

All headless pairs use the same cartridge, copied EEPROM seed, ratio 6, title shortcut disabled, language unchanged, affinity mask 4 and observed AC before/after. GUI, sound, pacing, diagnostic counters and PGO are disabled. GCC 16.2 Release uses `-O3 -DNDEBUG -march=x86-64`. Exact executable and state identities are in the accompanying validation manifest.

The register-shift-only 600-million-step probe matched state but did not establish a speed advantage: CPU time was 9.140625 versus 10.031250 seconds, while wall time was 10.729 versus 10.434 seconds. No speed claim is made for that intermediate binary.

The complete integer/halfword extension then ran one 2.4-billion-step pair. The final image was inspected and shows Wade's title screen with Press Start. CPU, complete SDRAM and diagnostic snapshot hashes match exactly in every retained pair.

| Change in its own pair | Reference CPU s | Candidate CPU s | Reference wall s | Candidate wall s |
| --- | ---: | ---: | ---: | ---: |
| Integer/halfword coverage versus preceding JIT | 45.531250 | 33.578125 | 45.704 | 33.893 |
| Sampled compilation versus integer/halfword build | 30.593750 | 26.765625 | 30.825 | 27.957 |

The first pair observes about 26.3% less process CPU time. The second observes about 12.5% less. These are separate single-order comparisons with host variability; their percentages must not be added or presented as a combined measured result. They do not compare against the installed PGO interpreter or establish weak-device performance.

With sampled compilation, compilations fall from 76,859 to 5,100 and code-cache evictions from 165,790 to zero in the recorded pair. Generated work falls from 2,270,390,359 to 572,974,054 steps, with 1,818,174,658 steps delegated through sampled reference batches. This is a measured hybrid execution policy, not a claim that more of the game runs natively.

## Live acceptance

The integer/halfword live probe is stopped after 12.97 seconds for persistent low speed and recurring empty queues. The sampled-entry probe is likewise stopped after 17.43 seconds. Its last four intervals show approximately 80.9%, 71.5%, 78.3% and 59.4% modeled speed, with empty-queue events rising from 5 to 26. Both close normally after the watchdog requests their own window to close. No gameplay sweep or Chefs counter-test follows these failures.

The implementation gains are retained, while usable test-EXE readiness and J5 acceptance remain open. The installed EXE has not been replaced. A compiled development artifact is not a passed live-audio or listening check.

## Rejected direct-store extension and build consolidation

A direct SDRAM-store extension checked live TLB write permissions, both touched code pages and active framebuffer capture before bypassing the checked helper. All six suites passed, and it preserved the same final Wade state. Its pair nevertheless regressed from 29.265625 to 34.250000 seconds of process CPU time and from 29.768 to 34.677 seconds wall time. The extra guards and emitted-code footprint did not pay for themselves in this scene. The entire direct-store extension was removed, with no live run or variant sweep afterward. The checked store helper remains in use.

The final build consolidates CPU and bus source compilation. The emitter remains separate because GCC 16.2 encounters an internal compiler error when that source is also included in the same unity unit. This is a build arrangement, not an attributed implementation speedup. Final executable identity, post-build state checks and the final live gate are recorded separately in the manifest.

Final executable SHA-256: `6aa728da0c7d27e8b37e3f7f6b0e85dd48631fac84c1ecd9ea89b09873e55cdf`. Its six suites pass in 7.79 seconds; its separate 2.4-billion-step state check matches all three reference hashes. The final live gate is stopped after 12.58 seconds: the last four modeled-speed intervals are approximately 80.6%, 69.1%, 82.1% and 59.6%, while empty queues rise from 5 to 28. No usable test-EXE readiness is claimed. The [validation manifest](validation/2026-10-18_jit_coverage_tiering.json) retains exact binaries, inputs, comparisons, rejections and live results.

The next implementation priority is to complete common ARM block transfers and ordinary return/control-flow coverage, preserving precise faults and event exits. More standalone cache-size changes and extra per-store guards are not justified by these results. A production switch still requires full-rate live output and audio acceptance.
