# Conditional fallthrough region candidate

Date: 2026-10-18. This short J4 candidate follows conditional branches through only their fallthrough path and exits precisely when the branch is taken. It was rejected after the Wade comparison.

## Result

The candidate passed the six JIT test suites and completed the 600-million-step Wade run. CPU, SDRAM and diagnostic snapshot hashes matched the reference. In the single reference-then-candidate pair, process CPU time was 17.95 seconds for the reference and 21.64 seconds for the candidate (about 20.5% more). Both runs reported AC power and used affinity mask 4; the run was headless, with audio and pacing disabled. No second pair was run.

The candidate is not retained and no test executable is ready from it. Matching guest state establishes correctness for this fixture, not a speedup. This measurement applies only to this build, host, scene and run order.

## Next step

Keep conditional branches as precise exits until a bounded native region can represent and execute both successors, including joins, dynamic retired-instruction counts and event deadlines. Test the smallest supported control-flow shape first, then run the same short Wade workload once. Do not add the fallthrough-only path back as a standalone optimization.
