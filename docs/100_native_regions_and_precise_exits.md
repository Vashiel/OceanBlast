# Native regions and precise exits

Date: 2026-10-18. Continues [cached dispatch](99_cached_dispatch_and_mapping_context.md) under J4. Technical checks pass; the live Wade gate fails. No usable test-EXE or production acceptance is claimed.

## Delivered

* The frontend follows unconditional direct branches within one 1 KB permission span. The player enables this option. Branches still retire and update LR normally. Promotion and invalidation retain noncontiguous physical source addresses.
* General registers and CPSR remain in native temporaries between integer operations. Predicated first-use operands and skipped destinations retain their values. Registers/flags are materialized before helpers, control-flow exits and loop re-entry.
* Native coverage includes ARM literal loads, shifted register-offset byte/word transfers, ARM MUL/MLA and Thumb MUL. Unsupported PC destinations, system/control operations and multiword transfers retain reference exits. Register-clock execution remains entirely reference-based.
* RAM loads inspect the live MMU TLB: enable state, tag, actual privilege, the selected permission field and physical RAM bounds are checked. Misses, denied permissions, mirrors and device addresses use checked helpers. Stores retain code-write/frame-capture bookkeeping. The fixed TLB, flags and RAM allocations remain owned by the bus/MMU and outlive generated code.
* Warm validated entries execute directly in the runner. Promotion, compilation and changed dependencies remain in the slow path. A checked unsupported instruction executes exactly one reference step, preserving exception/context/deadline reasons without constructing a second scalar run loop.
* A backward edge may target a suffix after one-time setup. Only the suffix repeats. Decoding remains bounded to 32 instructions/128 bytes/one permission span; execution may retire up to 65,536 instructions within the caller budget and first device event. MMIO, faults and active-code-page stores still exit immediately.
* Phase accumulation uses one bounded 32-bit quotient/remainder calculation, with constant division for the shared ratio-6 setting. This replaces two 64-bit divisions found in exact-binary disassembly. Guest steps, phase and peripheral time remain unchanged.

These are initial bounded regions. Conditional edges leaving the region still return through the runner; a general control-flow graph backend is not complete.

## Technical checks

Six CTest suites pass, including 4,680 native semantic comparisons. New fixtures cover 384 shifted register-offset combinations, byte/rotated-word literal loads, cached flags/carry/predication, noncontiguous code invalidation, page-end backward links, suffix loops with one-time setup, partial RAM-loop iterations and precise scalar exit reasons. Privileged TLB priming followed by user reads checks all four small-page permission fields. Timing checks cover every phase at ratios 1, 3, 6 and 16. Shared headers pass ordinary-build C++17 syntax checks.

The [manifest](validation/2026-10-18_native_regions.json) separates source hashes, intermediate executables, final checks and runtime failure. Final player SHA-256:

`e1516d444b573b1168d881e50a5fcac3b4dd737494cea08a1284a85457a1e207`

Build: GCC 16.2, conservative x86-64 Release, no PGO; pinned AsmJit `0464e1c8f1f43c861a0c73d4e59437fff255cdd9`. The player uses promotion threshold 3 and the existing 8 MiB emitted-code limit. Metadata consumes additional memory.

## Wade result

The final headless pair compares the previous `6f398de` JIT executable with the candidate. ROM, EEPROM seed, ratio 6, language, disabled title shortcut and affinity mask 4 are identical; AC is observed before/after. GUI, audio, pacing and sampling are disabled.

At 600 million steps, CPU, complete SDRAM and diagnostic snapshot match exactly. This reaches the Engine Software logo, not full title/gameplay acceptance.

| Counter | Previous | Final |
| --- | ---: | ---: |
| Generated steps | 518,835,700 | 544,508,139 |
| Reference steps | 81,164,300 | 55,491,861 |
| Compilations | 78,508 | 71,789 |
| Native cache hits | 154,207,169 | 131,248,515 |
| Peak code bytes | 6,918,995 | 8,388,608 |
| Inline RAM loads | not implemented | 132,275,389 |

About 90.8% of steps execute generated code. Coverage is not speed acceptance. The final single-order pair records 13.375 s versus 12.500 s process CPU time, about 6.5% less. Intermediate pairs vary substantially, including unfavorable results. They remain separate; percentages are not added, and sustained performance is unaccepted.

The final GUI/audio watchdog stops the 120-MIPS-paced run after 13.14 seconds for sustained low speed and recurring queue starvation. The last four stable-rate intervals report approximately 34.7%, 45.3%, 37.5% and 44.8% modeled speed; empty-queue events rise from 36 to 54. This is deliberate test termination, not a diagnosed product crash. No gameplay sweep, Chefs test or listening request follows.

The installed EXE and older isolated package retain their preceding hashes. The final binary is archived in the ignored build directory; it is not offered as ready for game testing.

## Discarded implementation and profiling

An ABI-safe generated dispatcher called existing blocks and a C++ successor helper. A second version emitted successor guards/accounting directly. Both matched guest state but failed to establish a speed advantage; neither remains in source. Their hashes/results are retained separately in the manifest.

A 1.5-second profile of the exact pre-phase-arithmetic candidate starts after the 400-million-step marker: 697 module samples, 113 outside-module samples, zero failures. [Samples](validation/2026-10-18_native_region_host_samples.csv) and [function attribution](validation/2026-10-18_native_region_host_functions.txt) use exact PE `.pdata` ranges matched to exact-start COFF symbols. Source lines are unavailable.

`run` contains 359 module samples, including inlined preparation/timing work; these are not all dispatch instructions. Exact instruction inspection identifies run-context initialization and two phase divisions as concrete costs. The final arithmetic change follows that inspection. Outside-module samples remain unattributed. Sampling suspends the CPU thread and is not a throughput measurement. This profile predates the final arithmetic change and is not attributed to the final binary.

## Next delivery

Keep conditional successors inside a generated region, with dynamic retirement counts and explicit exits. Preserve source/context dependencies, register retention, checked memory and scalar semantics. Merely wrapping tiny block calls in another dispatcher does not meet this objective.

The next bounded slice is a two-successor region: verify both paths, changing predicates, backward edges, code writes and precise event/fault exits, then one short Wade gate. More cache-size sweeps and isolated decoder percentages are lower priority. Test-EXE readiness requires the runtime gate to pass; production also requires listening, gameplay and measured CPU reserve.
