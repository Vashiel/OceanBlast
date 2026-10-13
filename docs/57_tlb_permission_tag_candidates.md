# TLB Permission-Tag Candidate Checks

The [host CPU samples](54_host_cpu_samples.md) identify the TLB hit path as a possible optimization target. Encoding access permissions in precomputed read/write tags can reduce checks on a permitted hit. This remains a performance hypothesis; the candidates below were checked for correctness before throughput measurement.

The baseline is revision `81cac21`. Three isolated candidates replace `TlbEntry`'s virtual-page, AP and valid fields with read/write tags and a physical page base. No candidate changes were adopted in the production CPU or bus sources.

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

## Reproduction and adoption gate

[The ROM-free diagnostic](../tools/probe_tlb_permissions.cpp) prints observations rather than treating the baseline's known limitations as passing regressions. Build and run it against the current bus implementation:

```powershell
g++ -std=c++17 -O2 -Isrc/memory -Isrc tools/probe_tlb_permissions.cpp src/memory/bus.cpp -o build/tlb_permission_probe.exe
./build/tlb_permission_probe.exe
```

For an isolated candidate, put its memory-header directory before `src/memory` on the include path and compile its matching `bus.cpp`. The original bus sources remain unchanged.

Before benchmarking, a corrected candidate must pass these checks, exhaustive AP read/write and mode-transition cases, invalidation and TLB-index collision checks, and the existing CPU/timing/UART regressions. Unsupported descriptor behavior and any synthetic-mapping policy changes must be documented separately. No throughput or gameplay improvement is claimed for any candidate.
