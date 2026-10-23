# Phase 0.2: generated exceptions and native ABI boundaries

Phase0.1 passed all ten fixtures in the user Android report. Phase0.2 keeps those
fixtures and the same FEX/dependency pins, adding controlled generated-exception
exits and explicit native ABI witnesses. It remains a separate game-free app.
No game support, title speedup or completed Phase 0 gate is claimed.

## Runtime changes

- Bind the pinned thread's generated SIGILL/SIGTRAP/SIGSEGV dispatcher pointers
  to FEX's `ThreadStopHandlerSpillSRA`. The engine's `Break` path first resets its
  temporary stack and publishes synchronous exception metadata; the stop handler
  then spills guest registers and restores the native callee-saved frame. This
  replaces the fixture-only `EnableExitOnHLT()` shortcut with an explicit stop
  contract. No upstream API or Linux signal frontend is invented or imported.
- Verify HLT metadata, UD2 faulting PC/GPR preservation, explicit resume after
  UD2, INT3's next-PC semantics, and an unsupported INT's exact GP selector error.
  These are five new checks, for **15 cloud CPU checks**. The GP case is not a
  demonstration of running Xbox privileged code at CPL0; that contract is open.
- Wrap each native engine invocation with host FPCR/FPSR save/restore. Inspection
  of the pinned dispatcher shows its callee-saved register helpers do not own
  those system registers. This is an adapter boundary fix, not an upstream FEX
  regression claim or a change to the production emulator.
- Exercise six native ABI checks on every engine entry/exit, including the
  reverse callback and exception/resume paths: x18 unchanged; SP restored and
  aligned at the return boundary; x19-x29 preserved; d8-d15 low halves preserved;
  FPCR restored; FPSR restored. The witness never writes native x18 and keeps
  x29's existing frame-chain value. It temporarily seeds other saved registers
  and a nondefault FP environment, then restores its caller's original state.
- Log each check immediately, identify the probe as phase0.2, and include device
  manufacturer/model and Android API in the app's shared report. No serial or
  unique device identifier is collected. Replace the misleading generic device
  gate footer with the actual remaining contract categories.

The result is **21 expected PASS lines on native Android**. Cloud simulation
runs 15 CPU checks and explicitly marks the six native checks unverified.
ABI witnesses cover these entry/return boundaries; they do not prove stack
alignment at every internal generated host call or general Android signal/TLS
coexistence. The assembly wrapper is a test instrument, not a performance path.

## Witness validation and cloud limits

The cloud build cross-assembles the actual ARM64 witness and executes it under
VIXL with a valid callback and four intentionally corrupted callbacks (x18, x19,
d8, FPCR). All five controls pass, proving that the modeled witness checks reject
their matching corruption rather than always returning success. Deliberate x18
corruption exists only in the host simulator control, never in the Android app.
The build extractor accepts only relocation-free AArch64 ELF64 text from our
compiler output. These controls are distinct from executing Android/FEX itself.

The pinned VIXL does not implement FPSR MRS/MSR. The control test therefore does
not score the FPSR result and reports that missing model explicitly. Actual
native FPSR preservation remains a device check. GPR/vector/control witnesses
against the real engine also remain device checks, despite compilation and
witness-control coverage.

Cloud validation: 15 FEX/VIXL fixture checks; five witness controls; Android ARM64
full-object engine link and diagnostic compile; separate APK packaging,
external-dependency closure, signature and alignment checks. No repeat of the
unchanged production runtime's numerical suites is required for this isolated
probe. Existing project release-versioning checks remain applicable.

## Device test and next gate

Install `armxbox-fex-phase0.2-debug.apk`, open **ARMXBOX FEX Probe**, run the
fixtures, then **Share report**. If Android rejects an update because the fresh
CI debug key differs, uninstall the old probe first; its package is separate from
ARMXBOX and contains no game saves. Force-stop/relaunch to repeat.

This increment handles engine-generated architectural exceptions only. An
unmapped/protected guest load/store still faults in the Android process; it is
not handled by rebinding generated-exception pointers. Raw memory-fault recovery,
guest-PC reconstruction for such faults, MMIO exits, 16 KiB host pages, the full
bridge/memory contract and Xbox CPUID/privilege policy remain open. The pinned
DIV/IDIV paths also need a dedicated exception-fidelity audit; do not infer that
UD2/INT3/GP coverage establishes divide-error behavior. No broad HLE work should
start on the strength of these limited checks alone.
