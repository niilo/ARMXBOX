# Phase 0.5 Pocket S execution report

Received 24 October 2026 (Europe/Helsinki). AYANEO Pocket S, Android 13/API33,
native ARM64, 4096-byte pages, multiblock translation. All **75 checks pass**,
with no FAIL lines. Check identities match the accepted 56 native Phase0.4.1
checks plus the 19 new allocator checks, without missing or duplicate checks.
The allocator checks match the verified CI report; six host-only simulated flag
witnesses are excluded from native expectations. The engine pin matches.

This accepts the scoped bounded-arena and host-coexistence increment on this
device. All 19 new checks pass, including concurrent host allocations, ordinary
malloc/mmap and thread stacks, fresh library loading, partial-free/reuse,
request-level exhaustion, out-of-arena/low-guest rejection and shared file
backing. Host allocations and bounded requests still work after CPU execution.
All inherited arithmetic, divide, exact fault-state, word-MMIO and native ABI
checks remain clean.

| Process snapshot | Virtual size (KiB) | RSS (KiB) |
| --- | ---: | ---: |
| Before arena | 16,432,944 | 127,992 |
| After arena and host controls | 19,057,244 | 130,196 |
| After CPU fixtures | 19,057,244 | 131,680 |

RSS increases by **3,688 KiB (about 3.6 MiB)** across this run. Process virtual
size increases by 2,624,300 KiB (about 2.50 GiB), including the 2 GiB reserved
arena and other process mappings. These are process-wide snapshots, not a
measurement of FEX-only memory or peak usage. The reservation is virtual
capacity, not a 2 GiB physical-memory commitment. Later workloads can commit
more memory; these snapshots do not establish sustained memory behavior.

This report does not measure FPS, CPU throughput, audio, temperature, stop
latency, general recoverable engine OOM, full library unload, arbitrary guest
memory layouts, concurrent guests, or 16 KiB hosts. It reports successful
fixture completion, not independent evidence of crash-free subsequent process
teardown. Full CPU/bridge/memory contracts remain open before broad HLE.

See [implementation and limits](phase05.md), [remaining gates](next-gates.md)
and the [next CPU-policy increment](phase06-plan.md).

Source SHA-256: `20c4d414fe7e4e2776db53fb655c841ae6f914115272ae69986324d3d1c97202`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.5; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
Process memory before arena: virtual=16432944 KiB RSS=127992 KiB
Process memory after arena/host controls: virtual=19057244 KiB RSS=130196 KiB
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
Host arena: base=76bb840000 reserved=2147483648 bytes (virtual capacity, not RSS)
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
PASS host allocations and threads still coexist after all CPU fixtures
PASS bounded arena still rejects exhaustion and services small allocations
Process memory after CPU fixtures: virtual=19057244 KiB RSS=131680 KiB
Result: CPU FIXTURES PASS; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
