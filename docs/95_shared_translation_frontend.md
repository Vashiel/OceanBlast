# J1: Shared ARM/Thumb Integer Frontend

Status: initial frontend slice delivered, 2026-10-17. Native lowering, wider instruction coverage and complete J1 acceptance remain separate gates.

`translation_ir.h` defines host-independent 32-bit integer operations, constant/register operands, constant shifts, carry sources, conditional execution, flag-write policy and optional result registers. Comparisons/tests write flags without assigning a register. The representation retains guest PC, instruction width and original encoding for precise attribution and fallback.

`translation_frontend.h` decodes bounded straight-line ARM and Thumb blocks. Initial ARM coverage includes data-processing instructions with rotated immediates or constant register shifts, excluding PC operands and register-controlled shifts. Thumb coverage includes immediate arithmetic/comparisons, constant shifts, register/immediate add/subtract and selected register ALU operations. Memory access, complex control/state operations and unsupported encodings stop before execution.

Default limits are 32 instructions and 128 guest bytes. Decoding also stops at a 1 KB permission subregion, an unavailable fetch, misaligned entry or address wrap. Branches/BX remain explicit pre-execution fallback boundaries; this slice does not lower branch targets or build traces. The fetch provider must avoid MMIO and other side effects. Persistent physical-page dependencies and code-cache identity belong to J3.

## Short validation

The ROM-free frontend test checks block/fetch limits and unsupported-operation boundaries. A test-only IR evaluator compares 4,580 arithmetic, shift, carry, overflow, ARM-condition and Thumb cases with the scalar CPU. No evaluator is linked into the product and no second decoded interpreter is introduced.

Passing these cases establishes agreement with the tested reference semantics, not full independent ARM920T hardware conformance. This frontend does not execute native code and is not a performance result.

## Remaining frontend work

Expand lowering and checks alongside the native backend: control flow, additional shifts, checked memory operations and explicit complex-operation fallbacks. Retain the shared representation for x86-64 and AArch64. Do not turn the test evaluator into another runtime engine.
