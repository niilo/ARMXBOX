# Phase 0.10.2 Pocket S execution report

AYANEO Pocket S, Android 13/API33, native ARM64, 4096-byte pages,
multiblock translation and matching engine pin. All **177 expected native
checks pass**, with no missing, extra, duplicate or failed identities. The
113 instruction-policy entries and 27 bounded bridge entries complete, including
the six native ABI witnesses across policy entries. The final result is CPU
FIXTURES PASS and the user confirms the app did not crash.

This accepts the scoped [Phase0.10/0.10.2 instruction-policy and allocator
increment](phase102.md) on this run. The previously failing policy stage now
completes, including newer-instruction rejection, default-context isolation,
AVX128 behavior, pre-memory rejection, exact state, budgets and latched stops.
Every inherited CPU, division, fault/MMIO, CPUID, privileged-exit, callback and
allocator check passes, including the new OS mapping failure/reuse control.

The crash regression is cleared for this diagnostic run. The earlier journal
lacked errno and a fault trace, so this does not identify which allocation
condition caused the original failure. The changes combine reduced cold-test
buffer growth and corrected error handling; their individual device effects
were not isolated. General OOM recovery and sustained stability remain open.

| Process snapshot | Virtual size (KiB) | RSS (KiB) |
| --- | ---: | ---: |
| Before arena | 16,406,376 | 123,884 |
| After arena and host controls | 19,029,900 | 126,548 |
| After CPU fixtures | 19,030,676 | 129,392 |

Process RSS increased 5,508 KiB (about 5.4 MiB). These are snapshots, not peak
or FEX-only memory. The 2 GiB arena is reserved virtual capacity. Worker stop/join
observations were 65/80 microseconds; the callback observation was 46 microseconds.
These fixture-specific timings do not measure game speed or a worst-case bound.

Next: a generated, strictly admitted XBE with a minimal typed import, owned
low-address mapping, finite execution allowance and controlled completion.
Virtual IF/timers, broader ISA/REP/helper/fault bounds, arbitrary XBE/retail
admission, full HLE and 16 KiB runtime remain open. No gameplay FPS/audio/thermal
improvement is measured. Completion does not independently verify later teardown.

Source SHA256: `b40fc8e4060dc408d24642f56febe27cdd1ce6bf2d6fe3fa03c036c801b85e0c`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.10.2; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
Process memory before arena: virtual=16406376 KiB RSS=123884 KiB
Process memory after arena/host controls: virtual=19029900 KiB RSS=126548 KiB
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
PASS bounded arena propagates failed OS mapping and remains reusable
Host arena: base=76bd029000 reserved=2147483648 bytes (virtual capacity, not RSS)
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
Observed cooperative stop and join: 65, 80 us (fixture-specific)
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
Observed callback stop and join: 46 us (fixture-specific); bridge entries=27
Bounded bridge controlled entries: 27
PASS instruction policy rejects unknown policy value
PASS instruction policy rejects 64-bit guest mode independently of identity
PASS instruction policy rejects shared disk cache independently of identity
PASS instruction policy rejects experimental cache independently of identity
PASS instruction policy rejects cache validation independently of identity
PASS instruction policy rechecks incompatible cache at core initialization
PASS reference policy retains SSE1 XORPS
PASS reference policy retains MMX PXOR
PASS reference policy retains x87 FLD1
PASS reference policy retains observed SSE2 PXOR compatibility ambiguity
PASS reference policy rejects captured SSE3 HADDPS with exact state
PASS reference policy rejects captured SSSE3 PSHUFB with exact state
PASS reference policy rejects captured SSE4.1 PTEST with exact state
PASS reference policy rejects captured SSE4.2 PCMPGTQ with exact state
PASS reference policy rejects captured AVX VZEROUPPER with exact state
PASS reference policy rejects captured RDTSCP with exact state
PASS reference policy rejects captured XGETBV with exact state
PASS reference policy retains precise explicit UD2 rejection
PASS policy SIMD rejection precedes inaccessible memory operand
PASS default contexts retain modern operations and unsupported AVX behavior
PASS instruction policy and CPUID identity remain independent
PASS policy code invalidation observes replacement with allowed instruction
PASS admission budget boundary precedes policy rejection
PASS latched stop boundary precedes policy rejection
PASS policy rejection preserves SIMD MMX x87 status and floating controls
PASS reference policy rejects precisely without instruction checkpoints
PASS AVX128 VZERO dispatch isolates rejection policy from default contexts
PASS instruction policy entries preserve all six native ABI witnesses
Instruction policy entries: 113
PASS host allocations and threads still coexist after all CPU fixtures
PASS bounded arena still rejects exhaustion and services small allocations
Process memory after CPU fixtures: virtual=19030676 KiB RSS=129392 KiB
Result: CPU FIXTURES PASS; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
