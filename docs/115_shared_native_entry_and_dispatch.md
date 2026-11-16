# Shared native entry ABI and intrusive dispatch streamlining

Date: 2026-11-15. Integrates the [shared native entry ABI](108_shared_native_entry_abi.md) and addresses the host dispatch and code-cache lookup costs identified in [114](114_chefs_battery_memory_review.md).

## Delivered behavior

1. **Shared Windows x64 entry bridge and single-pass block emission:** `X64ExecutionEngine::Impl` builds one shared `enterBridge` trampoline that saves nonvolatile registers and reserves a 16-byte-aligned 40-byte stack frame once per execution window (`R12 = ARM920TState*`, `R13 = NativeRun*`). Emitted blocks use `asmjit::x86::Assembler` directly with a 4-slot physical register cache (`esi`, `edi`, `r10d`, `r11d`), `ebp` for cached guest `CPSR`, `r14d` for the block retirement base, and `r15d`/`ebx` for clocked cycle accounting. Blocks exit or link to successors via indirect jumps through `NativeRun::exit` and `NativeRun::link` without per-block function prologues, epilogues or `Compiler` register-allocation passes.
2. **Intrusive PC hash table and splice-free LRU tracking:** `std::unordered_multimap<uint32_t, Cache::iterator> cacheByPc` is replaced by a fixed 4,096-bucket intrusive table (`std::array<CacheEntry*, 4096> cacheByPc` linked via `CacheEntry::nextByPc`), eliminating prime-modulo hashing and heap node allocations during lookup and insertion. Cache hits update `entry.used = ++useCounter` without relinking `std::list` nodes (`cache.splice`) on every hit. `cacheLifetime` advances on insertion only when another entry for the same guest PC already exists in the bucket.
3. **Two-way successor slots and upper-bound lookup limits:** Each `CacheEntry` retains two guarded successor slots (`std::array<SuccessorSlot, 2>`) so alternating taken and fallthrough cross-page branches remain linked within one shared window. `HotEntry::limit` and `SuccessorSlot::limit` record the maximum instruction budget (`maxLimit`, up to 32) for which the selected translation remains the longest match at that PC, allowing partial event-window budgets (`count <= slot.limit` with `entry.context[5] <= count`) to reuse cached entries without invalidating the 32-step slot.

## Rejected sub-candidate and bug fix during integration

- **Conditional memory `CPSR` clobber fix:** An initial single-pass emission draft used `ebp` as the effective-address scratch register in `emitMemory` while `compile` also held `cachedFlags` in `ebp` across predicated instructions. When a conditional memory operation (`STRNE`/`LDREQ`) executed after `readFlags()` loaded `CPSR` into `ebp`, `emitMemory` overwrote `ebp` with the guest memory address while `flagsValid` stayed set; a subsequent flag-setting ALU operation in the same block then wrote the corrupted low bits into `state.cpsr`. `emitMemory` now uses `esi` (flushed before memory operations) for the effective address.
- **Over-eager sampled compilation:** Breaking cold `sampledCompilation` fallback batches on every branch and bypassing second-stage `deferred` promotion when `sampledCompilation` was active increased Superstar Chefs 600M-step compilations from 5,813 to 81,005 and triggered 73,742 cache evictions, raising process CPU time to 18.58 seconds. The two-stage sampling and promotion thresholds were restored before acceptance.

## Correctness

All 13 CTest suites pass in 20.66 seconds. Added coverage in `tests/x64_execution.cpp` verifies two-way cross-page successor retention in a shared window and confirms that conditional memory accesses (`STRNE`/`LDREQ` with writeback) followed by a flag-setting ALU instruction (`SUBS`) preserve `CPSR` mode and mask bits across all 16 guest NZCV combinations.

## Controlled headless comparisons

Both comparisons use GCC 16.2 Release `-O3 -DNDEBUG -march=x86-64` builds with PGO disabled in the same build tree, affinity mask 4, disabled title shortcut and observed AC power throughout. Reference SHA-256 is `59e754c0321bbc535565cd46fd378e6d4e9eccb984df820c38858c5f556949a3`; candidate SHA-256 is `b1af3fafe515402467ff6d59eaef1b308ca9b59099277d8316cff3b413bf1684`.

### Superstar Chefs (600,000,000 steps, ratio 3, scripted input)

| Metric | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Process CPU seconds | 6.781250 | 6.250000 | -7.83% |
| Wall seconds | 6.976748 | 6.647501 | -4.72% |
| Native instructions | 411,811,593 | 412,221,857 | +0.10% |
| Compilations / evictions | 5,813 / 0 | 5,813 / 0 | 0 / 0 |
| Peak code-cache bytes | 4,204,845 | 3,712,640 | -11.71% |
| Window links | 51,157,511 | 51,219,687 | +0.12% |
| Fast window links | 46,617,262 (91.12%) | 49,712,380 (97.06%) | +6.64% (-66.80% slow lookups) |

Canonical CPU (`137783ecc64f2fdb7f440ae1a4fc0b923fcf944ad1704d739b05b6651ea3aa9a`) and 32 MB SDRAM (`29c365d657fd742f343c523922a4398756e75f71b1eb3197a084b23e6d4b2d44`) hashes match bit-for-bit.

### Wade Hixton's Counter Punch (2,400,000,000 steps, ratio 6)

| Metric | Reference | Candidate | Change |
| --- | ---: | ---: | ---: |
| Process CPU seconds | 29.046875 | 27.953125 | -3.77% |
| Wall seconds | 29.736063 | 28.376676 | -4.57% |
| Native instructions | 1,585,593,126 | 1,584,265,812 | -0.08% |
| Compilations / evictions | 6,836 / 0 | 6,874 / 0 | +38 / 0 |
| Peak code-cache bytes | 6,430,382 | 5,801,962 | -9.77% |
| Window links | 257,496,034 | 257,376,681 | -0.05% |
| Fast window links | 243,545,362 (94.58%) | 254,165,913 (98.75%) | +4.36% (-76.98% slow lookups) |

Canonical CPU (`da648c0e15cb3c9c64c24886aaa559153b202e4ee35d0c79d08871cd58fd8a0a`) and 32 MB SDRAM (`5fe4a1db3c2ce359f42d646dceeb946ff57632e7b7997671d7b292ea3e3bb1fa`) hashes match bit-for-bit.

## Scope and limitations

These are single-order headless JIT-to-JIT comparisons under AC power; they do not establish battery-mode audio reserve, calibrated original-console timing or weak-hardware performance. Installed binaries (`bin/oceanblast.exe`) remain unchanged. The [validation manifest](validation/2026-11-15_shared_native_entry_and_dispatch.json) records exact source, executable and state hashes, CTest timings and engine counters.
