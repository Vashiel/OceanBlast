# Cached Dispatch and Mapping Context

Status: execution-loop and cache changes delivered in a bounded development slice, 2026-10-18. Production pacing, audio and weak-host reserve remain separate acceptance gates.

## Execution and emission

Safely fetched unsupported instructions now have cached scalar exits. They retain physical-source, instruction-set, privilege, CP15 and generation checks, and execute exactly one reference step on every use. They share the bounded LRU with native entries; unavailable/device fetches and prefetch faults are never cached as ordinary scalar exits. Allocation failure still falls back before guest execution. Native and scalar cache-hit counters remain separate.

A 1,024-slot direct table points to live LRU entries. Every hit checks complete context, lookup budget, physical code version and mapping/epoch generations. Collisions use the ordinary indexed lookup; eviction and reset remove pointers before releasing entries. Matching the lookup budget prevents a short entry from hiding a previously cached longer block. LRU ordering remains in force. The direct table costs approximately 24 KiB on this x64 build.

The emitter avoids unused operand/status work. Unconditional operations preserving flags do not load CPSR unless carry/RRX needs it; unary MOV/MVN do not load an unused left operand. Constant-shift carry is calculated only when logical flags need it. Conditions inspect the necessary CPSR masks directly instead of extracting every NZCV bit for every condition. Existing scalar/native semantic cases remain the oracle.

Within one batch call, native RAM/integer blocks share a known absolute device deadline. Each new call recomputes it; reaching the deadline, any reference fallback or a generated device/fault/code-write exit discards it. Eligible IRQ and level-source checks remain at boundaries. A dedicated test arms Timer 4 from a generated MMIO store and verifies the subsequent interrupt against scalar execution.

Supported conditional immediate byte/word accesses now use the same condition evaluation before any memory side effect. Failed conditions consume a guest instruction and its time, without translation, memory access or a data abort. PC operands, register offsets, unprivileged post-index variants and unsupported overlaps remain scalar. Matching the current reference does not establish hardware correctness of those remaining forms.

A terminal direct branch back to its own block entry can repeat inside the generated function. Its live condition, registers and memory operations execute on every iteration. The supplied instruction/event budget bounds the total; faults, MMIO and code writes retain their early exits. This coalesces dispatch boundaries without skipping guest work or compressing time. Actual retired counts and internal backedges are reported; the original source dependency still covers the repeated block.

## Cold execution and promotion

The developer player now uses a compilation threshold of three entries. A supported block initially executes through the existing precise reference boundary; repeated entries promote it to generated code after source/context validation. The promotion decoder reads the already validated source words, so cold entries do not retain another IR tree. Unsupported instructions remain scalar exits. Native/fallback/cold counts and promotions are separate counters.

Promotion applies the same code-byte bound and can evict older functions while preserving the selected entry. A one-function-budget fixture verifies this lifecycle. Immediate compilation remains available through `NativeCacheLimits::compileThreshold = 1`; the native semantic oracle uses that policy to require generated execution on first use. Cold execution does not introduce another interpreter implementation.

## Cache working set

The earlier 4,096-entry limit caused 848,645 compilations and 1,394,692 evictions during the 600-million-instruction Wade boot fixture. An intermediate 16,384-entry bound still stopped at only about 2.7 MiB emitted code. The default entry ceiling is now 65,536, with the existing 8-MiB emitted-code limit retained. Scalar exits count toward the entry ceiling even though they allocate no generated function.

The eager code-budget check reaches the 8-MiB code cap and peaks at 61,108 live entries. It records 146,383 compilations and 178,657 evictions, with the same guest state as the smaller-cache runs. The final promoted-block build records 78,508 compilations, 148,804 evictions and about 6.6 MiB peak code at the entry ceiling. These counters explain reduced translation churn; they are not a controlled speedup measurement. Entry/source vectors, list/hash nodes, allocator bookkeeping and the direct table consume additional memory beyond emitted-code bytes. The entry ceiling bounds this metadata; the manifest does not equate the code cap with total process memory.

## Privilege and mapping checks

The visible startup check exposed repeated full source validation in an already compiled section. Previously, changing bus privilege advanced the global mapping generation. User/kernel alternation consequently invalidated entries for both contexts despite unchanged mappings and code.

Privilege now belongs to cache identity: both guest CPSR mode/Thumb state and actual bus user mode are checked. I/F interrupt masks are evaluated live at entry and are preserved by emitted operations; they do not require duplicate machine code. A warm-cache test changes those masks, then verifies that an eligible IRQ still enters before guest work. Privilege changes still invalidate the scalar instruction-address cache, but do not assert a physical mapping change. MMU enable, TTB/domain changes and TLB maintenance still advance mapping generation.

After a mapping-generation change, reuse first performs the current permission-checked translation. When the resulting physical source is identical and the code-page version and whole-memory/DMA epoch are unchanged, it refreshes mapping validation without rereading instruction bytes. A changed physical mapping, code version or epoch retains exact source validation or recompilation. A permission fault always takes the reference prefetch-abort path. No cached privileged translation can be used solely because the guest PC matches.

## Technical checks and runtime boundary

Six targeted suites pass, including 4,580 native semantic cases and direct-branch fixtures. New checks cover cached scalar reuse/reset/native replacement, alias writes, instruction permissions, direct-table collisions, changing block budgets and a timer armed inside a batch. Additional mapping fixtures cover alternating user/kernel contexts, unchanged physical code after TLB maintenance, changed user permissions, host bus privilege independent of CPSR, and a remap to different physical code. Existing DMA, code modification and fault-ordering checks remain in force.

The final fixtures also cover 720 conditional memory cases, repeated blocks ending at a not-taken branch, every legacy phase at external deadlines, promotion after cold execution and promotion under code-budget eviction. The existing mixed-memory PCM fixture now exercises repeated native blocks against all 16 scalar DMA samples.

The [validation manifest](validation/2026-10-18_cached_dispatch.json) separates exact executables, intermediate checks and the final runtime result. Earlier matched 100-million-step boot pairs reduced JIT process CPU time from 2.671875 s at the start of this work block to 1.375 s after the execution-loop/emitter changes; their paired interpreter observations were 1.125 s and 1.28125 s respectively. These are short single-order observations, not reversed-pair performance acceptance. CPU, complete SDRAM and diagnostic snapshots match in each pair.

The longer 600-million-step checks also match the stored reference CPU/SDRAM/snapshot. They reach the Engine Software logo, so they do not establish title/gameplay or live-audio quality. The first visible GUI/audio run was stopped after low speed and repeated queue starvation became evident; its forced test termination is not recorded as a product crash. The mapping-context changes address a counter-derived cause of repeated source checks. Runtime evidence for the final executable is recorded separately in the manifest; earlier results must not be attributed to a later binary.

The installed launcher is preserved. A separate EXE can be offered for a first user test only with its exact smoke-check limits stated; production acceptance additionally requires correct pacing, listening, gameplay and CPU reserve.

## Final runtime result and next delivery

The final executable matches the recorded CPU, complete SDRAM and diagnostic snapshot after 600 million Wade instructions. It records 17,496,490 internal backedges. The visible GUI/audio check still fails: after the input rate settles near 21,972 Hz, four consecutive intervals show approximately 36-45% modeled speed while empty-queue events rise from 34 to 54. The watchdog stops that process after 12.7 seconds, before the planned 1.5-billion-step limit. No gameplay sweep or user listening request follows. This build is not offered as a usable game test; both the installed launcher and previously prepared isolated package remain unchanged.

A separate 1.5-second host-PC sample starts after the 400-million-step marker in a headless replay of the exact final executable. There are 759 in-module and 48 outside-module samples, with no failed captures. Release DWARF lookup provides no source attribution. Instead, exact PE `.pdata` function ranges are matched to COFF function symbols at the identical range starts. The [samples](validation/2026-10-18_jit_host_samples.csv) and [function summary](validation/2026-10-18_jit_host_functions.txt) retain that distinction.

`Impl::reusable`, `runUntilDeadline` and `run` account for 403 of 807 samples, approximately 50% including the outside-module denominator. Outside-module samples are unattributed; they are not all labeled generated code. Sampling suspends the CPU thread, so these shares identify host hotspots rather than measuring throughput. The profile covers this startup interval only.

The next structural delivery is a native execution loop with safe links between eligible blocks, reducing returns through the C++ cache/runner boundary. Link lifetime must survive eviction/reset safely, and every successor must retain context, physical-code, instruction-budget and device-event guards. Preserve the scalar boundary for unsupported operations and precise fault/MMIO exits. Validate the linked lifecycle and deadlines first, then one short Wade gate. Additional instruction shortcuts or another cache-size sweep are not the next task.
