# ARM register-return execution

Date: 2026-10-18. Continues [native coverage and sampled compilation](103_native_coverage_and_sampled_compilation.md).

ARM `MOV PC, Rm` with no flag update and an unshifted general-register source now terminates a translated block through a dynamic register target. It preserves CPSR and aligns the target as the reference does. Conditional failures fall through normally. PC source operands, shifted PC writes and exception-return forms remain reference exits. Dynamic targets cannot be followed as static edges or mistaken for native loop backedges.

The boot diagnostic counted 700,418 eligible register-return exits in its first 600 million Wade steps. This establishes frequency in that fixture, not a speedup or a complete title/gameplay profile. Common block transfers accounted for 12,615,039 further cached scalar exits there; these remain the next implementation slice.

All six suites pass in 8.07 seconds. The new 1,920-case comparison covers every ordinary condition against all NZCV combinations, sources R0/LR and all four target low-bit patterns. Every case executes natively and matches the reference. The final build separately reaches the inspected Wade title state at 2.4 billion steps, matching the preceding CPU, complete SDRAM and snapshot hashes exactly.

The [manifest](validation/2026-10-18_register_return.json) records source/executable identities and checks. No new speed percentage is claimed. The preceding consolidated build failed the short live speed/audio gate; this technical return slice does not close that failure, so no additional listening or gameplay sweep is appended and no usable test-EXE is advertised. The installed player is unchanged.
