# Phase 0.9: bounded typed bridge and reverse callback diagnostic

This increment composes the [accepted Phase0.8 execution boundary](phase08-android-report.md)
with a constructed exit-based service/callback fixture. It adds no engine patch,
kernel services, game loading or graphics/audio integration. It is a diagnostic,
not a general HLE bridge or a measured performance improvement.

## Contract

The host dispatch runs only after controlled guest execution has returned. A
service requires `GuestExit`, the exact owned stub PC, generated SIGSEGV/#GP
metadata with zero error, a complete readable three-word IA32 stack frame and
the owned caller return PC. Budget/stop exits, other stubs/faults, incomplete or
overflowing frames, excess services and excess callback depth are rejected
before the host-body counter changes. The service and callback values are typed
32-bit integers, with exact stdcall cleanup; there is no arbitrary host pointer
call or generic guest-pointer marshaller.

One host-owned admission allowance is passed through caller, callback and caller
resume. It is reduced after every engine return, including generated faults.
The normal fixture starts with 64 admissions: caller uses four, callback uses
14 (including its HLT completion), caller resume uses one, leaving 45. A second
normal run reuses translated code. The service count cap is one and callback
depth cap is one; these are explicit fixture limits, not demonstrated general
nested kernel callbacks. The host body is fixed, nonblocking arithmetic.

The callback runs through a new controlled entry on the same stopped thread.
The suspended architectural snapshot restores GPRs, canonical flags, SIMD/MMX/x87
state (including x87 status/TOP), floating controls and segment/TLS state. Engine-owned lookup pointers,
inline headers, deferred-signal counters, native return-stack location, fault
metadata and atomic execution-control fields are not restored from a snapshot.
The next controlled entry clears previous generated fault metadata. A callback
budget/stop/fault leaves callback attribution and state in place; it does not
complete or resume the caller. Explicit idle reset is required after a stop.

Pinned FEX's `BranchOps.cpp` caches guest/host return PC pairs in a per-thread
shadow stack; `Dispatcher.cpp` spills/loads its cursor in `CPUState::callret_sp`.
Code-cache clearing and code-buffer switches can discard the backing entries.
Copying/restoring a cursor cannot prove that old host PC pairs remain valid.
This fixture explicitly zeroes the owned 4 MiB shadow cache and resets its cursor
before callback entry and after caller restoration, so returns use FEX's normal
lookup fallback when the cached pair is absent. It deliberately spends memory
bandwidth and touches those pages; it is not the proposed hot-path policy. It
neither copies live runtime atomics nor reenters a running engine frame.

## Coverage

Sixteen common checks cover typed result/stack cleanup, warm reuse, actual guest
mutation and caller restoration of CPU/FPU state, bounded frame/return/stub and
exception validation, service/depth limits, shared finite allowance, a looping
callback, latched stop across host dispatch, UD2 attribution and malformed callback
completion. The callback executes SSE1, MMX/EMMS, FNINIT, LDMXCSR and an FS load,
and changes GPRs/flags. Its TLS base is distinct from the suspended caller's.

A callback with one remaining admission must stop before the second instruction
before the nonterminating callback is admitted. Finite admission budgets remain
the cleanup fallback; process timeout is only a failing-test guard.

Two additional native checks exercise a callback worker whose locked RAM
heartbeat proves guest progress before the host requests stop. The worker must
return `StopRequested`, publish the exact callback PC/GPRs/flags and clean stop
metadata, leave the caller suspended, and retain its request on subsequent
entry. Inspection occurs after join. All bridge entries share the established
FPCR/FPSR/native ABI wrapper; a third native check requires all six witnesses
across exactly **27 bridge entries**. The report includes observed callback
request-to-join time, without claiming a universal bound.

The new cumulative cloud expectation is **127 PASS checks** in both translation
modes: all 111 inherited plus 16 bridge checks. The **phase0.9/code10** APK expects
**148 native PASS checks**: all 129 accepted plus 16 common, two native callback
stop/state checks and one ABI summary. Native acceptance is pending a Pocket S
report. Host simulation cannot establish Android concurrency, ABI or timing.

## Cloud validation

Both translation modes pass all 127 checks, with all 111 inherited labels
retained and 24 bridge entries each. The five native-ABI wrapper controls,
54 fault-policy controls and 31,296-case native x86 division reference output
match the accepted Phase0.8 outputs. Five fresh allocator processes pass their
internal 19-check suite.

Three temporary negative controls were restored after use: resetting the
allowance at each entry fails six checks and does not admit the looping callback;
omitting caller restoration fails four; selectively bypassing the readable-frame
end check fails its incomplete-frame test without admitting invalid pointers.
A preliminary broader removal of that bound also allowed an invalid pointer and
crashed the negative-control process; that was a failed guard, never accepted
execution evidence. Correct sources pass after restoration.

The NDK30 ARM64/API29 full core and diagnostic link. The local phase0.9/code10
APK passes signature, two-library/system-dependency, notices and APK/ELF 16 KiB
alignment verification. Runtime admission remains 4 KiB; alignment alone does
not establish 16 KiB runtime support. Independent GitHub builds and the native
Pocket S report remain separate verification steps.

## Remaining gates

This proves only constructed frames, owned RAM and explicitly admitted code.
Arbitrary faults, REP/helpers/translation latency, general pointer validation,
stack unwinding, reentrant/nested services and blocking native bodies require
separate contracts. No signal handler or new engine cancellation path is added.
Instruction/privilege/timers, general memory/bridge semantics, engine OOM/unload
and 16 KiB hosts remain open. Game execution and FPS/audio/thermal comparison
remain later milestones. See the [plan](phase09-plan.md) and [remaining gates](next-gates.md).
