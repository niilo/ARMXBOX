# Phase 0.10.1: recover progress after a native probe crash

Update: the recovered device journal located a code-buffer protection failure.
See [Phase0.10.2](phase102.md) for evidence and the pending native rerun.
The description below records the initial recovery build.

The user reports that phase0.10/code11 closes almost immediately after tapping
Run CPU fixtures on Pocket S. No completion report or device crash trace is
available yet. **Phase0.10 is not natively accepted**, despite passing its cloud
fixtures. The cause and faulting device stage are not established.

This revision adds persistent diagnostic progress, not an asserted engine fix.
It retains all CPU cases, the eight-patch engine state, the selected policy,
AVX128 tests and the 176-check native expectation. It does not skip a crashing
path or catch a native fault and continue through an uncertain engine state.

## Recoverable report

The Java worker writes device/build identity and a library-load marker to an
app-private journal before loading the native library, then records the boundary
before JNI. JNI opens that existing file before FEX initialization. Normal host
logging synchronously appends bounded progress, including policy factory guards,
context feature choices, core/thread setup and each instruction-policy entry and
return. The borrowed descriptor is disabled before closing it. No new signal
handler is installed and no journal work is added to an existing signal handler.

The native journal is limited to 128 KiB with a truncation marker; Java recovery
reads at most 132 KiB, including its startup prelude. The current complete cloud
run produces about 27 KiB. Progress writes add diagnostic I/O overhead and are
not performance measurements.

If the process dies before Java saves the final report and completion marker,
reopening the app displays the incomplete journal with Share report. It is
labelled incomplete, not proof of a particular crash signal: force-stop and OS
termination can also interrupt it. A new run overwrites the journal, so share the
recovered result first. A successful run stores the normal final report. Existing
force-stop/relaunch ownership remains one run per process. No storage/network
permission or external file access is needed.

## Verification and limits

A separate host test kills its child immediately after a journal write and
verifies the boundary survives without a crash handler. It also checks borrowed
FD detachment and the output limit. The existing VIXL CPU fixtures remain
required; those do not establish native Android execution or UI recovery.

An additional private Linux ARM64 cross-build was executed under QEMU user-mode
6.2, beyond VIXL. It crashed at the inherited early SIMD fixture. Investigation
found stale code-cache lookup data and a branch into retired protected code.
A standalone mmap/write/MADV_DONTNEED/read control returned success but retained
123 instead of the required zero. This emulator limitation invalidates that run
as a reproduction of the Pocket S regression. No emulator-specific workaround
is shipped, and no Android ABI or crash-fix success is inferred from it.

The diagnostic is **phase0.10.1/code12**. If it closes, reopen it and Share report
before another run. The recovered stage is needed to locate the device failure;
a complete native result would still require all 176 PASS checks and 113 new
instruction-policy entries. Controlled XBE integration stays gated while this
native regression is unresolved.
