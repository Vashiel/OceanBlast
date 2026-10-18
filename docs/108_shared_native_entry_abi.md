# Shared native entry ABI fixture

Date: 2026-10-18. Follows [shared execution windows](107_shared_native_windows.md).

The isolated `x64_entry_abi` target establishes a concrete Windows x64 entry/exit convention for the next backend slice. One bridge saves the nonvolatile registers it uses and allocates aligned Windows shadow space once. Internal blocks keep authoritative state in R12 and the shared execution frame in R13. They transfer directly through jumps to separately allocated executable blocks and return through one shared exit. The stack does not grow across block transitions.

The test executes budgets 0, 1, 2, 3, 65 and 65,536, verifies exact retirement and independent arithmetic results, and checks the same aligned stack at every block entry. Alternating blocks call a normal C++ helper with stack-local data; state/frame pointers survive those calls. The focused CTest passes in 0.12 seconds. Existing player execution code is unchanged, so no additional Wade benchmark or live run follows this test-only addition.

This fixture has no guest memory access, exceptions, MMIO, mutable code, cache eviction or successor permission checks. It demonstrates the calling convention and constant-stack cross-allocation transfer only. It is not integrated into the player and provides no measured game-speed or audio improvement. The production interpreter and installed executable remain unchanged.

Next, adapt emitted blocks to this shared entry and route guarded successors through it. Preserve the existing window budget and physical active-code page, materialize exact prefix timing before checked memory helpers, validate borrowed successor lifetime before dereference, and retain mapping/code/permission checks and host exits for MMIO, aborts and invalidation. Reuse the existing cross-page, phase, partial-budget, late-abort and cache-eviction fixtures. Integration still requires those checks and one controlled Wade comparison before any live or delivery claim.

The [manifest](validation/2026-10-18_native_entry_abi.json) records test/source identities and the test output. This is a completed entry-ABI fixture, not a completed direct-link backend.
