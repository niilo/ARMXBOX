# Phase 0.4.1: precise memory-fault state

The [phase0.4 Pocket S report](phase04-android-report.md) passes all 39 inherited
CPU/division/ABI checks but fails ten raw-fault/MMIO checks. The diagnostic caught
unsupported events and declined bus service; it did not demonstrate accepted
recovery. This correction remains a game-free diagnostic, not a playable backend.

## Reproduced causes and correction

Pinned FEX's `CanHaveSideEffects` did not recognize `FLAGS_MEM_OFFSET` operands
used by absolute-address accumulator MOV instructions (moffs). Consequently, the
compact native-PC/guest-PC table omitted their boundary. An actual native fault
recovered the containing block's entry instead of the memory instruction.

A new generated-code test reproduces this without inaccessible memory: it executes
mapped moffs read/write, ModRM read/write and memory DIV/IDIV fixtures and checks
the real live block's compact metadata through `RestoreRIPFromHostPC`. Before the
fix, both moffs cases fail while the other four cases pass.

A host-only VIXL observer also samples actual simulated registers/PSTATE immediately
before those six memory accesses. Before the fix, all six fail the pre-fault
arithmetic-flags expectation. For MOV, flags can be optimized away because a later
ADD overwrites them; for the DIV fixtures, the observed carry convention disagrees
with the ABI spill/restore path. These are cloud observations of the generated
instructions, not guesses based solely on the single native error detail.

Reviewed patch `0003-precise-memory-fault-state.diff`:

- Recognizes moffs operands as potentially faulting.
- Canonicalizes carry and flushes deferred flags/static state before potentially
  faulting instructions, retaining the existing partial register-cache flush.
- Treats guest-opcode boundaries as flag observations in dead-flag elimination,
  preserving state even when the successful path later overwrites it.

The patch does not enable debugger symbol generation or force one guest
instruction per block. It adds work to some normal memory paths, so its cost must
be measured and refined before claiming a runtime performance benefit. Precise
fault state is a correctness requirement. No FPS gain is measured here.

Patch preparation now composes overlapping reviewed patches in a temporary Git
index and recognizes only exact reviewed prefixes. It preserves the real index,
rejects extra edits and validates applied source whitespace. The engine pin and
dependencies are unchanged.

## Verification and next device check

The corrected cloud run passes all **45 checks** in both translation modes:
the retained 33 CPU/matrix summaries, six actual-engine PC-metadata cases and six
pre-access flag observations. The six flag observations use VIXL's live simulator
and decoder, not a substitute interpreter; they remain host-only evidence.
The 54 ownership/decoder/transaction controls, five ABI controls and 31,296 native
x86 reference cases are retained, along with full Android compilation/linking
and APK verification.

Native phase0.4.1 expects **56 PASS lines**: 39 inherited CPU/ABI checks, six new
PC-metadata checks and the original eleven raw-fault/MMIO/restoration checks.
Every original native check is retained. Failure detail now records each failed
raw case, including event/state PC, flags, registers and trap/error metadata,
instead of overwriting all detail with the last case.

Install the separate FEX Probe app, run fixtures and share its report. The
reported native failures are not considered fixed until the corrected Android
run passes; cloud metadata/flag observations cannot prove kernel ucontext
recovery. Broad HLE remains gated on the other [embedding contracts](next-gates.md).
