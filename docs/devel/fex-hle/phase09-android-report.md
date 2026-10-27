# Phase 0.9 Pocket S execution report

Received 25 October 2026 (Europe/Helsinki). AYANEO Pocket S, Android 13/API33,
native ARM64, 4096-byte pages and multiblock translation, with the matching FEX
pin. All **148 checks pass**, matching the exact expected native identities:
all 129 accepted Phase0.8 checks, 16 common bridge checks and three native checks.
There are no missing, extra, duplicate or FAIL checks. Both bridge entry counters
report **27**, and the six-witness native ABI summary passes across those entries.

This accepts the scoped [Phase0.9 bounded bridge/callback increment](phase09.md)
on the tested Pocket S. Typed service return and exact stack cleanup, warm code
reuse, callback CPU/FPU/TLS restoration, malformed-frame rejection, service/depth
caps, shared budget exhaustion, callback fault attribution and latched stop all
pass. The native callback makes guest progress before stopping, leaves its caller
suspended, publishes the checked state after join and retains its request across
host dispatch and another controlled entry until reset. Every inherited CPU,
CPUID, precise-exit, division, fault/word-MMIO and allocator check remains clean.

Observed request-to-join times are **51 microseconds for the callback** and
**44 and 51 microseconds for the ordinary worker fixtures**. They include wrapper,
TLS and worker-exit work for constructed loops. A handful of observations does
not establish a worst-case bound, a regression against earlier samples or title
frame-time performance.

| Process snapshot | Virtual size (KiB) | RSS (KiB) |
| --- | ---: | ---: |
| Before arena | 16,468,856 | 126,316 |
| After arena and host controls | 19,092,380 | 128,864 |
| After CPU fixtures | 19,093,188 | 131,432 |

Process RSS increases **5,116 KiB (about 5.0 MiB)** across this run. Virtual size
increases 2,624,332 KiB, including the 2 GiB reserved arena and other process
mappings. These are process-wide snapshots, not peak or FEX-only memory. The
diagnostic explicitly touches its shadow-return cache; this report does not
isolate its cost or a memory regression. Completion does not independently prove
subsequent teardown or sustained behavior.

The accepted flags fixtures use IF=1. The pinned engine's setter forces that
userspace flag; this is not IF=0 restoration or virtual Xbox privilege acceptance.
This remains a fixed-frame, owned-memory diagnostic, not general kernel HLE.
REP/helpers/translation and arbitrary-fault bounds, general memory/bridge,
instruction admission/virtual privilege/timers, engine OOM/unload and 16 KiB hosts
remain open. No game execution, FPS, sound or thermal improvement is measured.

Next: [reference-backed instruction and virtual-state policy](phase10-plan.md).

Source SHA-256: `2e79b7cfc76f75c6eef3f0b66e5283a4e5f144aa04b161f4adabbe7aaca06286`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.9; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
Process memory before arena: virtual=16468856 KiB RSS=126316 KiB
Process memory after arena/host controls: virtual=19092380 KiB RSS=128864 KiB
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
Host arena: base=76b941d000 reserved=2147483648 bytes (virtual capacity, not RSS)
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
PASS default and foreign-owner contexts reject controlled APIs
PASS checkpoint contexts reject incompatible mode and caches
PASS zero admission budget returns before any guest instruction
PASS one instruction budget stops before next register change
PASS warm budget resume continues at exact guest PC
PASS guest HLT remains a guest exit distinct from budget stop
PASS direct nonterminating loop returns on admission budget
PASS conditional backedge returns with coherent guest state
PASS linked guest blocks share one admission budget
PASS pre-entry stop stays latched across repeated runs
PASS explicit stopped-thread reset permits execution again
PASS controlled divide fault can be repaired and resumed
PASS checkpoint preserves SIMD state before pending store
PASS checkpoint preserves x87 stack across stop and resume
PASS checkpoint preserves MMX data and EMMS tag state across resume
PASS native cold loop stops after progress and rejects concurrent run/reset
PASS native cold stop publishes state before thread reset
PASS native repeated loop stops after progress and rejects concurrent run/reset
PASS native repeated stop publishes state before thread reset
PASS controlled execution preserves all six native ABI witnesses
Observed cooperative stop and join: 44, 51 us (fixture-specific)
PASS bounded bridge typed service returns exact result and stdcall stack
PASS bounded bridge warm repeat retains one admission allowance
PASS bounded callback restores caller SIMD MMX x87 and control state
PASS bounded callback restores caller flags GPR and FS state
PASS bounded bridge rejects incomplete readable argument frame
PASS bounded bridge rejects overflowing IA32 argument frame
PASS bounded bridge rejects unowned caller return address
PASS bounded bridge rejects unexpected service stub PC
PASS bounded bridge rejects stop budget and wrong exception metadata
PASS bounded bridge enforces reverse callback depth cap
PASS bounded bridge enforces service count cap
PASS bounded callback exhausts shared allowance without caller completion
PASS nonterminating reverse callback returns on shared admission allowance
PASS callback stop remains latched across host dispatch and repeated entry
PASS callback fault retains exact attribution without caller completion
PASS bounded bridge rejects malformed callback completion frame
PASS native reverse callback stops after guest progress without caller completion
PASS native callback stop remains latched after returning to host dispatch
PASS bounded bridge entries preserve all six native ABI witnesses
Observed callback stop and join: 51 us (fixture-specific); bridge entries=27
Bounded bridge controlled entries: 27
PASS host allocations and threads still coexist after all CPU fixtures
PASS bounded arena still rejects exhaustion and services small allocations
Process memory after CPU fixtures: virtual=19093188 KiB RSS=131432 KiB
Result: CPU FIXTURES PASS; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
