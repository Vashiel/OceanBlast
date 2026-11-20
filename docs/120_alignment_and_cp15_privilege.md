# Alignment checking, CP15 privilege and the cache type register

## Scope

[Report 119](119_arm920t_cp15_and_transfer_semantics.md) listed four ARM920T differences in the interpreter: unprivileged transfers (`LDRT`/`STRT`), the CP15 c1 alignment bit, user-mode CP15 access and the cache type register. Unprivileged transfers now use user permissions under the DACR domain policy. This report covers the remaining three items. Each was first measured on all available cartridges and then changed.

## Measurement

A diagnostic interpreter build counted, per instruction site, every misaligned word, halfword, `LDM`/`STM` and `SWP` access, together with the CP15 c1 `A` bit and the processor mode. It also counted CP15 instructions executed in user mode. All 24 images ran with the interpreter engine and their cartridge presets: games for 1,500,000,000 steps with `tests/start_buttons.txt`, video and combined cartridges for 3,000,000,000 steps with `tests/video_language_confirm.txt`. Each run started with a zeroed 2-KB EEPROM.

| Cartridge | Instruction site | Access | `A` bit | Mode | Count |
| --- | --- | --- | ---: | --- | ---: |
| Spider-Man: Mysterio's Menace | `0x0003e03c` | `LDM`, base not word-aligned | 1 | user | 590,240 |
| Spider-Man: Mysterio's Menace | `0x0003e108` | `STM`, base not word-aligned | 1 | user | 590,240 |
| Winx Club + 5 Atari Games (NL FR DE TR) `[b]` | `0x000580b0` | `LDRH`, odd address | 1 | user | 130 |
| Pitfall: The Lost Expedition | `0x000caff4` | `LDR`, misaligned | 1 | user | 1 |

No other cartridge produced a misaligned access. No misaligned access occurred in a privileged mode. No cartridge executed a CP15 instruction in user mode. The Winx `[b]` accesses belong to the launcher crash loop caused by the erased NAND page described in [ROM integrity](14_rom_integrity.md); the repaired `[f]` image produced none.

## Behavior before the change

- `LDM` and `STM` passed an unaligned address to the bus. The bus then assembled each word from four bytes starting at the unaligned address, and stores wrote four bytes at shifted positions. The ARM920T ignores address bits `[1:0]` for these instructions. The Spider-Man sites therefore read and wrote different data than the hardware on every execution.
- The `A` bit had no effect. Misaligned `LDR` returned the rotated word, as with `A` clear.
- CP15 `MCR`/`MRC` executed in user mode. The ARM920T raises the Undefined Instruction exception.
- `MRC p15, 0, Rd, c0, c0, 1` returned the main ID `0x41009200` instead of the cache type register.

## Changes

- `LDM`/`STM` access the word-aligned address in the interpreter and in native multiple transfers. Base writeback still uses the unmodified base.
- With the `A` bit set, a misaligned word or halfword transfer, `LDM`/`STM` or word `SWP` takes a data abort with FSR `0x1` and the access address in FAR, before any memory or register update. Native code checks this in the checked load, store and multiple-transfer helpers. Inline native loads branch to the checked path for misaligned addresses only in blocks translated with the `A` bit set; CP15 c1 is part of the native block key.
- CP15 instructions in user mode take the Undefined Instruction exception. The native frontend translates CP15 c7 cache maintenance only for privileged blocks.
- `MRC p15, 0, Rd, c0, c0, 1` returns `0x0d172172`, the ARM920T cache type value from TRM table 2-6.

Thumb transfers are not alignment-checked. None of the 24 images executes Thumb code; the code marks this as a known shortcut.

## Linux interaction

The cartridge kernels set the `A` bit on every kernel entry. The kernel command line contains no alignment setting, so user-mode alignment faults use the kernel's default handling: the handler clears the `A` bit for the current task and returns, and the faulting instruction is retried without alignment checking. The next kernel entry sets the bit again.

A Spider-Man run with the installed JIT build and `--fault-log` over 1,500,000,000 steps recorded 149 data aborts at the `LDM` site `0x0003e03c`, against 590,240 misaligned executions of that instruction in the measurement. This matches one fault per interval between kernel entries. The run produced no `SIGBUS` and no segmentation fault. The retried transfers now use the word-aligned address, as on the hardware.

With the cache type register implemented, the kernel reports the ARM920T cache geometry during boot:

```
CPU0: D VIVT write-back cache
CPU0: I cache: 16384 bytes, associativity 64, 32 byte lines, 8 sets
CPU0: D cache: 16384 bytes, associativity 64, 32 byte lines, 8 sets
```

The earlier build printed no cache lines because the returned value equalled the main ID, which the kernel treats as an absent cache type register.

## Validation

- ROM-free tests cover the cache type and main ID reads, the user-mode Undefined Instruction trap, the privileged-only native translation of CP15 c7, word-aligned `LDM`/`STM` with the `A` bit clear, and native-versus-scalar alignment aborts for `LDR`, `STR`, `LDRH`, `STRH`, `LDM` and `STM` with the `A` bit set and clear.
- All 24 images were run with the native JIT before and after the change, using the same inputs and step counts as above. Every run reached its step limit. Guest segmentation faults and kernel error lines are unchanged for every image; the Winx `[b]` image keeps its 130 launcher faults. The CPU and RAM end states differ for all images because the kernel's boot path now reads the cache geometry.

## Open points

- The main ID register returns `0x41009200`. The ARM920T TRM gives `0x41129200`; with architecture field `0`, the kernel prints `ARMvundefined/unknown`. The kernel's processor table masks this field, so the correct processor functions are selected. The value is unchanged.
- Domain checks apply to unprivileged transfers only. Report 119 found no differing decision for ordinary translations.
- Misaligned halfword accesses with the `A` bit clear keep the existing byte-assembled result; ARMv4 leaves this case unpredictable.
