# MMU Table-Walk Outlining Experiment

## Hypothesis and scope

The earlier host sample attributes 9.2% of in-module samples to MMU translation. The cached translation routine includes a large table-walk and synthetic-mapping fallback. Keeping that fallback out of line might reduce code in the frequent cache-hit path. This experiment changes only the compiler's inlining choice, not translation results, permissions, cache keys, invalidation or device access.

An isolated source copy at `6c91e86` adds `[[gnu::noinline]]` to the existing inline definition of `Arm920TMmu::translateSlow`. The [patch](validation/2026-10-15_mmu_outline.patch) applies to that revision with `git apply --ignore-space-change` (also accepting CRLF worktrees). It is archived for reproducibility and is not applied to production source. Both executables use normal GCC `-O3 -march=native` unity builds, not PGO. The existing current developer executable is the reference; its hash and the candidate's hash appear in the results.

The symbol ranges reported by `nm -S` for GCC's `translate.part.0` change from `0x4b0` (1,200 bytes) to `0xb0` (176 bytes). The candidate also has a separate `translateSlow` symbol with range `0x470` (1,136 bytes). These are static symbol observations, not instruction-cache measurements or a speedup.

## Short acceptance and paired measurement

The candidate passes 10 standalone MMU checks, 58 CPU checks and 87 timing checks. It was then compared once using the [active Superstar Chefs fixture](62_active_scene_acceptance.md): blank EEPROM, fresh settings, ratio 3, GUI/audio, four billion instructions and host-cost measurement from three to four billion. `--unpaced --inspect-end --timeout 120` disables host pacing and leaves the completed level available for inspection. No compilation or other emulator replay ran concurrently.

Both stopped windows show the expected first-level state: 760 points and four lives. Final CPU, SDRAM and framebuffer hashes match each other and the preceding active-scene acceptance. There was no observed end-screen regression. This is bounded scene evidence, not continuous visual inspection or listening acceptance.

| Observation | Reference | Outlined candidate |
| --- | ---: | ---: |
| Active-section throughput | 80.6042 MIPS | 80.3968 MIPS |
| Active-section elapsed time | 12,406.3 ms | 12,438.3 ms |
| Section CPU/bus elapsed time | 12,323.3 ms | 12,336.0 ms |
| Section GUI time excluding wait | 62.355 ms | 82.002 ms |
| Whole-run interval throughput | 81.2569 MIPS | 81.4010 MIPS |

The [paired CSV](validation/2026-10-15_mmu_outline.csv), [reference intervals](validation/2026-10-15_mmu_outline_baseline.csv) and [candidate intervals](validation/2026-10-15_mmu_outline_candidate.csv) retain the measurements. Active-section throughput differs by -0.257%; the whole-run difference is slightly positive. This is inconclusive variation, not an established regression or improvement. Whole-process CPU and elapsed times include manual end inspection and cannot establish build performance. The section timers end before that inspection.

Unpaced runs discard 710,910 and 715,256 audio samples respectively, with zero empty queues. Audio generation above playback speed fills the output queue; these counters are not normal-paced sound acceptance. No audio threshold was applied to this throughput comparison.

## Decision

Reject the candidate: smaller hot code did not produce a useful gain in the one predefined pair. No repeated pair, PGO rebuild or additional scene sweep was added. Production source and the installed launcher remain unchanged. This does not rule out other memory optimizations; it rules out retaining this compiler-layout change on the available evidence. The next memory experiment should address work performed on ordinary data accesses rather than assume that smaller translation code is faster.
