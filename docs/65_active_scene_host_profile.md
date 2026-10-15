# Active-Scene Host Profile

## Why this measurement

The MMU outlining and LDM span candidates did not improve the active gameplay comparison. The earlier host sample covered startup/title execution, so it was insufficient to select further LDM work. This profile samples the current production interpreter during the established active Superstar Chefs replay instead of adding another speculative memory fast path.

## Conditions and boundaries

The unchanged normal GCC `-O3 -march=native` unity executable has SHA-256 `1849572904e1e92f66569ccc2503392e4aaf591cec40f8a883453789be9d8e62`. It uses GUI/audio, no host pacing, ratio 3, the [active input fixture](../tests/chefs_active_scene.txt), isolated settings and the same initial EEPROM fixture as the paired measurements. The [metadata](validation/2026-10-15_active_host_metadata.json) retains hashes and timing boundaries.

After the flushed host-cost marker at step three billion, the existing sampler observes the highest-CPU-time thread for six seconds with a requested two-millisecond interval. The replay remains unfinished when sampling ends, then completes normally at four billion instructions. Thus the sample lies within the three-to-four-billion-step section but does not cover its entire duration; exact guest steps at sampling start and end were not captured. Final CPU, SDRAM and framebuffer hashes match the active reference.

An initial attempt used a different EEPROM initialization and was discarded. The single corrected sample restores the existing fixture; its initial hash matches the comparison records. No emulator compilation or other replay ran concurrently. Sampling suspends the thread briefly and changes its execution conditions, so this run is not a throughput benchmark or audio acceptance test. No new visual or listening acceptance is claimed.

## Observations

There are 2,194 in-module samples, eight outside the executable module and zero failed samples. Shares below use the in-module denominator. The [raw offsets](validation/2026-10-15_active_host_pc.csv) and [resolved summary](validation/2026-10-15_active_host_summary.txt) are preserved. Resolution uses the exact executable and DWARF inline information through `tools/analyze_host_samples.py`.

| Resolved source group | Share |
| --- | ---: |
| CPU | 67.6% |
| Memory/peripherals | 24.7% |
| Execution scheduling/batching | 7.2% |
| Other groups combined | about 0.5% |

| Resolved function | Share |
| --- | ---: |
| ARM dispatcher (`stepARM`) | 20.1% |
| Instruction step (`step`) | 17.0% |
| General simple-ALU routine | 13.4% |
| Peripheral advancement (`Bus::tick`) | 7.8% |
| MMU translation | 7.5% |
| Legacy execution batch | 7.1% |
| Retained immediate no-flags shortcut | 6.3% |
| Instruction fetch | 4.0% |
| ARM multiple-register transfer | 2.4% |

One UART level-interrupt check line accounts for 4.1% of samples; the major ARM dispatch switch line accounts for 5.3%. Line attribution can include nearby instructions and compiler scheduling. These figures do not measure exact statement costs, guest opcode frequencies, exclusive hardware cycles or the time removable by an optimization. The 2.4% multiple-transfer share also excludes translation work attributed elsewhere. The profile does not establish that LDM is never worth optimizing; it does not justify another LDM specialization now.

## Next candidate

Test a narrow direct path for unconditional immediate ARM arithmetic/logical operations that set NZCV flags. The retained shortcut currently accepts only the no-flags subset. The general simple-ALU routine and dispatcher remain material in this active sample, making a bounded extension a better next hypothesis than another unmeasured LDM change. This profile does not reveal the frequency of eligible flag-setting immediate instructions, so it does not predict a gain.

Keep carry-dependent arithmetic, conditional instructions, PC operands and unsupported encodings on the existing path initially. Verify rotated-immediate shifter carry, arithmetic carry/overflow and preservation of unaffected flags against the reference interpreter. Keep the existing no-flags path inexpensive. Use one matching-build active-scene comparison; reject a loss or inconclusive difference. The previously rejected register-operand extension is not reinstated. No candidate implementation or new executable is delivered by this profiling task.
