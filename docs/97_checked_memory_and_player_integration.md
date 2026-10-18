# Checked Memory and Developer Player Integration

Status: checked J3 slice and initial J4 integration delivered, 2026-10-18. The native engine is not accepted for game speed, pacing or live audio.

## Delivered behavior

The ARM frontend now represents unconditional immediate byte/word loads and stores. Generated code computes addressing and applies destination/base updates only after a successful checked bus access. Alignment, rotated word loads, permissions, MMIO and data-abort entry reuse the reference behavior. Conditional accesses, PC operands, register offsets, unprivileged variants and unsupported overlap cases still execute through the interpreter.

An integer prefix and its following memory access now share a generated block. The prefix materializes state and advances its legacy tick phase before the checked access; the block ends at that access. Entry deadline bounds stop before a later access when an event is due in the prefix. A memory instruction encountered at entry still forms a one-instruction block. No generated work runs beyond an MMIO operation, abort or code-writing store. Blocks containing several ordinary RAM accesses remain future work.

ARM B/BL and Thumb conditional/unconditional direct branches now end generated integer blocks. Their conditions, branch target, link register and legacy timing phase agree with scalar execution in short differential fixtures. Complex control flow remains scalar. The backend's batch entry now executes successive internal boundaries within the caller's budget, rather than returning to the application after each generated block.

## Code dependencies and bounded reuse

The cache uses an entry-PC index and LRU eviction, bounded by 4,096 entries and 8 MiB of emitted code. The 64-KiB per-function cap remains. Entry identity still includes instruction-set/mode and CP15 context; code dependencies retain physical addresses and source words.

The bus tracks versions of watched physical 1-KiB SRAM/SDRAM pages. Normal byte/halfword/word writes, slow physical writes and SDRAM mirror writes notify the tracker. MMU enable, privilege, table/domain changes and TLB maintenance advance a mapping generation. Reset and cartridge loading invalidate a whole-memory epoch. OHCI direct RAM updates conservatively invalidate the epoch before DMA advancement. Any future raw host RAM writer must also notify this contract before execution resumes.

Unchanged page, mapping and epoch generations allow direct reuse without fetching and comparing every instruction again. A changed generation triggers checked translation and exact physical/source validation before reuse. Alias writes, self-modification, remapping and permission changes retain dedicated checks. Tracking arrays use about 160 KiB for 32 MiB SDRAM plus steppingstone SRAM; writes avoid page tracking until code is first watched. This is a single-machine, single-execution-thread contract; concurrent mutation is unsupported.

## Separate runnable developer build

The optional CMake project now builds `oceanblast_jit_player.exe` with the existing Windows display/audio frontend and embedded resources. Only this dedicated build defaults to JIT; normal builds retain the interpreter. `--engine interpreter|jit` selects the backend. JIT always disables the legacy title-blit shortcut; reference comparisons must also use `--title-blit off`. Guest-time compression is not a native acceptance mechanism.

`--checkpoint <prefix>` writes canonical little-endian CPU fields and all 32 MiB SDRAM, independent of whether the MMU has been enabled. These are diagnostic CPU/SDRAM checkpoints, not complete restoreable machine save states. They contain cartridge-derived data and belong outside source control. Native counters include generated, memory and scalar steps, cache hits, generation-fast hits, source validations, compilations, evictions and failures. GUI runs flush counters periodically; headless runs write their final counters on normal exit.

Build with the [pinned dependency and compiler setup](96_initial_x64_execution.md), then prepare an isolated local test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_jit_test.ps1 `
  -RomPath 'C:\external\Wade.bin' `
  -NvramPath 'C:\external\wade.nvram'
```

The default executable path is `build/jit/oceanblast_jit_player.exe`; `-ExecutablePath` can select another CMake build. The output defaults to ignored `bin/JIT-Test`. The preparation tool copies the EXE and existing control settings, creates separate JIT/reference saves and logs, and supplies two launchers. Existing test saves/settings are preserved. ROMs are referenced externally. The installed player and its original storage are untouched. This package is an early development build, not a performance upgrade.

## Short verification and observed limits

Six focused suites pass: native execution, 4,580 native semantic cases, checked memory, invalidation, scalar CPU regressions and emulation timing. The new direct-branch checks compare 480 ARM and 480 Thumb cases against scalar execution, covering NZCV combinations, taken/skipped conditions and forward/backward targets. Memory fixtures cover addressing/writeback, faults, MMIO deadlines and stores to compiled code. Mixed-prefix checks exercise every phase of ratios 1/3/6/16, interpreter handoff, an external deadline before the access and an abort after prefix results have been committed. Invalidation fixtures cover fast reuse, code writes, mirrors, cross-page writes, bulk invalidation and reset. The normal C++17 interpreter application also builds.

A bounded Wade boot check executes 100,000,000 instructions at legacy ratio 6, with no input, GUI, sound or title shortcut. Canonical CPU state and complete SDRAM agree with the recorded reference checkpoint. The packaged native build records:

| Counter | Result |
| --- | ---: |
| Generated steps | 87,147,254 |
| Generated memory steps, included above | 28,609,975 |
| Scalar fallback steps | 12,852,746 |
| Compilations | 3,362 |
| Cache hits | 49,678,905 |
| Generation-fast hits | 49,678,905 |
| Evictions / compilation failures | 0 / 0 |

Before prefix/access fusion, the same workload recorded 65,668,424 cache hits with identical generated/fallback instruction totals and matching CPU/SDRAM. Fusion reduces cache-hit dispatches to 49,678,905. This counter reduction is structural evidence, not a measured speedup claim. Roughly 49.7 million generated-code entries still perform only 87.1 million guest instructions; frequent memory boundaries and short blocks remain.

The final short comparison uses the same preserved executable for both backends, identical ROM/EEPROM, affinity mask 4 and observed AC power. Interpreter process CPU time is 1.203125 seconds; JIT process CPU time is 3.578125 seconds. This single ordered boot pair is sufficient to reject performance acceptance at this stage. It is not a steady-state game benchmark, reversed-order comparison or weak-host result. Earlier exploratory timings had uncontrolled affinity/power and are not used as accepted speed evidence. A longer 600-million-instruction attempt was stopped at its 60-second limit without a native checkpoint; compilation also overlapped part of that attempt. It establishes neither a CPU mismatch nor title/gameplay acceptance. No further cartridge sweep or listening request was added after this incomplete check. The exact packaged build and short-check hashes are recorded in the [manifest](validation/2026-10-18_jit_memory_player.json).

## Next delivery and acceptance gate

Continue within the committed JIT architecture: extend mixed blocks beyond a single terminal memory access, with precise event/fault exits, MMIO boundaries and code-write invalidation. Avoid a full cache dispatch after every ordinary RAM operation. Retain the scalar oracle and checked slow paths. Block-entry counts, not just generated-instruction percentages, must show that the recurring boundary has actually been reduced.

The next bounded slice must first pass mixed integer/memory, deadline, abort and self-modification checks. Then use one short matched Wade check. Only a healthy result justifies proceeding to title/gameplay and live audio. J4, J5, weak-host CPU reserve and Android acceptance remain open; the installed executable is not replaced.
