# Development Roadmap

Updated: 2026-10-15. All milestones below are planned, not completed validation.

## Goal and priorities

Improve usable game performance on weaker CPUs while preserving the working cartridge compatibility. Keep the current interpreter as a reference and evolve the existing implementation in small, reviewable steps. A target CPU has not yet been selected, so this roadmap makes no minimum-hardware or universal full-speed promise.

Current measurements place most host cost in CPU execution and memory translation. This makes execution architecture a higher priority than frontend cleanup. Existing throughput and gameplay observations remain build-, host- and scene-specific; see [runtime acceptance](docs/50_runtime_acceptance.md), [host costs](docs/53_host_execution_costs.md) and [candidate experiments](docs/56_host_fast_path_candidates.md).

| Priority | Milestone and concrete output | Why this comes next | Completion criterion |
| --- | --- | --- | --- |
| P0 | Establish one short technical check and a short visible title-screen check for the affected cartridge. Record executable, cartridge, settings, observed symptom and result. Select a target CPU before claiming weak-device support. | Fast decisions need a reproducible baseline and an explicit stopping point. Reuse existing tools and fixtures. | A contributor can perform the relevant check within the task's test budget and reach a pass/fail decision. |
| P1 | Separate architectural CPU state from the interpreter execution strategy; introduce a bounded run interface while retaining the existing instruction implementation. | Prepares alternative execution engines and direct state comparisons without rewriting the emulator. Avoid per-access virtual dispatch in hot memory paths. | The existing interpreter runs through the new interface, the relevant CPU/timing checks pass, and the short affected-cartridge check shows no new issue. No speedup is required for this structural milestone. |
| P2 | Give execution and peripherals a shared scheduling boundary: run until an instruction budget or the next modeled event. Centralize time advancement incrementally. | Larger execution blocks need clear interrupt, timer and DMA boundaries. The legacy ratio mode and experimental register-clock mode currently require separate treatment. | One bounded execution path uses the shared boundary with targeted event/interrupt checks passing. Preserve existing cartridge presets until replacements are validated; a single universal timing model is not an immediate acceptance requirement. |
| P3 | Separate MMU/TLB semantics from physical RAM and device access responsibilities. Address each known permission or invalidation issue as its own change. | Correct memory semantics are needed before cached or compiled execution. This also allows fast-path costs to be evaluated against an equivalent reference. | Each changed rule has a focused regression check. A performance comparison uses matching semantics; known limitations and unsupported cases remain explicit. |
| P4 | Prototype one alternative execution approach in isolation: a decoded block interpreter or a small ARM/Thumb JIT experiment, chosen from measured hot paths. | Repeated instruction decoding is a plausible opportunity, but previous decoder/ALU experiments did not establish a gain. A prototype must earn further investment. | A short same-build-profile comparison preserves the relevant guest state and shows enough benefit to justify the next slice. Reject clear losses; inconclusive results are reported as inconclusive rather than extended indefinitely. Any retained code cache must handle writes, mapping changes and execution boundaries correctly. |
| P5 | Extract a reusable machine/session component from application orchestration; reduce frontend coupling as needed. | Improves testing and future platform support, but GUI cleanup has lower priority than measured CPU and translation costs. Extract only the pieces earlier milestones actually need. | The normal launcher and a bounded command-line run use the shared component with their relevant smoke checks passing. |

P1-P3 prepare the execution core; they do not promise an immediate FPS increase. P4 is a decision gate, not a commitment to a full JIT. No whole-project rewrite is planned. A newly introduced boot, display or audio regression takes priority over the next structural milestone.

## Task budgets and decision rules

Break milestones into independently reviewable tasks. The following are default elapsed-time budgets per task, including investigation, implementation, build and validation; they are not estimates for an entire milestone.

| Task type | Total budget | Validation allowance within that budget |
| --- | --- | --- |
| Small fix | 15-30 minutes | 2-5 minutes |
| Bounded structural change | 45-60 minutes | 5-10 minutes |
| Performance experiment | 30-45 minutes | One short reference/candidate comparison, normally within 5-10 minutes |

Before starting, name the expected result, scope, relevant check and budget. Compilation and PGO training count toward that budget. Reuse a suitable existing baseline where its provenance is known; do not compare unlike compiler profiles and call the difference an implementation gain.

At the limit, stop and report one of: completed, rejected approach, or incomplete with a concrete blocker and proposed next slice. Ask the user before extending the agreed budget or starting a materially larger investigation. Do not silently add repeated comparisons. If the planned build or necessary check cannot fit, narrow the task or agree on a different budget before starting it. Never describe an untested result as accepted merely because time expired.

## Short checks first; stop on sufficient evidence

1. Run the smallest relevant technical check. CPU, MMU and timing changes require their targeted regression checks; documentation changes require document/link checks, not emulator builds.
2. For changes affecting runtime behavior, launch the affected cartridge with the appropriate executable and settings. Inspect the earliest screen that can answer the question. A title screen is sufficient when it already exposes the relevant pacing, rendering or audio problem.
3. **Stop immediately when the relevant failure is evident.** Do not wait for gameplay, finish an input script or collect more scenes after an obvious title-screen failure. Fix or reject the candidate first. Do not force gameplay progression when an earlier scene supplies sufficient evidence.
4. If the early check passes, advance only when a later scene tests something material that the early screen cannot cover. A title-screen pass does not establish gameplay, save behavior, long-run stability or audiovisual synchronization.
5. Stop after the predefined acceptance check passes unless a concrete new failure or unresolved question justifies another check. For a performance experiment, use one short comparable interval first. Repeat only to resolve a specific ambiguity and within the agreed budget. A small noisy difference remains inconclusive.

The contributor's short self-test is the primary routine acceptance check. When useful, ask the user whether the result feels better after reporting what was actually checked. User feedback is secondary and does not replace the contributor's basic verification. If local visible or listening checks are unavailable, state that limit and request only the missing observation.

Long gameplay sweeps, repeated benchmark pairs and broad cartridge coverage are reserved for a specific late-occurring problem, a justified shared-core risk, or release validation. Define their scope and time budget separately. Earlier research reports retain their historical methods; their long runs are not the default workflow for every new task.

## First implementation slice

The [first execution slice and short check](docs/58_execution_boundary_and_short_checks.md) are recorded as of 2026-10-15. P0 now has a bounded title-screen procedure and executable/settings records; selection and measurement of a weak target CPU remain open. P1 now shares one interpreter execution boundary between single steps and batches. CPU-state separation remains open. Instruction semantics, timing policy and cartridge presets were retained.

Next: take a separate P1 task to extract architectural CPU state without changing its execution behavior. Keep TLB corrections, new optimizations and frontend redesign outside that task.
