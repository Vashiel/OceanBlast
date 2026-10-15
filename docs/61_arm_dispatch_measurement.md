# ARM Dispatch Measurement

The subsequent [active-scene check](62_active_scene_acceptance.md) exercises movement and A/B/C in the first level. It preserves guest state but does not establish a general gameplay speedup.

## Current cost profile

A five-second host-PC sample of the current normal GCC `-O3 -march=native` unity build begins after the 600-million-instruction snapshot of Superstar Chefs at ratio 3. The unpaced headless replay has no input and stops at 1.8 billion instructions. This covers an early startup/title interval; the exact ending guest step of the sample was not recorded. Executable SHA-256: `a73b3c1d0ed48de23df3acac9bcfed6bac67da33694435466f0da4dd937756da`.

Of 1,736 in-module samples, 67.7% resolve to CPU code, 23.5% to memory/peripheral code and 8.8% to execution batching. There are no failed samples or samples outside the module. `stepARM` accounts for 27.6%, `executeSimpleDataProcessing` 15.7%, `step` 12.2% and MMU translation 9.2%. The [raw samples](validation/2026-10-15_current_host_pc.csv) and [resolved summary](validation/2026-10-15_current_host_summary.txt) retain the observations. Sampling and source-line attribution do not establish exact instruction-cycle costs.

## Short candidate decisions

Each isolated candidate uses the same normal compiler configuration as its reference. One headless pair runs 1.2 billion Superstar Chefs instructions at ratio 3 without input or host pacing. No compilation or other replay runs concurrently with the measured pair. All four comparisons preserve final CPU, SDRAM, framebuffer and snapshot-state hashes.

| Candidate | Reference MIPS | Candidate MIPS | Decision |
| --- | ---: | ---: | --- |
| Move diagnostic ARM tracing into a cold helper | 80.810 | 81.881 | Not retained: the 1.3% wall-throughput difference is too small to establish a useful gain. |
| Replace the TLB valid flag with an invalid VPN sentinel | 81.710 | 78.459 | Rejected after the slower pair. |
| Direct execution of plain immediate ARM operations | 76.076 | 79.401 | Retained as a narrow execution-path simplification after reference and title checks. The measured pair is 4.4% faster. |
| Extend the direct path to unshifted register operands | 81.870 | 80.024 | Rejected after the slower pair; only the immediate path is retained. |

The [tracing](validation/2026-10-15_cold-trace.csv), [TLB](validation/2026-10-15_tlb-key.csv), [immediate](validation/2026-10-15_immediate-alu.csv) and [register-extension](validation/2026-10-15_plain-alu.csv) records include executable hashes and CPU times. Immediate-candidate process CPU time decreases from 15.3125 to 14.4375 seconds in its pair. Reference timings vary between experiments, so these single pairs do not establish a repeatable host-wide gain or predict old-device or PGO speed. No repeated pairs or gameplay sweeps were added.

The unretained [tracing patch](validation/2026-10-15_cold-trace.patch), [TLB patch](validation/2026-10-15_tlb-key.patch) and [register-extension patch](validation/2026-10-15_plain-alu.patch) apply with `git apply --unidiff-zero` to revision `286ac22` in separate checkouts. They are not part of the production interpreter.

## Retained execution path

After the normal interrupt and instruction-fetch checks, `ARM920T::step` directly executes unconditional immediate AND, EOR, SUB, RSB, ADD, ORR, MOV, BIC and MVN when the S bit is clear and neither source nor destination is PC. It then advances PC and uses the normal peripheral tick. There is no decoded-code cache, extra persistent state or host-specific instruction requirement.

Flag-setting instructions, carry-dependent arithmetic, PC operands, conditional instructions, register operands and debugging retain the existing ARM decoder. The single-instruction cycle count remains one for the retained subset, as in the existing simple ALU path. Fetch faults and pending interrupts are handled before the shortcut.

The candidate passes all 87 timing checks and the existing CPU reference comparisons. An added matrix checks all sixteen NZCV patterns, every immediate rotation, boundary operands and the nine supported operations against the general interpreter. The CPU suite now has 58 passing checks. Existing random and conditional-operation checks also cover fallback behavior.

One short visible GUI/audio run reaches the title menu at ratio 3 and normal pacing. It completes 1.2 billion instructions, reports no empty audio queues or dropped samples, and retains the prior title run's final SDRAM and framebuffer hashes. The [intervals](validation/2026-10-15_immediate_title.csv) are preserved. This is bounded title-screen evidence, not listening or later-gameplay acceptance.

## Portable local PGO build

The source at `e1149e4` was built as an `-O3` unity PGO executable with `-march=x86-64`, using Superstar Chefs and Rayman 3 training runs of 900 million instructions each at ratio 3. This selects conservative x86-64 code generation rather than the development machine's native CPU features. Training is build preparation, not a gameplay check.

The finished executable passes one short GUI/audio title run: 1.2 billion instructions, ratio 3, fresh settings and EEPROM, no input and normal pacing. The [intervals](validation/2026-10-15_immediate_pgo_title.csv) sustain the 60-MIPS target with zero empty queues and dropped samples. The title menu was inspected after execution stopped. Final SDRAM and framebuffer hashes match the previous PGO title check.

The tested executable, SHA-256 `aefd4e59cc6a14a74796703e914be25642c8a91729a0e730b9aa3ff445db6d59`, replaces the local `bin/oceanblast.exe`. Its predecessor, SHA-256 `5a83fcb8c93b1fac14c3941ab4d5e89a86243b2774f4cb3c8c37eaf09700429d`, is saved under ignored `build/launcher-backup-2026-10-15-before-immediate/oceanblast.exe`. Existing sessions and preferences were retained. No release archive was published.

The prior installed executable used native architecture options and different training limits. Their throughput has not been compared as a matched PGO pair, so the developer-build improvement must not be presented as a measured improvement over that installed PGO build. Old-device speed, broader cartridge coverage and audio listening remain separate acceptance work.
