# JIT Dispatch and Region Candidates

Date: 2026-10-18

This note records two bounded candidates tested against Wade after the native-region integration. Neither produced enough evidence to retain a change or prepare a test executable.

## Two-successor conditional region

The candidate emitted both successors of a conditional forward branch into a bounded native region, with dynamic instruction retirement and permission-page constraints. The reference and candidate each completed 600 million steps at ratio 6. CPU, SDRAM and snapshot hashes matched. Process CPU time was 10.296875 seconds for both runs. The candidate had 19 compilation failures and its generated/fallback/cache counters were effectively unchanged. There was no measured throughput benefit, so the candidate and its test were reverted.

The pair used executable SHA-256 `6009a0428d610972e63a8491784e9fef37882f8f1dc51377f595202459af35ff`, ROM SHA-256 `2d100b17fe7da2bc316f6fc5a7814aafde4e08749862a3a5d27e5b639634e19a`, and EEPROM seed SHA-256 `47c12bf246aa2c887d4706d9cf8bc1f207df6ba18ac373703adbbe4c173aebbc`. The pair was headless and unpaced; it does not establish live audio or gameplay acceptance.

## Larger translation window

A second candidate increased the maximum translated region from 32 instructions/128 bytes to 128 instructions/512 bytes. Six targeted test suites passed, including an 80-instruction region check. In the single Wade pair, both variants completed 600 million steps at ratio 6 with matching CPU, SDRAM and snapshot hashes. CPU time was 11.6875 seconds for the reference and 10.828125 seconds for the candidate, while wall time moved from 11.932 to 12.491 seconds. The candidate also had substantially fewer direct cache hits, while generated-step and compilation totals remained nearly the same. Because this was one pair with materially different host scheduling and no corresponding workload-coverage improvement, it is inconclusive and the candidate was reverted.

The pair used executable SHA-256 `7e2151168ba03a42462426d9db8f9149ca51ac43611efb857d34c64f2b31e7b7` with the same ROM and EEPROM hashes listed above. The next test executable must be built from the restored known baseline, not either candidate.

## Profile-led next investigation

A 1.5-second sample of the exact baseline executable recorded 678 samples inside the executable and 122 outside it, with no sampling failures. The executable's own COFF symbol map resolved 413 of the 678 in-module samples (60.9%) to `X64ExecutionEngine::run`. This is hotspot attribution only: the sampler pauses the process and does not measure candidate speed. No source-line attribution or live-paced audio conclusion follows from this result.

The next bounded investigation should inspect the per-block dispatch and cache path inside `X64ExecutionEngine::run`, then propose one code-level change with a specific expected reduction in that path. Run its focused correctness checks and one short Wade comparison only if the change plausibly affects the measured work. The exact baseline executable SHA-256 was `e1516d444b573b1168d881e50a5fcac3b4dd737494cea08a1284a85457a1e207`. Existing installed and launcher binaries have not been replaced by these experiments. A test executable is not ready: the JIT has not passed the short live pacing/audio gate.
