# ARM920T CP15 and transfer semantics

## Scope

The x86-64 JIT returns unsupported instructions to the reference interpreter. A fallback count measures which engine handled an instruction. Guest behavior depends on the interpreter operation, the selected timing model, and any part of the hardware that operation controls. This report checks those paths using the cartridge kernels and small ROM-free CPU fixtures. [JIT architecture](92_jit_architecture_decision.md)

The earlier 23-module inventory counted encodings fetched by the interpreter before the ARM condition check. The measurements here count CP15 instructions inside the interpreter handler after the condition check. Each record includes the executing PC, opcode, processor mode and first and last operands. The transfer probe observes the single-transfer decode after address calculation. These counts describe the specified execution windows.

## Kernel cache maintenance and the international Winx cartridge

The Linux kernel cleans data cache entries before another consumer needs the data in RAM, invalidates instruction cache entries when code mappings change, and drains pending buffered writes at ordering points. The ARM920T has separate instruction and data caches with 32-byte lines. Linux also performs cache maintenance around page-table updates, address-space switches and selected DMA operations. [ARM920T TRM, sections 2.3.11 and 4](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb), [Linux ARM920 routines](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/mm/proc-arm920.S)

The guest instructions at `0xc0021aac` and `0xc0021ab4` match the range-flush sequence. The first cleans and invalidates a D-cache entry, the second invalidates the I-cache entry for executable ranges, and the loop advances by 32 bytes. The loop at `0xc0021bcc` processes 512 D-cache entries by index and is followed by I-cache invalidation, a write-buffer drain, a new translation-table base and a TLB flush. These sequences were checked against code read from guest SDRAM. [Linux ARM920 routines](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/mm/proc-arm920.S)

The international Winx combined cartridge was run at legacy ratio 3 for 3,000,000,000 steps with the same `tests/video_language_confirm.txt` input and a fresh 2,048-byte EEPROM in both runs. The reconstructed copy replaces one erased 512-byte data page at raw offset `0x1e5e350`. The intact parts of both compressed blocks and the replacement bytes agree with the IT/ES Winx and SpongeBob combined cartridges. Both zlib streams decompress to 65,536 bytes and pass their stream checksums after substitution. The original CRC32 is `eb1d8fda`. The reconstructed copy has the existing cartridge identity CRC32 `653adfeb`. [ROM integrity](14_rom_integrity.md), [Cartridge identification](../src/core/cartridge_settings.h)

| Observation | Original dump | Reconstructed copy |
| --- | ---: | ---: |
| Executed CP15 `c7` writes, all observed variants | 82,971,911 | 4,102,348 |
| Range-flush operations at `0xc0021aac` and `0xc0021ab4` | 68,788,992 | 3,120,896 |
| Address-space switch cache-line operations at `0xc0021bcc` | 2,769,920 | 117,248 |
| `Segmentation fault` messages | 130 | 0 |
| `SQUASHFS error` messages | 128 | 0 |
| Observed launch of `runatari` with `asteroids` | No | Yes |

The original dump repeatedly faults while reading executable data from SquashFS. The repeated process starts account for the large cache-maintenance workload. The reconstructed copy also reached `runatari asteroids` using the installed x86-64 JIT at the same step budget and ratio, with no logged SquashFS errors or segmentation faults. Boot and program launch establish this result for the recorded scene. Cartridge-wide playability and original physical page provenance remain open.

## Observed instructions and effects

In this table, tuples for `MCR` and `MRC` give `CRn`, `CRm`, and `opcode_2`, with CP15 `opcode_1 = 0`. The Wade case uses 1,500,000,000 interpreter steps and `tests/start_buttons.txt`. The Winx counts above use 3,000,000,000 steps per case.

| Access | Guest purpose | Current emulator behavior and observation |
| --- | --- | --- |
| `MCR c7,c14,1` and `c7,c14,2` | Write modified D-cache lines back and invalidate them. Operands select an address or a cache index. | Ordinary CPU and DMA data share RAM. The operations have no modeled guest cache effect. The interpreter counts the instruction and the JIT can lower it to a no-op. |
| `MCR c7,c5,0/1`, `c7,c6,1` and `c7,c7,0` | Invalidate instructions, data, or both caches, as appropriate to the encoding. | No modeled cache-line state changes. JIT code validity uses its own address mapping and backing-memory checks. |
| `MCR c7,c10,1/4` | Clean a D-cache line or drain buffered writes before a consumer proceeds. The kernel uses this sequence after writing page-table entries and in DMA maintenance. | RAM writes become visible directly. No guest write buffer or drain latency is modeled. |
| `MRC c0,c0,1` | Read cache type and geometry. The boot kernel reads this separately from the processor ID. | `executeCP15` selects the result by `CRn` alone. The read at `0xc000c6c8` returned CPU ID `0x41009200`. TRM table 2-6 specifies cache type `0x0d172172` for the ARM920T. |
| `MCR c3,c0,0` | Change the domain access policy for address translations. A client domain checks page permissions, a manager bypasses them, and an inaccessible domain faults. | The DACR is stored and participates in JIT context selection. The MMU translation paths do not check it. Wade performed 4,522 writes, including `0x17` and `0x15` at `0xc0019840`. |
| `MCR c1,c0,0` | Update the CPU control register. At system-call entry, the guest writes the alignment-enabled control word. | MMU enable and the exception vector base have effects. Cache enables have no guest cache to control. The ROM-free fixture below confirms that the alignment bit does not cause an alignment abort. Wade executed 1,113,329 writes of `0xc0003177` at `0xc0019958` and 1,078,004 `gettimeofday` calls in the same window. |
| `LDRT`, `STRT`, `LDRBT`, `STRBT` | Access data using user-mode permissions while running kernel code. Linux uses these encodings in its user-copy routines. | The single-transfer helper calculates the post-indexed address and performs the access with the current privilege. Wade executed 2,375,558 such transfers, including two `STRT` sites in the kernel copy loop. |
| `MCR c2,c0,0` and `MCR c8` | Select the active page table and invalidate instruction or data address translations. | The table base and TLB invalidations have effects. The emulator uses a shared TLB, and its targeted invalidation works at 4-KB granularity. Wade executed 45 table-base writes and 4,353 `c8` writes. |

The code at the Wade syscall entry loads the control word immediately before writing CP15 `c1`. The same control word was observed on every hit at `0xc0019958`. The count reflects repeated kernel entry while polling time. The nearby kernel source corroborates the alignment-control write and the use of `STRT` for writes into user buffers. [Linux ARM syscall entry](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/kernel/entry-common.S), [Linux ARM user accessors](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/uaccess.h), [ARM920T TRM, sections 2.3 and 3.7](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb)

The same code review confirms that `MRS` and `MSR` read and write processor status, `SWI` enters the guest kernel, long multiplies perform arithmetic, and memory branches load their target. FPA11 instructions supported by the scalar fast path execute there. Unhandled coprocessor instructions take the Undefined Instruction exception, which Linux can handle with its software floating-point emulator. [ARM interpreter](../src/cpu/arm920t.cpp), [Linux NWFPE](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/nwfpe/fpa11.c)

CP15 `c7,c0,4` has a separate wait-for-interrupt path in register-cycle mode and no wait effect in legacy instruction-ratio mode. The three traces contained no such operation. CP15 cache lockdown, TLB lockdown and FCSE PID are missing from the register handler and were absent in these windows. The ARM920T also traps CP15 access in user mode. The current handler and JIT cache-operation decode lack that privilege check. The measured CP15 accesses were privileged. [ARM interpreter](../src/cpu/arm920t.cpp), [JIT frontend](../src/cpu/translation_frontend.h), [ARM920T TRM, section 2.3](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb)

## ROM-free checks

- A section for domain 1 with `AP=3` kept translating to `0x30000000` with no fault both when the DACR set the domain to client and when it barred access. The result held on a TLB hit and after flushing the TLB.
- A supervisor `LDRT` read a word from an `AP=1` section and advanced the PC. An explicit user-mode read of the same mapping produced a section permission fault.
- Writing CP15 `c1` with `A=1` and then loading a word from an address offset by one byte produced the rotated value `0x78123456` and advanced the PC. The ARM920T specifies an alignment fault in this case, even with address translation disabled.

The probe recorded no misaligned single word transfers with `A=1` in the three runs. The ROM-free results establish the semantic differences on inputs that exercise them. Their impact on cartridge timing, video and audio remains open. A cache timing study needs separate instruction-cache, data-cache, write-buffer and external-memory evidence. [MMU translation](../src/memory/arm920t_mmu.h), [Common timing model](110_common_timing_model_and_calibration.md)


## Cartridge permission and alignment cross-check

A separate interpreter build observed permissions without changing the guest's execution. It retained the descriptor's domain when the existing walker filled a TLB slot and used it again on hits. The comparison applies the ARM920T domain policy, access-permission bits and CP15 control bits to the same mapping. It checks every `LDRT`, `STRT`, `LDRBT` and `STRBT` transfer. For ordinary translations it records every different permission decision and samples matching decisions at one per 257 calls. Guest fetches served directly from the emulator's instruction-page pointer do not perform a translation and are outside this count. Translation failures and emulator-specific mappings are counted apart from permission differences. The reconstructed Winx run with the observer enabled matched the previous interpreter run's SDRAM, active framebuffer and EEPROM byte for byte. [ARM920T TRM, tables 3-10 and 3-11](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb#page=73)

| Cartridge and step window | Ordinary data translation calls | Unprivileged transfers checked | Observed writes that should have faulted | Manager-domain transfers that need the permission bypass | Misaligned data accesses with `A=1` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Wade startup, 0-1.5 billion, ratio 6 | 615,183,688 | 2,334,401 | 0 | 33,409 | 0 |
| Wade desert intro, 5.0-6.2 billion, ratio 6 | 521,022,055 | 5,834,851 | 0 | 0 | 0 |
| Reconstructed Winx, 0-3.0 billion, ratio 3 | 1,270,745,347 | 1,611,870 | 44 | 41,074 | 0 |

The Wade run uses the fight input sequence through the Start press at step 5.0 billion. A framebuffer snapshot at 5.5 billion confirms the desert cutscene. The Winx run uses `tests/video_language_confirm.txt`. Each run starts with a zeroed 2-KB EEPROM. The alignment probe covers word and halfword data accesses passed to the bus and the original word addresses in the ARM paths that align them before calling the bus. No different domain or access-permission decision was observed in ordinary translations. The 44 divergences were all Winx writes using the unprivileged-transfer encoding.

The manager-domain accesses show why changing the transfer's privilege requires honoring DACR at the same time. Linux's kernel address-limit handling sets the kernel domain to Manager when it needs to access kernel buffers through the same user-copy instructions. Manager bypasses the page AP check. Adding the user check to the existing permission path independently would deny the otherwise allowed accesses counted in the Manager column. [Linux ARM user accessors](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/uaccess.h), [ARM920T TRM, table 3-10](https://documentation-service.arm.com/static/5e8e2a5b88295d1e18d381bb#page=73)

Winx's first divergent access was at `0xc001ce6c`. The guest instructions store saved ARM registers at offsets 12, 16, 20 and following in a user-space signal frame. The offsets and transfer sequence match Linux's `setup_sigcontext`. The data pages were Client-domain mappings with `AP=2`, which grants user read access. The emulator performed the stores using supervisor permissions. [Linux ARM signal handling](https://github.com/torvalds/linux/blob/v2.6.12/arch/arm/kernel/signal.c), [Signal-frame layout](https://github.com/torvalds/linux/blob/v2.6.12/include/asm-arm/sigcontext.h)

A second isolated interpreter run routed only the missed client-domain transfer-store faults to the existing data-abort handler and supplied the fault's domain in CP15. Linux took two page permission faults. It changed each mapping from `AP=2` to `AP=3` and retried the faulting store successfully in the same address space. The physical page was unchanged in both cases. Both runs reached Asteroids with identical final framebuffers and EEPROM contents. SDRAM differed. The test shows an actual difference in the user-stack update path. It leaves the timing and audiovisual impact of the larger CPU gaps open.
