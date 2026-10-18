# Shared native execution windows

Date: 2026-10-18. Implements the structural boundary identified in [106](106_execution_window_delivery_gate.md).

## Delivered behavior

Bulk execution can run several already compiled, validated blocks inside one device-event window. They share the run-state structure, a global instruction budget and accumulated legacy-time accounting. Each generated block includes the preceding blocks in its retirement count. Ordinary RAM accesses retain checked translation, and a memory helper materializes the complete preceding prefix before a device access or fault. The physical active-code page changes with each block.

An entry can remember its last validated successor. Mapping generation, code epoch, physical source version, target PC and lookup budget guard every reuse. A cache-lifetime generation is checked before dereferencing borrowed pointers; eviction and reset advance that generation before releasing entries. No compilation, promotion or eviction occurs within a shared window. A missing successor returns to the normal runner. Native instructions preserve mode, privilege and CP15 context; unsupported context-changing instructions, faults, MMIO and active-code writes retain their checked exits.

`NativeCacheLimits::linkNativeWindows` enables this policy for bulk execution. `runToBoundary` retains its existing single-block/event contract. Source blocks remain bounded by their existing 32-instruction/permission-span limits. A window retires at most 65,536 instructions and always stops at the earlier caller or device-event boundary. Guest timing and the title shortcut are unchanged.

This is shared host-side dispatch and time accounting. Blocks still return through C++ and retain their individual native function prologues. Direct native successor jumps and cross-block register allocation are not implemented.

## Correctness

All six suites pass in 14.87 seconds. Added comparisons cover every legacy phase for ratios 1, 3, 6 and 16, partial budgets, external deadlines, distinct permission pages whose PCs collide in the hot table, a linked internal loop with a preceding prefix, and the unchanged single-boundary contract. Memory checks cover late-word load/store aborts after a linked prefix, partial results, suppressed writeback and a newly armed timer. Invalidation checks cover writes to a future successor, bulk code epochs, remapped physical code, eviction under a two-entry cache and explicit reset.

Initial failures in the new fixtures were corrected before acceptance: the expected abort link used the wrong instruction address; a test-data write used a virtual address after enabling the MMU; a repeatedly reloaded timer could never expire. The final comparisons use physical host setup writes and a timer path that actually reaches expiry. No emulator semantics were weakened to accommodate the fixtures.

## One bounded Wade comparison

Both normal Release builds execute 2.4 billion steps from the same copied EEPROM, ratio 6, unchanged language and disabled title shortcut. GUI, sound, pacing and PGO are disabled; affinity is 4 and AC is observed before and after. CPU, complete SDRAM and diagnostic snapshot hashes match exactly. The snapshot identity matches the previously inspected Wade title scene.

| Metric | Previous multiple-transfer JIT | Shared windows |
| --- | ---: | ---: |
| Process CPU seconds | 35.703125 | 24.500000 |
| Wall seconds | 35.877 | 24.739 |
| Native instructions | 588,101,592 | 1,136,827,875 |
| Compilations | 5,547 | 6,736 |
| Cache evictions / compilation failures | 0 / 0 | 0 / 0 |

The pair observes about 31.4% less process CPU time. This is a single-order JIT-to-JIT result with host-frequency/scheduling limits, not a comparison with the installed interpreter or a weak-device guarantee. Shared windows traverse 95,799,458 native successors, including 87,866,144 guarded fast reuses. There are 22,709,264 native windows. Generated-code storage rises from 5,622,080 to 6,328,168 bytes.

Candidate SHA-256: `081269a368d1a68c05d1eac2358de02d39d96123d229a1e8b424225b24ac198a`.

## Early live gate

The one live check uses GUI/audio, 120 MIPS pacing, ratio 6 and the same isolated fixture. It closes normally after 8.66 seconds because the last four intervals stay below 90% modeled speed while empty queues recur. Those intervals show approximately 87.3%, 65.0%, 87.0% and 89.8% speed; empty-queue counts rise from 2 to 15. The failure occurs during startup, so no title-to-gameplay sweep or Chefs check follows. No listening-quality claim is made.

The measured implementation gain is retained. The installed player remains unchanged, and usable improved test-EXE readiness remains open. The [manifest](validation/2026-10-18_native_windows.json) records exact inputs, executable/state/source hashes, focused checks, counters and live failure.

## Next implementation boundary

The next structural slice is a shared native entry/exit convention with guarded successor transfer, retaining this window's precise budget, fault, device and invalidation boundaries. It must remove per-block host returns rather than add more compiler/profile variants. This is not implemented or measured here. Full-rate live output, listening and a matched installed-player comparison remain delivery gates.
