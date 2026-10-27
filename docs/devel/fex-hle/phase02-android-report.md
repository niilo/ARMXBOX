# Phase 0.2 Pocket S execution report

User report received 24 October 2026 (Europe/Helsinki). The report identifies an
AYANEO Pocket S running Android API 33 (Android 13), a native ARM64 process,
4096-byte host pages and the pinned FEX revision. All **21 checks pass**.

This validates the constructed generated-exception/guest-PC/resume cases and
all six native boundary witnesses on this device, including FPSR, which the
cloud simulator could not model. It retains the original arithmetic, x87,
MMX/SSE, FS base, callback-state, alias and invalidation coverage. It does not
establish full instruction coverage, arbitrary Android signal/fault recovery,
MMIO handling, internal-call stack alignment or title performance.

There are no timing, FPS, power or thermal measurements. The independently
confirmed unsigned DIV zero/quotient-overflow #DE gap remains unresolved;
these cases are outside the 21-check suite. Raw memory-fault/MMIO recovery,
full bridge/memory semantics, 16 KiB hosts and Xbox CPU/privilege policy also
remain open. Broad HLE is still gated on the remaining Phase 0 contracts.

Source report SHA-256: `9de6a4853964f889b3fe03817b22fa2b234d4706ec399cfbefb266ca0c1b747c`.

```text
Device: AYANEO Pocket S
Android API: 33
ARMXBOX FEX Phase 0.2; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
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
PASS Android x18 unchanged across all engine entries/exits
PASS native SP restored and 16-byte aligned at return boundary
PASS AAPCS64 x19-x29 preserved
PASS AAPCS64 d8-d15 low halves preserved
PASS native FPCR restored after guest execution
PASS native FPSR restored after guest execution
Result: CPU FIXTURES PASS; raw-memory/MMIO, full bridge, 16K, Xbox CPU policy and performance gates remain open
```
