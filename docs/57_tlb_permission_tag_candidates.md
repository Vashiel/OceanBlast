# TLB Permission-Tag Candidate Checks

The [host CPU samples](54_host_cpu_samples.md) identify the TLB hit path as a possible optimization target. Encoding access permissions in precomputed read/write tags can reduce checks on a permitted hit. This remains a performance hypothesis; the candidates below were checked for correctness before throughput measurement.

The baseline is revision `81cac21`. Four isolated candidates replace `TlbEntry`'s virtual-page, AP and valid fields with read/write tags and a physical page base. No candidate changes were adopted in the production CPU or bus sources.

## Cached permissions and subpages

The first candidate marks user-denied accesses with bit 31 and falls back to `translateSlow` when a tag comparison fails. A privileged access first caches an AP=1 section. The probe then changes its RAM descriptor to AP=3 without flushing the TLB and enters user mode. The baseline retains the denial; the candidate rewalks the changed descriptor and permits the access. A denied cached hit therefore cannot be treated as a translation miss.

The baseline also caches the AP field of only one 1-KB subregion for an entire 4-KB small page. Priming a user-accessible subregion incorrectly permits access to an adjacent restricted subregion. This is an existing limitation, preserved by the first candidate. Instruction-fetch address caching at 1-KB boundaries does not resolve it because the underlying translation cache still uses a 4-KB virtual-page key.

The second candidate stores four user-denial bits per tag and a section/page fault-type bit. It returns cached permission faults without a table walk. The two checks above pass, and a cached AP=2 section write returns fault `0xD`, matching the cold translation rather than the baseline's cached `0xF`.

## Remaining contradictions in the second candidate

The synthetic kernel-linear fallback caches user-denial bits but returns the translated address unconditionally when filling the entry. In user mode, its first access succeeds and its second access faults. The baseline allows both. Changing the fallback's access policy needs a separate compatibility decision; permitted and denied results must not depend on whether the entry is already cached.

For the currently accepted second-level descriptor type `3`, the candidate builds tags from AP0 for every subregion but retains the slow path's subregion-specific AP selection. A descriptor with AP0=3 and AP1=1 therefore denies the first access to subregion 1, fills a permissive tag, and allows the second access. The implementation must first settle the supported descriptor format and then apply the same rights and address semantics to cold and cached translations. This probe does not establish that type `3` is a valid coarse-table descriptor on the target hardware.

| Probe | Baseline | First candidate | Second candidate |
| --- | --- | --- | --- |
| Cached denial survives RAM descriptor change without invalidation | Yes | No | Yes |
| Adjacent restricted 1-KB subregion remains denied | No | No | Yes |
| Cached/cold AP=2 section write fault | `0xF` / `0xD` | `0xD` / `0xD` | `0xD` / `0xD` |
| Kernel fallback cold/cached user access | Allowed / allowed | Allowed / allowed | Allowed / denied |
| Type-3 AP0=3, AP1=1 cold/cached access to subregion 1 | Denied / denied | Denied / denied | Denied / allowed |

## Corrected candidate

The third candidate preserves unrestricted tags for the synthetic kernel fallback and encodes all four AP fields for both currently accepted small-page descriptor types. All five diagnostic scenarios now have consistent results: cached denials survive descriptor changes, restricted subregions remain denied, section permission faults retain their type, and fallback/type-3 access does not change on a repeated lookup. The existing ROM-free CPU, emulation-timing and UART-interrupt regression suites also pass with the isolated candidate.

These bounded checks establish neither complete MMU fidelity nor a throughput improvement. Domain-control and system/ROM protection semantics need their own architectural review; preserving the existing coarse-table type-3 handling does not validate it against hardware. The candidate is ready for further correctness checks and controlled performance comparison, not a claim of perfect emulation.

The candidate also fills translation entries before returning a permission fault, whereas the baseline fills them only after a permitted access. This is another behavioral change to review independently of tag-comparison speed. A comparison against a correctness-equivalent reference is needed to separate the cost of permission fixes from the benefit of precomputed tags.

## Preserving cold-fault cache behavior

The fourth candidate moves every descriptor-based TLB fill after the cold permission check. It therefore preserves the baseline's existing entry when a forbidden translation collides with that entry's TLB index. The added collision probe caches an allowed section, attempts a forbidden section at the same index, changes the first section's RAM permissions without invalidation, and confirms that the original cached permission remains effective.

An additional large-page probe repeats the same descriptor in all sixteen second-level slots and checks each 4-KB slice across user, privileged and user mode. It verifies translated offsets and read/write permissions in all four 16-KB permission regions. The existing `(pa & ~0xFFF)` calculation already retains the 4-KB slice offset; these measurements do not identify a new large-page addressing defect.

Both additional checks, the five previous diagnostic scenarios, and the CPU/timing/UART regression suites pass with the fourth isolated candidate. The section-fault and small-page rights corrections still differ from the original baseline. Throughput must therefore be measured separately against a correctness-equivalent reference; restoring cold-fault fill behavior alone does not isolate tag-comparison speed.

A separate PGO test executable passes one bounded Superstar Chefs startup comparison against the installed PGO baseline: 1.2 billion steps, ratio 3, the same input fixture and a blank EEPROM. CPU, SDRAM, snapshot and framebuffer hashes all match. Elapsed times are 10.588 s and 10.556 s; process CPU times are 9.234 s and 9.266 s. This single pair establishes startup state equivalence, not a repeatable throughput gain or gameplay acceptance. Both executables use native x64 PGO builds; their training profiles are not established as identical. The [records](validation/2026-10-14_tlb_tags_chefs_startup.csv) retain the exact executable and cartridge hashes.

## Reproduction and adoption gate

[The ROM-free diagnostic](../tools/probe_tlb_permissions.cpp) prints observations rather than treating the baseline's known limitations as passing regressions. Build and run it against the current bus implementation:

```powershell
g++ -std=c++17 -O2 -Isrc/memory -Isrc tools/probe_tlb_permissions.cpp src/memory/bus.cpp -o build/tlb_permission_probe.exe
./build/tlb_permission_probe.exe
```

For an isolated candidate, put its memory-header directory before `src/memory` on the include path and compile its matching `bus.cpp`. The original bus sources remain unchanged.

Before benchmarking, a corrected candidate must pass these checks, exhaustive AP read/write and mode-transition cases, invalidation and TLB-index collision checks, and the existing CPU/timing/UART regressions. Unsupported descriptor behavior and any synthetic-mapping policy changes must be documented separately. No throughput or gameplay improvement is claimed for any candidate.
