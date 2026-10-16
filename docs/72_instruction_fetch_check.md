# Combined Instruction-Fetch Subpage and Alignment Check

## Candidate

The [active-scene guest instruction mix](67_active_scene_instruction_mix.md) shows that `97.89%` (`978,940,394` of `999,993,610`) of instruction fetches in active gameplay remain within the currently cached 1 KB SDRAM subpage (`instructionPage`). Previously, `Bus::readInstruction` in [`src/memory/bus.h`](../src/memory/bus.h) masked `va` to its 1 KB boundary (`va & ~1023u`), tested `page != instructionPage`, separately tested alignment (`(va & (thumb ? 1u : 3u)) == 0`), and branched on `page != instructionPage || !aligned`.

Because `instructionPage` always has its low 10 bits clear (`va & ~1023u`) when valid and is set to `UINT32_MAX` when invalidated (on mode changes, MMU state changes and TLB flushes), both the 1 KB subpage match and instruction alignment can be tested in a single bitwise operation:
`((va ^ instructionPage) & (thumb ? ~1022u : ~1020u)) != 0`.
When `instructionPage` is valid, the upper 22 bits of `va ^ instructionPage` are zero if and only if `va` lies in the same 1 KB subpage, and the low alignment bits (`bit 0` for Thumb, `bits 1:0` for ARM) of `va ^ instructionPage` equal the low bits of `va`. When `instructionPage == UINT32_MAX`, the low alignment bits of `va ^ instructionPage` are inverted, so an aligned `va` is guaranteed to have non-zero masked bits and take the slow path; inside the slow path, `aligned` is checked explicitly before updating `instructionPage`.

## Short checks

All 58 CPU checks (including instruction fetch across modified code, ARM/Thumb transitions, 1 KB subregion boundaries, TTB/MMU/TLB invalidation, user-mode permission rechecks, unaligned RAM fetches and reset) and all 87 timing checks pass (`0 failure(s)`).

## One active-scene pair

Reference (`037fdb721f11cf45364ad590469fde9f0d05369c3c477a70e9d00d45a73cc8b7`, from [docs/70](70_halfword_transfer_shortcut.md)) and candidate (`d664045aac6367bf456f320572668bc3ca61777f7e6984cade3bdbc6fe47fedd`) use matching GCC `-O3 -march=native` unity builds on `Superstar Chefs [G] (EN).bin` for `4,000,000,000` steps at ratio 3 with [`tests/chefs_active_scene.txt`](../tests/chefs_active_scene.txt), a blank `0xFF` EEPROM, isolated settings, GUI/audio enabled and host pacing disabled. One pair was run without repetitions.

Final CPU (`0f10c4d81e5522cb6d06f9c6ab177e4ce8ed5d43f88d5796d0ee7ff689aefa92`), SDRAM (`4dc004cc589c46e7c70ec1468070bfd9a9be57619765295ab02ec5d3a78e6d87`) and active framebuffer (`f1460da33fb68b4746fb3328f4a28e429a04f201d3fe49d3720294917626399d`) hashes match between reference and candidate. The [paired results](validation/2026-10-16_instruction_fetch_check.csv), [baseline intervals](validation/2026-10-16_instruction_fetch_check_baseline.csv) and [candidate intervals](validation/2026-10-16_instruction_fetch_check_candidate.csv) record the run.

| Observation | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Active-section throughput (`3B..4B`) | 51.9184 MIPS | 55.7115 MIPS | +7.31% |
| Active-section elapsed time | 19,261.0 ms | 17,949.6 ms | -6.81% |
| Section CPU/bus elapsed time | 19,103.6 ms | 17,785.8 ms | -6.90% |
| Section GUI time excluding wait | 128.978 ms | 130.942 ms | +1.52% |
| Whole-run interval throughput | 42.8069 MIPS | 55.8618 MIPS | +30.50% |
| Whole-run process CPU time | 66.5781 s | 44.0156 s | -33.89% |

Because `Bus::readInstruction` runs on every non-idle step (`999,993,610` times in the active window) and is inlined directly into `ARM920T::step`, collapsing the subpage and alignment test into one predicted-not-taken check improves active-section throughput from `51.92` to `55.71` MIPS (`+7.31%`, saving `1,317.8 ms` of CPU/bus time in the `3B..4B` section) and lowers whole-run process CPU time from `66.58 s` to `44.02 s` (`-33.89%`) in this matched pair.

## Decision

Retain the combined subpage and alignment check in `Bus::readInstruction`.
