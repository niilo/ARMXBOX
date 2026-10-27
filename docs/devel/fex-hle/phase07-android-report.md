# Phase 0.7 Pocket S execution report

Received 25 October 2026 (Europe/Helsinki). AYANEO Pocket S, Android 13/API33,
native ARM64, 4096-byte pages, multiblock translation. All **109 checks pass**.
Check identities match all 88 accepted native Phase0.6 checks plus the 20 new
common checks and one native ABI summary, without missing or duplicate checks.
The engine pin matches and there are no FAIL lines.

The EFLAGS correction passes its 256 API round trips, including arithmetic/DF
combinations, architectural control bits and supplied reserved high bits. All
19 controlled privileged-exit cases pass their arithmetic and cold/warm variants:
152 new guest entries preserve exact PC, exception metadata, all eight GPRs,
flags and following memory/GPR sentinels. INSB/OUTSB rejection precedes access to
the inaccessible operand. The new six-witness native ABI summary passes.
Inherited CPUID/context isolation, divide, fault/word-MMIO, ABI and bounded
allocator/coexistence checks remain clean.

This accepts the scoped Phase0.7 correction and precise controlled-rejection
increment on the tested Pocket S. It does not establish physical Xbox ring-0
semantics, instruction admission, virtual interrupts/timers or cancellation.

| Process snapshot | Virtual size (KiB) | RSS (KiB) |
| --- | ---: | ---: |
| Before arena | 16,445,244 | 125,764 |
| After arena and host controls | 19,068,768 | 128,064 |
| After CPU fixtures | 19,069,576 | 130,284 |

RSS rises **4,520 KiB (about 4.4 MiB)** across this run. Virtual size rises
2,624,332 KiB, including the 2 GiB arena reservation and other process mappings.
These are process-wide snapshots, not peak/FEX-only memory or a 2 GiB physical
commitment. They do not isolate a memory regression or establish sustained
behavior. No FPS, throughput, sound, thermal or stop-latency measurements are
included. Report completion does not independently verify later process teardown.

See [implementation and limits](phase07.md), the [bounded-execution plan](phase08-plan.md)
and [remaining embedding gates](next-gates.md). Broad HLE/game execution has not
started, and full bridge/memory semantics, general OOM/unload and 16 KiB hosts
remain open.

Source SHA-256: `da2ddf17e38e8b9b263d81191935319073b8b1219df242cb5ecfa5139c5e8b9e`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.7; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
Process memory before arena: virtual=16445244 KiB RSS=125764 KiB
Process memory after arena/host controls: virtual=19068768 KiB RSS=128064 KiB
PASS arena rejects incompatible host pages without changing hooks
PASS arena rejects undersized reservation without changing hooks
PASS arena rejects unaligned reservation without changing hooks
PASS arena rejects overflow reservation without changing hooks
PASS bounded high-VA arena initializes
PASS bounded arena refuses repeated initialization
PASS pre-existing host mapping remains intact
PASS host mappings survive concurrent allocator initialization
PASS ordinary host malloc coexists with bounded arena
PASS ordinary host mmap coexists with bounded arena
PASS host threads and concurrent bounded mappings coexist
PASS fresh host library maps and executes after initialization
PASS bounded mapping supports partial free and exact reuse
PASS host hooks reject fixed mapping and unmap outside owned arena
PASS host hooks reject low guest mapping and unmap
PASS bounded arena rejects exhaustion and invalid lengths
PASS bounded file mapping shares backing with ordinary host mapping
Host arena: base=76baa27000 reserved=2147483648 bytes (virtual capacity, not RSS)
Translation: multiblock
PASS IA32 arithmetic/carry and HLT exit
PASS SSE2 packed integer
PASS MMX and EMMS
PASS x87 extended arithmetic/store
PASS FS cached base load
PASS run/resume retains GPR state
PASS HLT exit carries guest PC and generated exception metadata
PASS UD2 controlled exit preserves faulting PC and GPR
PASS resume after handled UD2 reaches next fixture entry
PASS INT3 controlled exit reports next PC without executing it
PASS unsupported INT reports GP with exact selector error
PASS callback restores FPU/SIMD/control/flags state
PASS exit-based typed stdcall/reverse callback/stack cleanup
PASS generated direct accesses share backing aliases
PASS explicit code invalidation observes modified instruction
PASS DIV8 reference matrix: 5218 cases (2521 valid, 2697 faults)
PASS DIV8 memory divisor matrix: 5216 cases
PASS DIV8 aliased dividend/divisor: 1024 cases
PASS IDIV8 reference matrix: 5218 cases (2515 valid, 2703 faults)
PASS IDIV8 memory divisor matrix: 5216 cases
PASS IDIV8 aliased dividend/divisor: 1024 cases
PASS DIV16 reference matrix: 5218 cases (2413 valid, 2805 faults)
PASS DIV16 memory divisor matrix: 5216 cases
PASS DIV16 aliased dividend/divisor: 512 cases
PASS IDIV16 reference matrix: 5218 cases (1925 valid, 3293 faults)
PASS IDIV16 memory divisor matrix: 5216 cases
PASS IDIV16 aliased dividend/divisor: 512 cases
PASS DIV32 reference matrix: 5218 cases (2501 valid, 2717 faults)
PASS DIV32 memory divisor matrix: 5216 cases
PASS DIV32 aliased dividend/divisor: 512 cases
PASS IDIV32 reference matrix: 5218 cases (2002 valid, 3216 faults)
PASS IDIV32 memory divisor matrix: 5216 cases
PASS IDIV32 aliased dividend/divisor: 512 cases
PASS moffs load has exact fault-PC metadata
PASS moffs store has exact fault-PC metadata
PASS ModRM load has exact fault-PC metadata
PASS ModRM store has exact fault-PC metadata
PASS DIV memory has exact fault-PC metadata
PASS IDIV memory has exact fault-PC metadata
PASS raw protected read: exact guest PC/registers/flags
PASS typed MMIO read: one service and resume
PASS raw protected write: exact guest PC/registers/flags
PASS typed MMIO write: one service and resume
PASS raw unmapped read: exact guest PC/registers/flags
PASS typed unmapped MMIO read: service and resume
PASS raw unmapped write: exact guest PC/registers/flags
PASS typed unmapped MMIO write: service and resume
PASS DIV memory fault precedes arithmetic error
PASS IDIV memory fault precedes arithmetic error
PASS original SIGSEGV registration restored
PASS Android x18 unchanged across all engine entries/exits
PASS native SP restored and 16-byte aligned at return boundary
PASS AAPCS64 x19-x29 preserved
PASS AAPCS64 d8-d15 low halves preserved
PASS native FPCR restored after guest execution
PASS native FPSR restored after guest execution
PASS CPU identity rejects unknown profile
PASS Xbox identity rejects 64-bit guest mode
PASS Xbox identity rejects shared disk cache
PASS Xbox identity rejects experimental code cache
PASS Xbox identity rejects code-cache validation
PASS Xbox CPUID API matches 54 reference cases twice
PASS Xbox CPUID name API is independent of host CPU number
PASS generated immediate CPUID matches 54 reference cases
PASS generated dynamic CPUID matches 54 reference cases
PASS CPUID preserves flags and unrelated GPRs at exact HLT exit
PASS default Xbox default contexts isolate identical guest addresses
PASS default FEX CPU identity retains modern engine capabilities
PASS CPU identity guest entries preserve all six native ABI witnesses
PASS CLI controlled GP preserves state and stops before sentinel
PASS STI controlled GP preserves state and stops before sentinel
PASS MOV from CR0 controlled GP preserves state
PASS MOV to CR0 controlled GP preserves state
PASS MOV from DR0 controlled GP preserves state
PASS MOV to DR0 controlled GP preserves state
PASS RDMSR controlled GP preserves state
PASS WRMSR controlled GP preserves state
PASS RDPMC controlled GP preserves state
PASS IN immediate controlled GP preserves state
PASS OUT immediate controlled GP preserves state
PASS IN DX controlled GP preserves state
PASS OUT DX controlled GP preserves state
PASS INSB controlled GP precedes inaccessible destination
PASS OUTSB controlled GP precedes inaccessible source
PASS SYSENTER controlled UD preserves state
PASS SYSEXIT controlled UD preserves state
PASS UD2 preserves arithmetic-produced flags and stops before sentinel
PASS HLT preserves arithmetic-produced flags and stops before sentinel
PASS privileged exit entries preserve all six native ABI witnesses
PASS compacted EFLAGS excludes private NZCV and reserved high bits
PASS host allocations and threads still coexist after all CPU fixtures
PASS bounded arena still rejects exhaustion and services small allocations
Process memory after CPU fixtures: virtual=19069576 KiB RSS=130284 KiB
Result: CPU FIXTURES PASS; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
