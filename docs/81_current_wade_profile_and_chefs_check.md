# Current Wade Profile and Chefs Sanity Check

## Scope

The [controlled before/after comparison](80_controlled_total_comparison.md) established a combined Wade benefit for the retained changes. This task identifies remaining costs on that current build and performs one short Chefs counter-check. It introduces no runtime change and makes no additional throughput-gain claim. The total allowance was 15 minutes, including analysis and documentation; no compilation or launcher replacement was required.

## Exact-build provenance

The executable is the current normal GCC 16.2.0 `-O3 -march=native -g` unity build from the controlled comparison, SHA-256 `47f90a01197f8a2aded8a4109fe276022af51a094acbd0e25212095696928249`. Runtime source is `2880b0d`; repository HEAD at task start was `8914c98`. A frozen local copy, its full disassembly and raw PE `.pdata` exception section are preserved together under ignored `build/2026-10-17-wade-current-profile/`.

The [metadata](validation/2026-10-17_wade_current_host_metadata.json) records executable, disassembly, `.pdata`, ROM, input and initial EEPROM hashes. [Sample-to-function ranges](validation/2026-10-17_wade_sample_function_ranges.csv) match the highest-count offsets to that executable's PE exception ranges and DWARF source information. Addresses were not resolved against a rebuilt or older executable.

## One Wade sample

The unchanged emulator runs the established Wade ratio-6 replay with GUI/audio, disabled pacing, isolated settings and a zero-filled 2,048-byte EEPROM. Execution ends at 8.5 billion instructions. After the flushed host-cost marker at seven billion, the existing host sampler takes one six-second instruction-pointer sample at a requested two-millisecond interval. The emulator is pinned to logical processor 2; no compilation or other emulator replay runs concurrently.

Windows reports battery power at launch. The user connects AC during warmup, before sampling begins. AC is reported at sample start, sample end and replay completion; the observed Windows scheme remains Balanced. No restart is performed. This is a hotspot sample, not a throughput comparison, and its timings are not compared with the previous AC-only benchmark.

Sampling completes in 6.1254 seconds and ends before the 8.5-billion-step limit. Exact guest steps at the sample endpoints are not recorded, so the sample covers part of the 7-to-8.5-billion fight section rather than its whole duration. It captures 2,133 in-module observations, 18 outside the executable and zero failed observations. Shares below use the in-module denominator.

| Resolved source group | Sample share |
| --- | ---: |
| CPU | 67.5% |
| Memory/peripherals | 27.0% |
| Execution batching | 5.1% |
| Other groups | about 0.4% |

| Resolved function | Sample share |
| --- | ---: |
| `ARM920T::step` | 19.5% |
| `ARM920T::stepARM` | 18.3% |
| MMU translation | 13.9% |
| Single-data transfers | 6.0% |
| General simple ALU | 5.5% |
| Legacy batch loop | 5.1% |
| Register no-flags shortcut | 4.4% |
| Instruction fetch | 3.7% |
| General data processing | 3.6% |
| Last-fault inspection | 3.0% |
| Immediate no-flags shortcut | 2.8% |
| Halfword transfers | 2.8% |
| Peripheral ticking | 1.7% |

The [raw PC offsets](validation/2026-10-17_wade_current_host_pc.csv) and [resolved summary](validation/2026-10-17_wade_current_host_summary.txt) retain the observations. Thread suspension changes execution conditions; inline source attribution and nearby-instruction scheduling also affect the distribution. The shares are not exact cycle costs, removable-overhead estimates, opcode frequencies or predicted gains. In particular, `step` includes necessary execution work and must not be counted entirely as overhead. This Wade sample is not a like-for-like before/after comparison with the earlier Chefs profiles.

Final CPU, SDRAM and framebuffer hashes match all four runs of the controlled Wade reference. No new visible-motion or listening acceptance is claimed for this instrumented replay.

## Matched disassembly and next hypothesis

The cached-translation hit condition at `arm920t_mmu.h:45` receives 191 observations (9.0%). A [matched instruction excerpt](validation/2026-10-17_wade_tlb_hit_excerpt.txt) shows two separate validity/VPN tests in inlined copies inside the word-transfer handler, plus address calculation with a 12-byte TLB-entry stride. Source-line samples alone do not establish that those two branches consume the entire 9.0% share or identify cache-miss/branch-misprediction rates.

The next bounded candidate is a compact TLB-entry representation: keep the same 2,048 translations and physical-page bases while packing virtual-page identity, all four subregion AP fields, validity and section/page fault information into one metadata word. An eight-byte entry could reduce the array from 24 KiB to 16 KiB and allow a combined validity/key test. This is a layout hypothesis, not an implementation or measured gain.

It differs from the rejected valid-flag-to-sentinel experiment: that experiment did not compact the permission/fault metadata or reduce the entry stride. Keep all existing permission, descriptor, fault and invalidation behavior. Check the highest virtual page, all subregion permission combinations, cached denial, privilege changes, single/full invalidation and fault type before one Wade comparison. Do not bundle architectural MMU corrections or a new data cache into this candidate. Reject it if the bounded comparison supplies no useful gain.

## Short Chefs counter-check

After Wade profiling completes, the same frozen executable runs the active Chefs fixture through four billion instructions at ratio 3, normal 60-MIPS pacing, GUI/audio, isolated settings and the established `0xFF` EEPROM fixture. No profiler is attached. Scripted movement and A/B/C actions reach the first-level state with 760 points and four lives. That stopped screen is visually inspected after execution ends, then its window is closed.

The [intervals](validation/2026-10-17_chefs_sanity.csv) average 60.0000 MIPS and report zero empty audio queues and zero dropped samples. Final CPU, SDRAM and framebuffer hashes match the earlier active-scene reference; [metadata](validation/2026-10-17_chefs_sanity_metadata.json) preserves the checks. This is a bounded compatibility/pacing pass, not a new Chefs speedup comparison, listening test, save test or broad gameplay acceptance.

The current baseline is retained. Runtime code and installed launcher remain unchanged.
