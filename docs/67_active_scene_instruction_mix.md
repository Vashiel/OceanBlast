# Active-Scene Guest Instruction Mix

## Why this measurement

The [active-scene host profile](65_active_scene_host_profile.md) showed where host execution time accumulates, but function-level sample share alone did not predict whether individual opcode subsets were frequent enough to pay for another special case. After the [flag-setting immediate extension](66_immediate_flags_experiment.md), the [LDM span check](64_ldm_ram_span_experiment.md) and the earlier [register-operand shortcut](61_arm_dispatch_measurement.md) were rejected, this single instrumented pass records the exact emulated instruction mix, shortcut hit rates, basic blocks and short instruction sequences inside the active Superstar Chefs section (`3,000,000,000` to `4,000,000,000` steps).

## Conditions and boundaries

A standalone offline probe ran the unchanged interpreter core through `4,000,000,000` steps of `Superstar Chefs [G] (EN).bin` using the [active input fixture](../tests/chefs_active_scene.txt), ratio 3 and a blank 2,048-byte `0xFF` EEPROM (`d0ff1b294b5288d1ae1421eadf5b2d38a8752b76d472ff30bed9028e25b1c5b8`). Warmup from `0` to `3,000,000,000` steps executed without instruction classification; exact counting covered only the `1,000,000,000` active steps from `3,000,000,000` to `4,000,000,000`. Note that the standalone probe omitted the launcher's automatic cartridge language byte write (`bus.setEepromByte(0, langByte)`), whereas `compare_gui.py` runs `--gui` with isolated default settings (`cartLanguage = auto`). Because instrumented execution alters host overhead, this run is not a throughput benchmark. No permanent counters were added to production sources.

Within the `1,000,000,000` active steps, `955,303,811` steps (`95.53%`) execute in user mode (`TTB = 0x30f14000`) and `44,696,189` (`4.47%`) in kernel mode. Zero steps enter `WFI` idle or Thumb state, and `6,390` steps (`0.0006%`) enter the IRQ handler. Of `999,993,610` instruction fetches, `935,914,094` (`93.59%`) are strictly sequential (`PC + 4`) and `978,940,394` (`97.89%`) stay within the same 1 KB subpage. By condition field, `890,009,904` instructions (`89.00%`) are unconditional (`AL`), `70,525,307` (`7.05%`) are conditional and pass, and `39,458,399` (`3.95%`) fail their condition check.

## Instruction categories (`3B` to `4B` window)

| Category | Steps | Share of `1B` | Notes |
| --- | ---: | ---: | --- |
| `REG_ALU_NOFLAGS` | 291,512,931 | 29.15% | Unconditional/passing register ALU with `S=0`, non-PC (`149.44M` unshifted, `141.61M` immediate-shifted, `0.46M` register-shifted) |
| `IMM_ALU_NOFLAGS_FASTPATH` | 229,251,758 | 22.93% | Handled by retained `executeImmediateNoFlags` (`168.48M` unrotated, `60.77M` rotated) |
| `LDR_WORD_IMM` | 75,585,629 | 7.56% | `69.32M` are unconditional pre-indexed word loads without writeback or PC destination |
| `LDRH_STRH_LDRSB_LDRSH` | 73,833,858 | 7.38% | Halfword and signed transfers |
| `B_BL_COND_TAKEN` | 46,742,461 | 4.67% | Taken conditional branches |
| `ARM_COND_SKIPPED` | 39,458,399 | 3.95% | Condition failed before decode |
| `IMM_ALU_FLAGS_REJECTED66` | 31,963,124 | 3.20% | Flag-setting immediate ALU subset tested in [docs/66](66_immediate_flags_experiment.md) (`29.59M` `SUBS`) |
| `LDM` | 30,722,342 | 3.07% | `84.51M` total registers loaded (`2.75` registers per `LDM` on average) |
| `IMM_CMP_CMN_TST_TEQ` | 30,365,059 | 3.04% | Immediate test/compare operations |
| `MUL_MLA` | 25,904,117 | 2.59% | 32-bit multiply/accumulate |
| `STR_WORD_IMM` | 24,269,555 | 2.43% | `18.23M` unconditional pre-indexed word stores without writeback |
| `STM` | 20,946,409 | 2.09% | `65.29M` total registers stored (`3.12` registers per `STM` on average) |
| `REG_CMP_CMN_TST_TEQ` | 20,854,829 | 2.09% | Register test/compare (`18.56M` unshifted, `2.30M` immediate-shifted) |
| `REG_ALU_FLAGS` | 15,754,082 | 1.58% | Flag-setting register ALU (`15.26M` unshifted) |
| `STRB_IMM` | 12,414,558 | 1.24% | Immediate-offset byte stores |
| `UMULL_SMULL_MLAL` | 10,094,997 | 1.01% | 64-bit multiply/accumulate |
| `B_UNCOND` | 9,147,605 | 0.91% | Unconditional `B` |
| `IMM_ALU_OTHER` | 5,517,187 | 0.55% | Conditional, carry-using or PC-touching immediate ALU |
| Remaining categories | 5,659,100 | 0.57% | `BL` (`0.24%`), coproc/FPA/SWI (`0.14%`), `LDRB_IMM` (`0.09%`), `LDR_STR_REG` (`0.05%`), `MRS/MSR` (`0.04%`), `SWP`, `IRQ` |

## Why previous shortcuts lost

1. **Retained immediate no-flags shortcut (`22.93%` hit rate):** Hits on nearly a quarter of all active instructions (`117.00M` `AND`, `57.73M` `ADD`, `21.51M` `SUB`, `16.85M` `MOV`, `9.80M` `RSB`, `5.17M` `MVN`). Over `73%` require no rotation.
2. **Rejected flag-setting immediate shortcut ([docs/66](66_immediate_flags_experiment.md), `3.20%` hit rate):** Only `31.96M` instructions qualify, and `92.6%` of those are loop-counter `SUBS`. Widening the hot immediate check and adding NZCV calculation overhead did not pay off for a `3.2%` slice.
3. **Rejected LDM span translation ([docs/64](64_ldm_ram_span_experiment.md), `3.07%` hit rate):** Average register count per `LDM` is only `2.75` (`ldmia r1, {r1, r5}` in the hottest block loads just two registers). Setting up a span helper cost more than two cached subpage lookups.
4. **Rejected broad register-ALU shortcut ([docs/61](61_arm_dispatch_measurement.md)):** While `REG_ALU_NOFLAGS` accounts for `29.15%` of all steps, splitting it reveals `149,443,482` (`14.94%`) unshifted instructions (`bits[11:4] == 0`), `141,613,248` (`14.16%`) immediate-shifted instructions (`bit[4] == 0, shift_imm != 0`), and only `456,201` (`0.05%`) register-shifted instructions. Pulling general shifter-carry and shift-type dispatch into `stepARM` hurt code generation around the hot loop.

## Basic blocks and recurring short sequences

Execution is heavily concentrated in a few user-space loops (`64,119,695` basic-block executions across `2,785` unique blocks, mean length `15.60` instructions):

| Rank | Start PC | Mode | Block len | Executions | Dynamic steps | Share of `1B` | Cumulative |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | `0x0005dbf4` | User | 63 | 4,857,987 | 306,031,363 | 30.60% | 30.60% |
| 2 | `0x0000b9e4` | User | 36 (3x12) | 8,018,150 | 288,942,715 | 28.89% | 59.50% |
| 3 | `0x00011ac0` | User | 14 | 11,462,716 | 71,017,824 | 7.10% | 66.60% |
| 4 | `0x4004b754` | User | 25 | 3,979,360 | 47,918,634 | 4.79% | 71.39% |
| 5 | `0x0005ddf4` | User | 9 | 4,741,978 | 42,677,214 | 4.27% | 75.66% |
| 6 | `0x0005dd88` | User | 8 | 4,873,274 | 41,480,176 | 4.15% | 79.81% |
| 7 | `0x0005dd48` | User | 16 | 4,873,274 | 24,635,228 | 2.46% | 82.27% |

Within basic blocks, the top four 2-grams and top eight 3-grams consist exclusively of `REG_ALU_NOFLAGS` and `IMM_ALU_NOFLAGS_FASTPATH`:
* `REG_ALU_NOFLAGS -> REG_ALU_NOFLAGS`: `126.92M` (`12.69%`)
* `IMM_ALU_NOFLAGS_FASTPATH -> IMM_ALU_NOFLAGS_FASTPATH`: `85.47M` (`8.55%`)
* `REG_ALU_NOFLAGS -> IMM_ALU_NOFLAGS_FASTPATH`: `78.13M` (`7.81%`)
* `IMM_ALU_NOFLAGS_FASTPATH -> REG_ALU_NOFLAGS`: `76.12M` (`7.61%`)

Together, these two categories represent `52.08%` of all active instructions and dominate the 63-instruction graphics/blitting block at `0x0005dbf4`.

## Selected next candidate

Test a narrow, flagless, register-shift-free ARM register-ALU shortcut (`cond == AL`, `S == 0`, bit 25 clear, bit 4 clear, non-PC `rd/rn/rm`, nine non-carry opcodes) that avoids shifter-carry computation completely and separates the zero-shift case (`bits[11:4] == 0`, `14.94%` of active steps) from immediate shifts (`14.16%`). Because `S == 0`, no shifter carry or NZCV update is needed. Validate with the CPU/timing test suites and one unpaced active-scene comparison pair.
