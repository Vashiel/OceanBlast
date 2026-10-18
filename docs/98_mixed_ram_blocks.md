# Mixed RAM Blocks and Precise Early Exits

Status: mixed-block execution and bounded time accumulation pass technical checks, 2026-10-18. The final implementation has not yet run the next Wade gate. No game-speed or audio acceptance is claimed.

This report preserves that delivery slice. The [execution-loop, cache and mapping-context continuation](99_cached_dispatch_and_mapping_context.md) records the subsequent Wade checks and implementation.

## Change

The ARM frontend can now retain several supported byte/word memory instructions within one straight-line block, mixed with integer work and a terminal direct branch. The existing 32-instruction, 128-byte and 1-KiB permission-span bounds remain. Actual addresses are checked at runtime; an instruction's inclusion in the IR does not assert that its address is RAM.

Each supported access translates its data address once, retaining normal read/write permissions and fault attribution. Physical access uses the established bus paths, including framebuffer capture and code-write tracking. Loads retain rotated unaligned-word behavior; stores retain aligned-word behavior. Destination/base updates occur only on success. Other addressing forms and system instructions still use the scalar implementation.

The generated function records its actual retired-instruction count before each access. Successful ordinary SRAM/SDRAM accesses continue inside the function. A device or other non-RAM access executes once and returns immediately. A data abort returns at its faulting instruction. A store to the executing block's physical 1-KiB source page returns before later generated instructions can use stale code. Virtual aliases are compared by physical address; physical mirror accesses conservatively take the non-RAM boundary. Existing page generations validate later cache reuse.

## Time and device ordering

Entry is bounded by the first modeled device event and the external tick deadline. Eligible pending IRQs enter through the reference path; pending level-source refresh still restricts entry to one instruction. Supported integer operations do not change execution context, and ordinary RAM accesses cannot arm device events. With no concurrent host mutation, no new device event can occur earlier inside such a block.

The final implementation therefore accumulates ordinary RAM/integer time until block return. Before a device access or data abort, it first commits all preceding instructions' legacy ticks and phase. The boundary instruction's time is committed at return. A code-writing store also returns with its exact completed work. This preserves the access's position relative to device events while avoiding a second C++ completion/guard call and repeated bus ticking after every RAM operation. Byte/word helper selection is fixed during emission.

This guarantee depends on the current event scheduler and single-execution-thread contract. New RAM writers, device callbacks or operations capable of changing deadlines/context must be integrated into that contract before they can continue inside a block. Matching the scalar implementation remains distinct from physical-console timing acceptance.

## Short checks

All six targeted suites pass: native execution, 4,580 native semantic cases, checked memory, invalidation, CPU regressions and emulation timing. New memory fixtures cover:

* Several dependent stores/loads and arithmetic in one compiled function, every phase of legacy ratios 1/3/6/16, and continuation through the interpreter.
* An exact external deadline within a sequence of RAM accesses for every phase.
* SRAM and SDRAM-mirror stores modifying the next translated instruction, with an exit before the old instruction executes.
* A second-access abort preserving the first store and skipping later work.
* MMIO after RAM, ending the block at the access with scalar-equivalent device timing.
* A mixed RAM loop repeatedly changing the DMA source while audio runs: all 16 generated PCM samples and DMA completion registers match scalar execution.

The last full suite completes in approximately five seconds on this host. This is validation duration, not emulator throughput. Tests and final executable/source hashes appear in the [manifest](validation/2026-10-18_mixed_ram_blocks.json).

## Intermediate Wade result and final-build limit

One short Wade comparison was performed during this work block, before time accumulation replaced per-access completion checks. Both modes used the same preserved executable, the same ROM/EEPROM, 100 million instructions, ratio 6, no input/GUI/sound/pacing, affinity mask 4 and observed AC power. CPU, full SDRAM and diagnostic-snapshot hashes matched.

| Intermediate build observation | Result |
| --- | ---: |
| Generated / scalar steps | 87,147,254 / 12,852,746 |
| Generated memory steps | 28,609,975 |
| Cache hits | 29,734,073 |
| Compilations | 2,578 |
| Interpreter process CPU time | 1.1875 s |
| JIT process CPU time | 4.40625 s |

The preceding terminal-access implementation recorded 49,678,905 cache hits for the same instruction totals. Mixed blocks reduce recurring dispatches, but the intermediate implementation's extra per-access completion checks are too expensive. Its single ordered boot pair rejects performance acceptance; it is not a steady-state benchmark or a speed measurement of the final implementation.

The final source removes those recurring completion checks using the bounded-time contract above. It passes the technical fixtures, including DMA ordering, but no second Wade run was added to this work block. Its game correctness and speed remain unmeasured. The manifest distinguishes the intermediate measured EXE from the final technically checked EXE. The installed player and the previously prepared isolated test package remain unchanged.

## Next gate

Run one short matched Wade CPU/SDRAM/snapshot check on the final build and record process CPU time and block-entry counts. Stop if it fails or remains decisively slower; do not proceed to title/gameplay or ask for listening feedback at that point. A healthy result can justify the next bounded machine-integration check. J4 and J5 remain open.
