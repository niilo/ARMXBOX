# Next increment: bounded guest execution and cooperative stop

The [Phase0.7 Pocket S report](phase07-android-report.md) accepts all 109 native
checks, including architectural EFLAGS and precise controlled exits. The next
runtime gate is return from a nonterminating guest without terminating the app
process. [Phase0.8](phase08.md) now implements the first opt-in instruction-checkpoint
increment; its 129 native checks are pending. This document retains the broader
plan and remaining coverage requirements, not a claim of general cancellation.

## Engine mechanisms and constraints

At FEX pin `4e2b509a42b7762a48c407d6a59bb897dca1ded8`,
`Arm64JITCore::EmitSuspendInterruptCheck` conditionally emits a store to
`InternalThreadState::InterruptFaultPage`. Its context flag
`NeedsPendingInterruptFaultCheck` is currently enabled by the GDB-server path.
The dispatcher and `SignalScopeGuards.h` also touch the page when leaving guarded
runtime sections. These existing engine mechanisms are the first reuse candidate;
their presence does not prove bounded stop coverage or correct state publication.

The poll can be an **8-byte STR XZR or 16-byte STR Q0**, depending on the
structure offset and available instruction encoding. The accepted word-MMIO
adapter therefore cannot serve as its cancellation handler. Cancellation needs
separate ownership and validated generated-code sites. Do not broaden ordinary
MMIO handling to arbitrary host faults to accommodate the polling instruction.

`SignalDelegatorConfig` exposes spill-SRA stop/pause addresses and register maps,
but the base delegator does not install an Android signal handler. The frontend
must own registration, restoration, chaining and async-signal-safe handling.
`DeferredSignalRefCount` protects runtime sections where immediate exit can be
unsafe. Correct recovery depends on the precise checkpoint and whether guest
state has already been spilled. Architectural EFLAGS from Phase0.7 must survive
both generated and signal-driven exits.

## Implementation sequence

1. **Prove checkpoint coverage before exposing execution.** Inspect emitted
   checks at block entries, backward branches, linked blocks and multiblock
   internal loops. Record the guest-PC/state contract at each admitted stop site.
   If existing checks cannot cover a loop, add an explicit checkpoint through a
   reviewed engine patch. Reject unsupported cases rather than claim a bound.
2. **Add an explicit opt-in context contract.** Configure polling before
   translation, retain default contexts without added polling, and prevent
   reuse of code compiled for a different execution policy. Avoid enabling
   unrelated GDB behavior or changing global opcode tables.
3. **Implement owned stop requests and return.** Keep the execution worker,
   engine thread and polling page alive while a request can race with execution.
   Define pre-entry requests, repeated requests, rearming/resume and request
   generations so a request is not silently cleared at entry. Use a distinct
   frontend stop reason; do not fabricate a guest page fault or a successful HLT.
4. **Recover only at demonstrated sites.** Validate the active owner, complete
   poll access range, generated host-PC site and critical-section state. Publish
   a coherent guest PC/GPR/flags/FPU snapshot and return through the engine's
   normal spill/stop path. Keep host FPCR/FPSR and ABI ownership in the existing
   execution wrapper. Do not allocate, log or perform C++ unwinding inside the
   signal handler. Chain unrelated host signals under the established contract.
5. **Verify lifetime and resume.** Observe state only after execution returns
   and the host worker is joined or otherwise synchronized. Restore page and
   handler ownership before destruction. Test repeated stop/restart, callbacks,
   raw memory faults and guarded runtime sections before broadening admission.

A deterministic translated-block budget is a possible intermediate diagnostic
if needed to validate the same checkpoint/state contract without concurrent
signal delivery. It would bound admitted block execution, not guest cycles or
wall-clock time. It is not a substitute for the cooperative-stop demonstration.
Do not add a parallel optimizing JIT for this work.

## Acceptance evidence

Cloud checks must cover emitted access widths, checkpoint placement, request
ownership/state transitions and safe defaults. Retain all 96 current cloud
checks and reference suites, with a negative control that actually removes the
stop opportunity or violates its ownership contract. A process timeout is a
failed-test guard, never proof that the in-app guest stopped successfully.

The native increment must demonstrate successful return and coherent state from
known nonterminating direct, backward and linked/multiblock loops, then check
safe resume where supported. Verify stop-at-entry, repeated requests,
callback/fault boundaries, handler restoration, allocator coexistence and all six
ABI witnesses. Record observed stop latency and the tested workload; source
inspection alone cannot establish a universal maximum. Compilation, blocking
callbacks and long guarded runtime sections require separate bounds.

Keep 4 KiB host admission explicit and preserve the owned 2 GiB virtual arena.
No arbitrary XBE entry point is admitted by the existing HLT/privileged-exit
fixtures alone. Instruction/privilege/timer and bridge/memory contracts remain
open. After the required execution and embedding contracts pass, proceed to the
controlled XBE/minimum-import milestone, then triangle/texture/PCM timing and a
retail checkpoint. Only matched device runs can establish an FPS improvement.
