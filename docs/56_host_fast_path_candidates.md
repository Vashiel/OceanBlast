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
