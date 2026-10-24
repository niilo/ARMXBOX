# Phase 0.4 Pocket S report: raw-fault gate failed

Received 24 October 2026 (Europe/Helsinki). AYANEO Pocket S, Android 13/API33,
native ARM64, 4096-byte pages, multiblock translation. **40 PASS, 10 FAIL**.
All 39 inherited CPU/division/ABI checks pass, including all 18 divide matrix
summaries. This establishes the phase0.3 divide increment on the device in the
cumulative phase0.4 app. There are no timing or FPS measurements.

All ten new raw-state/MMIO/divisor-precedence checks fail; restoration of the
previous SIGSEGV registration passes. The final raw detail records an owned
fault event with Unsupported classification and a recovered block-entry PC,
`0x10000000`, instead of the memory instruction at `0x10000008`. Bus service was
correctly refused for that unsupported event. The report only retains detail
for its last ordinary memory case, so it cannot independently identify the
specific reason for each of the other failures.

The report does not establish correct general fault recovery or MMIO. The
[phase0.4.1 correction](phase041.md) reproduces missing moffs metadata and invalid
pre-access flags in the cloud and retains every failing native acceptance check.
The raw-fault gate remains open until a corrected native report passes.

Source SHA-256: `595d11d7c1e3a7f218fb6878d13fa3d2e1a361c8180fd3feac5ac90266fcc7e1`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.4; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
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
FAIL raw protected read: exact guest PC/registers/flags
FAIL typed MMIO read: one service and resume
FAIL raw protected write: exact guest PC/registers/flags
FAIL typed MMIO write: one service and resume
FAIL raw unmapped read: exact guest PC/registers/flags
FAIL typed unmapped MMIO read: service and resume
FAIL raw unmapped write: exact guest PC/registers/flags
FAIL typed unmapped MMIO write: service and resume
FAIL DIV memory fault precedes arithmetic error
FAIL IDIV memory fault precedes arithmetic error
PASS original SIGSEGV registration restored
Raw-fault detail: raw case=3 seen=1 kind=1 PC=10000000 expected=10000008
PASS Android x18 unchanged across all engine entries/exits
PASS native SP restored and 16-byte aligned at return boundary
PASS AAPCS64 x19-x29 preserved
PASS AAPCS64 d8-d15 low halves preserved
PASS native FPCR restored after guest execution
PASS native FPSR restored after guest execution
Result: FAIL; general MMIO/unaligned/atomic accesses, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
