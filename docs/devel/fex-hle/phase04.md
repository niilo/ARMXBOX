# Phase 0.4: controlled native memory faults and word MMIO

This cumulative probe retains the phase0.3 divide fix and adds protected/unmapped
memory tests to the Android ARM64 app. It does not run games. Host policy tests
and successful compilation are not proof of Android signal delivery/recovery;
the eleven new native checks explicitly need a device report.

## Scope and ownership

A short-lived SIGSEGV registration is active only around the controlled tests.
Thread-local lock-free activation identifies the executing FEX context. The
handler first checks a four-byte access inside a page owned by the probe, positive
MAPERR/ACCERR status and the captured x28 frame pointer. It then validates the
pinned live code header/tail and host PC range, aligned/bounded return stack and
an anchor captured by the calling native frame. Unrelated or unsafe contexts are
forwarded to the previous disposition rather than absorbed as guest faults.

The code uses the actual pinned FEX CPUBackend block layouts, not a copied
layout or a guessed guest base. Handler helper paths were inspected to perform
bounded metadata reads without allocation or locks. Device callbacks, logging,
code invalidation and mapping operations run outside the handled signal path.
The thread's TLS is touched before handler installation.

Only aligned scalar word LDAR/STLR/LDR/STR/LDUR/STUR operations without writeback
are recognized. Their effective addresses, direction and store value are checked
against the known guest fixture. Unknown instruction forms or guest-PC recovery
mismatches return an unsupported probe event, never a successful bus access.

Kernel return resumes at FEX's existing spill-and-stop path with its saved entry
SP. Captured guest registers and PSTATE are restored by signal return, then
spilled by the engine before returning through the frontend FP-state guard.
No longjmp crosses C++ or simulator frames. Raw-fault metadata is distinct from
engine-generated exceptions.

## Device cases

Eleven new native checks cover:

- Protected word read and write: exact guest PC after a real ADD prefix,
  unchanged destination/data registers, preserved arithmetic flags and native
  fault metadata.
- Unmapped word read and write with the same state checks and MAPERR status.
- A typed mock MMIO service followed by guest resume for each of those four
  cases. Service occurs outside the signal handler. Transactions reject invalid
  descriptors and repeated consumption; mock callbacks record actual service
  counts, addresses and data. Only the known scalar MOV fixture is emulated.
- Protected DIV and IDIV memory operands: the memory fault must occur before an
  arithmetic exception consumes the unavailable divisor.
- Restoration of the original SIGSEGV registration after the tests.

Combined with the previous CPU/matrix and six ABI witnesses, a healthy native
phase0.4 report has **50 PASS lines**. The native ABI witnesses also cover entries
and exits through these new signal paths. The test app is still one-shot per
process. If a fault is outside the strictly supported scope, normal Android
crash handling remains applicable; capture `adb logcat -s ARMXBOX-FEX` if a report
cannot be shared.

## Cloud validation and limits

The pure policy/transaction suite passes **54 checks**. It rejects user signals,
wrong addresses/frames, out-of-block PCs, unsafe SPs, bad instruction forms and
replayed bus transfers. Seven decoder cases are checked against observed ARM
load/store effects under VIXL rather than only against expected bit constants.
This validates policy and decoding, not Android ucontext edits or ART coexistence.

The phase0.3 suites remain in CI: 133,400 generated-code division cases in two
translation modes, 31,296 native x86 reference cases, original CPU fixtures and
five ABI witness controls. The native x86 flags oracle explicitly disables the
compiler red zone because its inline assembly uses balanced push/pop operations.
The Android full-object link, packaging, dependency closure, signature and APK/ELF
alignment checks are also retained.

General instruction decoding, unaligned/atomic/vector MMIO, concurrent guests,
arbitrary signal/TLS coexistence, concurrent signal-handler registration, complete
bridge/memory semantics, 16 KiB hosts, Xbox CPU/privilege policy and bounded
execution remain open. The handler is a scoped diagnostic experiment, not a
production-wide Android crash handler. No HLE, retail compatibility or FPS gain
is inferred from these checks. In particular, the bounded host-allocator/normal
Android-library mapping contract still needs audit before broad integration.
