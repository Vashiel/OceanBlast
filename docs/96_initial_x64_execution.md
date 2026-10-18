# Initial Windows x86-64 Execution Engine

Status: bounded native engine slice delivered, 2026-10-17. J2 integration is in progress; production and performance acceptance remain open.

This report preserves the initial integer-only slice. The [2026-10-18 continuation](97_checked_memory_and_player_integration.md) adds checked memory, direct branches, generation-based invalidation, a larger indexed cache and the separate developer player, with current acceptance limits.

## Delivered behavior

`X64ExecutionEngine` implements the shared machine execution contract. It lowers the initial ARM/Thumb integer IR to executable x86-64 functions, including conditions, constant shifts, carry/borrow, comparisons and NZCV updates. Guest registers and flags are stored in the machine-owned state. Native functions are entered through the ordinary scheduler backend boundary, not through a separate emulated machine.

The scalar interpreter executes unsupported operations before native execution can skip them. This includes memory/MMIO instructions, branches and complex system operations in the current slice. The experimental register-clock mode remains scalar. Counters distinguish generated steps, fallback steps, compilation failures, compilations, cache hits, evictions and resets. Existing title-blit acceleration is excluded from native fixtures and is not a native-engine acceptance mechanism.

Native integer blocks contain no guest memory or MMIO operation. A batch is bounded by the supplied step budget and the next peripheral/external deadline. It advances the same legacy tick count and ratio phase as scalar execution. Eligible pending IRQs enter through the reference path before a generated guest instruction executes. A pending level-source refresh limits the batch to one step, preserving zero-tick reassertion before the following instruction. Branches and mode transitions can return through scalar fallback without replacing the timeline.

## Code lifetime and source validation

The retained cache is limited to 64 blocks and 512 KiB of generated code. A single generated function is capped at 64 KiB. Publication may temporarily retain one additional function; allocator pool/bookkeeping overhead is separate from these logical code-byte limits. Least-recently-used entries are released when limits are reached. Explicit reset releases cached functions without resetting guest state or time.

Cache context includes entry PC, instruction-set/privilege/control bits and CP15 control, TTB and DACR. Entries retain exact physical source addresses and instruction bytes. Before reuse, the engine checks the current mapping/permissions and all source words. Like the reference instruction-fetch cache, each block stays inside one 1 KB permission span. Valid hits bypass instruction decoding and compilation. Alias writes and changed code cannot execute a mismatched retained function; changed permissions cannot be bypassed by a hit. Code outside boot SRAM and SDRAM remains scalar.

This is a conservative source-validation mechanism. It does not complete J3's write-generation, physical-page dependency/invalidation and memory-lowering work. Direct RAM lowering and faster invalidation must retain these correctness checks until replacement mechanisms pass their own gates. No RAM-write fast path is changed in this slice.

AsmJit publishes code through forced dual mappings. The Windows backend checks that the callable view is committed executable/read-only memory before allowing execution. Code emission/publication precedes calls; cache reset and machine mutation are allowed only while no batch is running. Allocation/emission errors fall back before guest execution. Host ABI/register allocation is expressed through AsmJit's host function signature.

## Dependency decision and reproducible developer build

Use [AsmJit](https://github.com/asmjit/asmjit/tree/0464e1c8f1f43c861a0c73d4e59437fff255cdd9), pinned to `0464e1c8f1f43c861a0c73d4e59437fff255cdd9`. Its [official API](https://asmjit.com/doc/index.html) provides x86-64 and AArch64 emitters, host ABI handling and executable-memory support. The [Zlib license](../third_party/asmjit_LICENSE.md) is retained. Sharing this emitter dependency avoids maintaining two handwritten instruction encoders; OceanBlast retains ownership of guest semantics, IR, scheduling, cache identity and fallback policy.

The optional `jit/` CMake project requires CMake 3.24 or later and a C++20 compiler for this pinned dependency. Existing Makefile/frontend builds remain available without downloading AsmJit. The native target currently requires 64-bit Windows. The initial Android backend is still future work.

For a MinGW developer build, with the toolchain on PATH:

```powershell
cmake -S jit -B build/jit -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-march=x86-64
cmake --build build/jit -j 4
ctest --test-dir build/jit --output-on-failure
```

CMake fetches the pinned source under the ignored build directory. An explicit local FetchContent source override is supported for offline development; verify its commit before using its results. This slice was built with GCC 16.2.0, conservative x86-64 instructions, Release optimization and no PGO. It does not install or switch the launcher engine.

## Short validation and limits

The same 4,580 ROM-free arithmetic, shift, flags, condition and Thumb fixtures pass with scalar execution and generated x86-64 execution against test-only IR expectations. Native coverage counters require all these native fixtures to execute generated code with zero fallback steps and zero compilation failures. A multi-instruction dependency fixture checks condition skips, ADC/SBC and RRX within one generated block.

Additional short checks cover legacy phases 1/3/6/16, scalar branch fallback, native/interpreter continuation, external/timer deadlines, alias-written code, MMU permission changes under the same context, cache reset/recompile, IRQ entry, zero-tick USB level reassertion and register-clock fallback. The USB fixture uses a temporary generated empty image and removes it afterward. In the synthetic 97-step loop fixture, 96 steps are generated and one branch step is scalar. This number is not game translation coverage.

Recorded checks and executable hashes appear in the [validation manifest](validation/2026-10-17_jit_foundation.json). Test duration measures validation work, not emulation speed. No game benchmark, live-audio acceptance, CPU-reserve result or Android result is claimed. The installed launcher executable remains unchanged.

## Next delivery

Add checked memory-operation lowering with precise fault attribution and device ordering, complete physical-page/mapping invalidation fixtures, then expand control-flow coverage through the same backend. Broader integration must record actual generated/fallback work. Run the short matched Wade gate when coverage and machine integration can support a meaningful comparison; only then assess real pacing, audio and CPU reserve.
