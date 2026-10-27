# Phase 0.11.1 Pocket S execution report

Received 27 October 2026 (Europe/Helsinki). AYANEO Pocket S, Android 13/API33,
native ARM64, 4096-byte pages, multiblock translation and matching FEX pin.
All **193 expected native checks pass**, with no missing, extra, duplicate or
failed identities: all 177 inherited checks, 15 common XBE checks and the new
six-witness native ABI summary. The report completes **seven XBE entries**,
113 instruction-policy entries and 27 native bridge entries.

This accepts the scoped [generated XBE increment](phase111.md) on this Pocket S
run: exact low-address ownership, conflict preservation, host protections,
raw/BSS contents, the typed ordinal284 RtlFillMemory call, guest continuation,
warm execution, invalid/repeated dispatch rejection and execution limits.
All six native ABI witnesses pass across the seven new entries. Every inherited
CPU, division, fault/MMIO, identity, privileged-exit and callback check passes.
It does not establish arbitrary XBE, TLS, kernel or retail-game coverage.

## Mapping evidence

```text
stage=mapped image=10000 stack=20000 nrErr=0/0 hint=0/0 hintErr=0/0 protErr=0 old20000000Overlap=1 dalvik=1
```

The image at 0x10000 and stack at 0x20000 mapped directly with NOREPLACE; no hint
fallback or protection error occurred. The fallback remains cloud-tested and
was not exercised on this native run. The old 0x20000000 range overlaps a host
mapping with a Dalvik tag in this process. This supports the address-conflict
diagnosis and shows why the old placement is unsuitable for the observed layout.
The earlier report did not capture its exact stage/errno, so this observation
is not a reconstruction of that earlier failure.

## Measurements and limits

| Process snapshot | Virtual size (KiB) | RSS (KiB) |
| --- | ---: | ---: |
| Before arena | 16,408,784 | 130,896 |
| After arena and host controls | 19,032,308 | 133,288 |
| After CPU fixtures | 19,032,340 | 135,932 |

RSS increased 5,036 KiB (about 4.9 MiB). These process-wide snapshots are not
peak or FEX-only memory; the 2 GiB arena reserves virtual capacity. Worker
stop/join observations were 62/59 microseconds and callback stop/join was 69
microseconds. These fixture observations do not establish worst-case latency
or a performance regression against other runs.

The eight-instruction synthetic program is not a game workload. There is no
native renderer/audio or title FPS measurement in this report, and subsequent
teardown/sustained stability are not independently verified. Virtual IF/clocks,
broader memory/atomic/REP/helper/fault bounds, general OOM/unload and 16 KiB
runtime remain open. Next: [graphics/PCM boundary and integration plan](phase12-plan.md).

Source SHA256: `fe71fbc1236c1da69c1134001cfb8c9363804038310b03fa601253022fd3a7d8`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.11.1; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
Process memory before arena: virtual=16408784 KiB RSS=130896 KiB
Process memory after arena/host controls: virtual=19032308 KiB RSS=133288 KiB
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
Host arena: base=76bc837000 reserved=2147483648 bytes (virtual capacity, not RSS)
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
Observed cooperative stop and join: 62, 59 us (fixture-specific)
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
Observed callback stop and join: 69 us (fixture-specific); bridge entries=27
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
PASS generated XBE header and bounded image plan are admitted
PASS XBE truncation overflow and malformed headers fail before mapping
PASS XBE invalid section extents flags and overlap fail before mapping
PASS XBE unsupported TLS imports encodings and guest code are refused
PASS XBE low mapping conflict preserves existing host bytes
PASS XBE image and guarded stack reserve exact owned low addresses
PASS XBE image data stack and guards have expected host protections
PASS XBE raw sections load exactly and BSS starts zero
PASS XBE ordinal284 RtlFillMemory executes once with typed stdcall frame
PASS XBE guest continuation publishes fill result and exact completion state
PASS XBE warm repeat retains code and shared finite instruction allowance
PASS XBE invalid service frames and repeated dispatch have no host effects
PASS XBE zero allowance returns before image entry or service effects
PASS XBE finite allowance stops before kernel dispatch
PASS XBE latched stop returns before image entry or service effects
PASS controlled XBE entries preserve all six native ABI witnesses
Controlled XBE entries: 7
Controlled XBE detail: stage=mapped image=10000 stack=20000 nrErr=0/0 hint=0/0 hintErr=0/0 protErr=0 old20000000Overlap=1 dalvik=1
PASS host allocations and threads still coexist after all CPU fixtures
PASS bounded arena still rejects exhaustion and services small allocations
Process memory after CPU fixtures: virtual=19032340 KiB RSS=135932 KiB
Result: CPU FIXTURES PASS; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
