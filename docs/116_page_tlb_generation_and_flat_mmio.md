# Per-page TLB mapping generation, compiled-block retention and flat MMIO table

Date: 2026-11-16. Follows [shared native entry ABI and intrusive dispatch streamlining](115_shared_native_entry_and_dispatch.md) and addresses the remaining transition-time bottlenecks observed in live play telemetry and [114](114_chefs_battery_memory_review.md).

## Delivered behavior

1. **Per-4 KB TLB mapping generation and DACR filtering:** `Bus::flushTlbEntry(u32 va)` now advances a 2,048-entry per-page generation table (`pageMappingGeneration[(va >> 12) & 2047]`) matching the 2,048-entry MMU TLB rather than incrementing the global `mappingGeneration` on every single-page invalidation. Full TLB flushes (`flushTlb`, `setTtb` and changed `setMmuEnabled`) continue to advance the upper 32 bits (`mappingGeneration`). `Bus::setDacr` no longer advances `mappingGeneration` because `cp15_dacr` is already part of `CacheEntry::context` and `Arm920TMmu` does not cache domain permissions in TLB tags.
2. **Compiled-block retention across sampling collisions:** In `X64ExecutionEngine::Impl::sampledHotEntry()`, when a PC misses or collides in the 1,024-entry `samples` table, the engine checks whether `cacheByPc` already holds a compiled native function (`candidate->function != nullptr`) for the current guest context. Already-compiled blocks immediately restore their sample slot and enter `reusable()` instead of dropping up to 4,096 instructions into the interpreter fallback batch.
3. **Slab-cached `VirtualQuery` verification:** `X64ExecutionEngine::Impl::verifyExecutableSpan()` caches the base address and size of the last verified `MEM_COMMIT` / `PAGE_EXECUTE_READ` region returned by `VirtualQuery` and clears that cached range on cache reset or eviction, avoiding repeated Windows kernel queries when `JitAllocator` sub-allocates blocks within the same executable slab.
4. **Flat `O(1)` MMIO register table:** `std::unordered_map<u32, u32> mmioRegs` in `Bus` (3.79% of all runtime samples in [114](114_chefs_battery_memory_review.md)) is replaced by a fixed `22 * 256`-word array (`mmioRegs`) indexed by `constexpr Bus::mmioIndex(u32 addr)`, covering all 20 S3C2410 peripheral blocks (`0x48000000`–`0x5B000000`) plus `UART1` (`0x50004000`) and `UART2` (`0x50008000`). Explicit write flags (`iisConWritten`, `iisModWritten`, `iisPsrWritten`) preserve unconfigured audio-prescaler defaults for diagnostic tests.

## Correctness

All 13 CTest suites pass in 20.00 seconds. An added regression in `tests/x64_invalidation.cpp` verifies that `flushTlbEntry` on one 4 KB page leaves cached translations on other pages fast-valid (`mappingRevalidations == 0`) while revalidating and recompiling the flushed virtual page after a page-table update.

## Controlled headless comparisons

All builds use GCC 16.2 Release `-O3 -DNDEBUG -march=x86-64` with PGO disabled in the same build tree, affinity mask 4, disabled title shortcut and observed AC power. Baseline is the [115](115_shared_native_entry_and_dispatch.md) candidate (`b1af3fafe515402467ff6d59eaef1b308ca9b59099277d8316cff3b413bf1684`); new candidate is `d0aaed61c369e15008ec78d6c1b7bf46cad9bd534fc280cf6facbe8f36e00b36`.

### Wade Hixton's Counter Punch (2,400,000,000 steps, ratio 6)

| Metric | Baseline ([115](115_shared_native_entry_and_dispatch.md)) | Candidate | Change vs 115 |
| --- | ---: | ---: | ---: |
| Process CPU seconds | 26.375000 | 21.671875 | -17.83% (-25.39% vs pre-115 HEAD) |
| Wall seconds | 26.735439 | 21.771640 | -18.57% (-26.78% vs pre-115 HEAD) |
| Native instructions | 1,584,265,812 (66.01%) | 2,156,965,208 (89.87%) | +36.15% (+572.7M native steps) |
| Sampled fallback steps | 797,522,097 | 200,479,530 | -74.86% (-597.0M fallback steps) |
| Mapping revalidations | 6,904,254 | 37,344 | -99.46% |
| Window links | 257,376,681 | 312,791,691 | +21.53% |
| Fast window links | 254,165,913 (98.75%) | 311,811,621 (99.69%) | -69.48% slow window lookups |

Canonical CPU (`da648c0e15cb3c9c64c24886aaa559153b202e4ee35d0c79d08871cd58fd8a0a`) and 32 MB SDRAM (`5fe4a1db3c2ce359f42d646dceeb946ff57632e7b7997671d7b292ea3e3bb1fa`) hashes match bit-for-bit.

### Superstar Chefs (600,000,000 steps, ratio 3, scripted input)

| Metric | Baseline ([115](115_shared_native_entry_and_dispatch.md)) | Candidate | Change vs 115 |
| --- | ---: | ---: | ---: |
| Process CPU seconds (pair 1 / pair 2) | 5.093750 / 4.609375 | 5.578125 / 4.734375 | +9.51% / +2.71% (641 more blocks compiled during boot) |
| Wall seconds (pair 1 / pair 2) | 6.346592 / 5.817083 | 5.874592 / 5.674963 | -7.44% / -2.44% |
| Native instructions | 412,221,857 (68.70%) | 442,608,852 (73.77%) | +7.37% (+30.4M native steps) |
| Sampled fallback steps | 183,144,609 | 152,337,509 | -16.82% (-30.8M fallback steps) |
| Mapping revalidations | 3,743,567 | 30,024 | -99.20% |
| Fast window links | 49,712,380 (97.06%) | 54,136,549 (99.39%) | -77.85% slow window lookups |

Canonical CPU (`137783ecc64f2fdb7f440ae1a4fc0b923fcf944ad1704d739b05b6651ea3aa9a`) and 32 MB SDRAM (`29c365d657fd742f343c523922a4398756e75f71b1eb3197a084b23e6d4b2d44`) hashes match bit-for-bit. In the short 600M boot-dominated window, retaining and promoting 641 additional recurring blocks adds ~0.12 s of upfront compilation CPU time while reducing wall time and cutting `mapping_revalidations` by 99.20%.

## Scope and limitations

Installed binaries (`bin/oceanblast.exe`) remain unchanged; the local isolated test package (`bin/JIT-Test-20261115/OceanBlast-JIT.exe`) is updated to `d0aaed61c369e15008ec78d6c1b7bf46cad9bd534fc280cf6facbe8f36e00b36`. Battery audio reserve, hardware timing calibration and weak-device validation remain open. See [validation manifest](validation/2026-11-16_page_tlb_generation_and_flat_mmio.json).
