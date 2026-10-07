# Independent CPU regression review

Date: 2026-10-07. Scope: ROM-free instruction execution tests against an isolated copy of the active source tree. No active emulator source files were modified.

## Reproduction

From the repository root, create a separate output directory and compile:

```text
g++ -std=c++17 -O2 -Isrc tests/cpu_regression.cpp src/cpu/arm920t.cpp src/memory/bus.cpp -o build/cpu_regression.exe
build/cpu_regression.exe
```

The reviewed snapshot was copied to an isolated review workspace. The executable was built with the existing w64devkit compiler. This is a working-tree snapshot, not a claim about a particular published commit. A current source tree can differ; rerun before integrating.

## Results

One control test passed; six tests failed. A failing suite exits with code 1 intentionally.

| Case | Expected | Observed issue and integration guidance |
| --- | --- | --- |
| ARM ADDS overflow-to-zero | Zero result, C=1, Z=1 | Passed; verifies the public test setup and ARM flag path. |
| Thumb `ADDS r0,r0,r1`, 0xffffffff + 1 | Zero result, C=1 | Failed. The decoder writes r[rd] before reading r[rs] for flag calculation. Save original operands before writing the destination; also cover aliased SUB and signed overflow. |
| Thumb `LSLS r0,r0,#1`, r0=0x80000000 | Zero result, C=1 | Failed. The shifter's carry result is discarded. Apply the carry update while preserving the special LSL #0 behavior. |
| Thumb `BX r0`, r0=0x200 | PC=0x200, ARM state | Failed. The current Thumb decoder handles only top-level groups 0 and 1; remaining groups silently do nothing. Implement ARMv4T BX semantics and state transition. |
| Thumb `LDR r1,[r0]` | Load the supplied memory word | Failed. The instruction is silently skipped. Add load/store groups and propagate MMU faults, including correct Thumb data-abort return addresses. |
| Thumb `SWI 0` | SVC vector 0x8, LR=0x102, ARM state | Failed. SWI is silently skipped. Preserve original CPSR in SPSR_svc and use the 2-byte instruction return address. |
| User `MSR CPSR_c,r1` with r1=0x13 | Remain in User mode | Failed. executeMSR permits modification of the privileged control field in User mode. Restrict writable fields by current privilege; flags remain separately writable. |

## Priority and limits

These are CPU correctness defects and plausible future userspace/game blockers. They do **not** establish the cause of the current boot stall: first correlate the failing instruction families with the actual execution trace. Do not describe this suite as complete CPU/MMU validation.

The test file uses public CPU/bus APIs and synthetic instruction words only. It needs no cartridge, firmware, extracted filesystem, or commercial game assets. It intentionally does not alter the shared Makefile.

Architecture references: [Arm instruction summary](https://developer.arm.com/documentation/dui0204/h/arm-and-thumb-instructions/instruction-summary), [Arm original Thumb encoding groups](https://support.arm.com/documentation/ddi0406/c/Application-Level-Architecture/Thumb-Instruction-Set-Encoding/16-bit-Thumb-instruction-encoding/Shift--immediate---add--subtract--move--and-compare). Use the ARM920T/ARMv4T reference for exact implementation details; newer Thumb-2-only instructions are outside the target ISA.
