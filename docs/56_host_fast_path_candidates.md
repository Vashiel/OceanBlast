# Host Fast-Path Candidate Validation

The [host CPU samples](54_host_cpu_samples.md) place most measured execution cost in ARM execution and memory translation. Several smaller host-side changes were evaluated separately before adoption. The [settings-persistence correction](55_frontend_settings_persistence.md) is independent of these execution candidates.

## Comparison method

All candidate binaries use the same normal GCC `-O3 -march=native` unity build and the same settings-corrected baseline. Each replay uses the same cartridge hash, input fixture, instruction limit, legacy ratio and blank 2,048-byte EEPROM. Headless pairs use `tools/compare_execution.py`; GUI/audio pairs use `tools/compare_gui.py`. Repeated pairs reverse the execution order on alternate repetitions. Compilation and other emulator comparisons are not run concurrently with the measured replay.

Headless comparisons require identical final CPU, SDRAM, framebuffer and snapshot-state hashes. GUI comparisons require identical final CPU, SDRAM and framebuffer hashes; host audio queue statistics and presentation counts may vary with scheduling. Fresh per-run frontend settings prevent the user's current preferences from affecting the comparison.

The GUI tool supports repeated pairs, process CPU time, disabled host pacing, separate baseline/candidate batch sizes and an optional fixed-scene host-cost interval. `--unpaced` measures available host throughput; its audio statistics do not establish sound quality under normal pacing. Normal development-build results do not predict the gain of a PGO release build or hardware-equivalent game speed.

## UART output buffering

The candidate replaces a flush after every UART0 character with a flush after each newline. Its guest-visible UART behavior remains unchanged in the measured replays. Three pairs per cartridge cover the first 1.2 billion steps, with Superstar Chefs at ratio 2 and Wade at ratio 6:

| Cartridge | Baseline median elapsed time | Candidate median elapsed time | Baseline range | Candidate range |
| --- | ---: | ---: | ---: | ---: |
| Superstar Chefs | 30.012 s | 27.858 s | 26.098–35.729 s | 26.882–28.366 s |
| Wade Hixton's Counter Punch | 27.169 s | 26.800 s | 26.180–27.201 s | 26.362–27.414 s |

All twelve runs have identical paired guest-state and framebuffer hashes. Individual pairs change sign, and host variation is larger than the apparent Wade median improvement. These measurements do not establish a repeatable throughput benefit. The candidate was discarded. The raw [Superstar Chefs results](validation/2026-10-13_uart_flush_chefs.csv) and [Wade results](validation/2026-10-13_uart_flush_wade.csv) preserve timings and executable hashes. These bounded startup runs do not establish an improvement in Wade's fight.

## LCD register caching

The main loop checks the GUI every 50,000 instructions in legacy mode, but framebuffer inspection and drawing already have a 16-ms host-time guard. LCD access therefore does not occur at the full GUI polling frequency. The [measured Wade fight MMIO records](validation/2026-10-10_wade_fight_mmio.csv) contain no guest LCD accesses. Combined GUI work accounts for roughly 0.5% of the separately measured fight interval.

Caching the LCD registers has not been benchmarked here. The existing evidence does not justify treating their hash-map lookups as the main performance bottleneck or changing their storage as a priority. Interrupt-subregister access needs separate treatment because it participates in device interrupt handling.

## Zero-time peripheral calls

The candidate guards `ARM920T::step`'s calls to `Bus::tick`: elapsed time always advances the bus, while a zero-time call is retained if a UART0 or USB host level interrupt needs reassertion. Omitting either device would change interrupt behavior. The candidate passes the ROM-free execution-timing and UART-interrupt suites.

Three Superstar Chefs ratio-2 headless pairs have identical CPU, SDRAM, snapshot and framebuffer hashes. Baseline elapsed times are 25.580, 27.559 and 28.471 s; candidate times are 27.625, 26.811 and 27.150 s. Paired differences change sign. Candidate process CPU time is higher in all three pairs. These results do not establish a throughput gain, and the guard was discarded. The [raw records](validation/2026-10-13_zero_tick_chefs.csv) retain the exact binary and cartridge hashes.

A GUI/audio Wade comparison covers 8.5 billion steps with the ratio-6 fight fixture. The reference reaches the expected final framebuffer hash, matching the earlier fight replay. In the 7.0–8.5-billion-step interval it spends 31.512 s in CPU/bus execution and 0.192 s in GUI work, within 31.741 s total. The reference's whole-run throughput is 41.835 MIPS against a 120-MIPS pacing target, with 1,474 empty audio-queue events. The candidate replay was stopped because of visibly poor pacing and stuttering title audio. It therefore has no final paired-state or performance result. The [reference-only record](validation/2026-10-13_zero_tick_wade_reference.csv) marks comparison status as `pending`; it must not be presented as a successful candidate comparison.

## GUI interval and batch size

At ratio 6, the 50,000-step legacy GUI interval corresponds to about 2,400 checks per modeled second. A 20,000-peripheral-tick interval would reduce that to 1,000 checks per modeled second. At ratio 1 it would instead increase checks from 400 to 1,000. Neither rate is the number of presented frames: drawing retains its independent host-time guard.

Changing the interval and increasing the execution batch from 4,096 to 65,536 have not received runtime validation. Both remain unchanged in the retained code. Any candidate must preserve exact input, snapshot, peripheral-time and diagnostic deadlines; a larger batch cannot reduce the cost of ARM instructions that are still executed individually.

The completed measurements support continued work on ARM execution and address translation. They do not establish a major GUI bottleneck, an INI-cache failure, or faster gameplay from these candidates. The [runtime build-profile notes](46_runtime_build_profiles_and_rayman_ratio.md) explain why normal developer and PGO gameplay builds require separate acceptance checks.

## TLB permission-tag hit path

The candidate stores valid VPN bits and per-subpage read/write permission bits in precomputed tags. The reference uses the same MMU corrections and tag population, but checks the tag key and cached permission separately; only the TLB-hit decision differs. Both native x64 unity builds use GCC `-O3 -march=native`, with no PGO. Six headless pairs replay Wade's ratio-6 fight fixture for 8.5 billion steps, split across two three-pair batches. Each batch reverses the executable order on alternate pairs:

| Measure | Reference median | Candidate median | Paired result |
| --- | ---: | ---: | --- |
| Elapsed time | 119.148 s | 118.368 s | Candidate faster in 4 of 6 pairs |
| Process CPU time | 115.859 s | 113.141 s | Candidate lower in 5 of 6 pairs |
| CPU-time-based throughput | 73.366 MIPS | 75.128 MIPS | Candidate higher in 5 of 6 pairs |
| Wall-time-based throughput | 71.359 MIPS | 71.835 MIPS | Candidate higher in 4 of 6 pairs |

CPU-time-based throughput is the 8.5-billion-step limit divided by the process CPU time; the comparison script's MIPS column uses wall time.

All twelve runs produced identical final CPU, SDRAM, framebuffer and snapshot-state hashes. Median process CPU time is 2.35% lower for the candidate, but one of six pairs reverses that result; an exact one-sided sign test gives p=0.109, so the direction is not significant at the 5% level. Wall time also varies substantially between batches, and the two batches do not control CPU affinity or operating frequency. These results suggest a possible small host-side gain but do not establish one, and they do not establish faster gameplay. The tag path remains experimental and is not enabled in production. A controlled paced GUI check would still be required before claiming a gameplay improvement. The [raw Wade fight results](validation/2026-10-14_tlb_tags_wade_fight.csv) include per-run timings and executable, ROM, and guest-state hashes.
PGO comparisons also need matching architecture options. An installed `-march=native` executable and a portable `-march=x86-64` executable can have different host instruction sequences even when all guest-state hashes match. Their timing difference cannot be attributed to a frontend correction alone. Profile generation and profile use must agree on the architecture options; reusing a profile across these targets can cause GCC to reject mismatched control-flow counters.

## ARM instruction class table

This candidate replaces the outer ARM instruction classification with a 4,096-byte `constexpr` class table indexed by bits 27:20 and 7:4. It uses class numbers and the existing handlers rather than a table of function pointers. Full-instruction checks for BX, MRS, MSR, SWP, CP15 and FPA11 remain in their existing paths. The existing condition check, Thumb decoder, instruction fetch, IRQ boundaries and peripheral timing are retained. This is a classification experiment, not a replacement ARM core or a change to the instruction set's exception semantics.

The [candidate patch](validation/2026-10-14_arm_class_table.patch) is based on production revision `bf52cb5fc033493d2894b8f4871793a0127109b8`; it does not include the experimental TLB changes above. Both executables use GCC 16.2.0, the same native x64 `-O3 -g` unity build and no PGO. The benchmark parent and child processes use Windows affinity mask 4. Operating frequency and background load are not fixed.

The [ROM-free classification check](validation/2026-10-14_arm_class_equivalence.cpp) compares all 4,096 keys with 256 variations of omitted bits per key: 1,048,576 comparisons pass. It can be compiled independently with a C++17 compiler. The candidate also passes all 55 CPU regression, 76 execution-timing and 12 UART-interrupt checks. These checks establish compatibility with the tested baseline paths; they do not independently prove complete ARM920T correctness.

One Wade ratio-6 pair replays 8.5 billion steps with `tests/wade_ratio6_fight.txt` and a zero-filled 2,048-byte EEPROM:

| Measure | Reference | Class-table candidate |
| --- | ---: | ---: |
| Elapsed time | 199.2922 s | 201.8997 s |
| Process CPU time | 196.7812 s | 199.2969 s |
| Wall-time throughput | 42.651 MIPS | 42.100 MIPS |

Final CPU, SDRAM, snapshot and framebuffer hashes match. The decoded final framebuffer shows the Rocco fight with a round counter of 73; its SHA-256 is `b6099539a7e6af759e1a067dcfc06b17e8db0323b5fcb432c44a9712a365ccf6`. Candidate CPU time is 1.28% higher in this pair. One pair cannot establish a repeatable regression, but it supplies no evidence of a gain. The candidate is discarded and remains outside production. The [raw Wade records](validation/2026-10-14_arm_class_wade.csv) retain the binary and cartridge hashes.

Two earlier Superstar Chefs ratio-2 pairs each run for 3 billion steps with `tests/chefs_auto_confirm.txt`; its first input is at step 1.4 billion, so a 1.2-billion-step run would miss that input sequence. All paired guest hashes match. Reference/candidate CPU times are 68.9062/86.6875 s and 50.3906/84.5938 s. Both candidates take more CPU time, with substantial variation in the reference. The third pair was interrupted and is excluded from the [completed records](validation/2026-10-14_arm_class_chefs.csv).

Subsequent performance candidates use Wade as the primary scene. Superstar Chefs is a regression check after a candidate demonstrates a reproducible Wade improvement. These measurements concern this particular class table in front of existing handlers; they do not rule out specialized instruction handlers or a translated-block execution engine.

## Immediate/register ALU specialization

The [second candidate](validation/2026-10-14_alu_immediate.patch) starts from the same production baseline, without the class table. `executeSimpleDataProcessing<bool Immediate>` has two direct template instances: the existing major-0 path calls `<false>`, and major 1 calls `<true>`. Compile-time branching selects either immediate rotation or register shifting. Opcode and flag handling, PC operand semantics, fallback guards, fetch and exception boundaries remain in their existing paths. No function-pointer dispatch or decoded-instruction cache is introduced.

Both executables use the same build and affinity settings as the class-table pair. The candidate passes all 55 CPU regression, 76 execution-timing and 12 UART-interrupt checks. One Wade ratio-6 pair covers the same 8.5-billion-step fight replay and blank EEPROM:

| Measure | Reference | ALU candidate |
| --- | ---: | ---: |
| Elapsed time | 191.4311 s | 199.0050 s |
| Process CPU time | 189.1250 s | 196.5625 s |
| Wall-time throughput | 44.402 MIPS | 42.712 MIPS |

All four paired guest hashes match, including the same final Rocco fight framebuffer recorded above. CPU time is 3.93% higher for the candidate. This single pair establishes no performance benefit and cannot attribute the difference specifically to branches, code layout or operating-frequency variation. The candidate is discarded; no Superstar Chefs follow-up or GUI performance claim is made. The [raw Wade records](validation/2026-10-14_alu_immediate_wade.csv) preserve the measured pair.

These two experiments do not establish that the interpreter is generally optimal or incorrect. Further changes need a closer comparison of the generated hot code and measured execution costs. Function-level sample shares alone do not distinguish decoding work, register spills, branch misses and cache effects; their relative contribution remains unmeasured here.

### Generated code observations

Function ranges from the x64 PE exception table (`.pdata`) and disassembly provide [static code observations](validation/2026-10-14_alu_assembly.csv) for the same reference and ALU executables:

| Function property | Reference | ALU candidate |
| --- | ---: | ---: |
| `step` code range | 14,878 bytes | 14,878 bytes |
| `step` prologue stack reservation | 0x148 bytes | 0x148 bytes |
| `stepARM` code range | 12,519 bytes | 13,549 bytes |
| `stepARM` prologue stack reservation | 0x88 bytes | 0x98 bytes |
| `stepARM` register pushes | 8 | 8 |
| `stepARM` start address modulo 64 | 16 | 16 |

Both `stepARM` versions contain two direct ALU call sites. The reference calls GCC's 1,643-byte `executeSimpleDataProcessing` partial clone; the candidate calls the 1,640-byte register and 1,275-byte immediate template instances. The specialization therefore does not eliminate these calls. Although the individual immediate handler is smaller, the caller is larger and reserves more stack space. These observations do not prove additional dynamically executed spills, cache misses or the cause of the measured timing difference. The next useful comparison is to map exact host-PC samples onto the decoder, ALU call boundaries and their generated instruction paths.
