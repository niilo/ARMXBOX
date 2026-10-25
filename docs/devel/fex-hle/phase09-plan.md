# Phase 0.9 plan: bounded exit-based bridge and reverse callbacks

The [Phase0.8 Pocket S report](phase08-android-report.md) accepts all 129 scoped
native checks. Next extend that proven execution boundary to a constructed typed
service and reverse callback before admitting a controlled XBE. The first constructed
[diagnostic increment](phase09.md) is implemented; native acceptance is pending.
The broader contracts below remain a plan, not a general runtime or a performance
result.

## Source findings and design

The original `diagnostic.cpp` bridge uses a HLT stub, copies `CPUState`, calls a
reverse guest through `executeGuest` and restores the suspended caller. Its
callback is constructed to terminate; it uses a default context without a finite
admission budget. Phase0.8's `execution_control_probe.cpp` separately proves
finite budgets, latched stop, coherent FPU state and native worker ownership.
Neither fixture demonstrates those two contracts together.

Compose them through an outer host dispatch loop. A service is considered only
after `ExecuteControlledThread` returns `GuestExit`, with matching owned stub PC
and expected generated exception metadata. Do not service `StopRequested`,
`BudgetExhausted`, arbitrary HLT/GP, or a partially validated request. Do not call
back into a currently executing engine thread or rely on native stack unwinding.

The first service is an explicitly typed, pure integer fixture. Use an owned
readable stack range, checked 32-bit arithmetic and a complete fixed frame-size
check before reading arguments. Validate the return PC against owned executable
fixture code. Reject malformed frames before any host-body side effect. No
arbitrary host function pointers or guest pointers converted into host callbacks.

Retain a suspended caller snapshot while running the reverse callback from the
outer loop on the same stopped engine thread. Save architectural flags and the
complete currently demonstrated CPU/FPU/SIMD state; define whether shadow
call/return bookkeeping also needs a separate saved cursor. Do not assume a
`CPUState` copy covers all execution-frame or engine-private state. Inspect
`CallRetStackBase`, `callret_sp`, exception metadata and the new control fields
before choosing the snapshot representation. Do not restore running-control
bits, erase a latched request or accidentally reuse callback fault metadata.

Give each entry a finite budget from one caller-owned admission allowance.
After each return subtract its consumed admissions using checked arithmetic,
including faulting admissions; do not reset the allowance per callback. Apply an
explicit callback-depth and service-count cap. Host-body work is fixed and
nonblocking in this increment. Instruction admissions cannot bound a blocking
host function or translation time.

On callback completion, restore suspended caller state, publish the typed result
and specified stdcall cleanup, and resume at the validated return PC. On callback
budget/stop/fault, preserve diagnostic attribution to the callback; do not fake
successful completion or resume the caller. Stop remains latched until explicit
idle reset. Define abort/reset ownership before allowing another invocation.

## Implementation and validation sequence

1. Audit snapshot and shadow-stack ownership, then introduce an explicitly listed
   diagnostic target/source using the existing controlled entry/ABI wrapper.
   Keep all 129 native and 111 cloud checks intact. No new engine patch unless a
   demonstrated missing contract requires it.
2. Prove normal typed call, exact return and stack cleanup; verify flags,
   nonvolatile GPRs, FS/TLS, SSE1/x87/MMX and floating control state after a callback
   that deliberately changes them. Check both translation modes and warm reuse.
3. Add malformed stack/return/stub cases and a host-body side-effect counter.
   Rejection must leave that counter unchanged. Add explicit depth and service
   limits and shared-budget accounting tests.
4. Try a finite callback before admitting its nonterminating-loop variant. Verify
   budget return, pending stop before callback entry, and fault attribution.
   Preserve caller snapshots for diagnosis; never report a partial call as success.
5. Add native worker stop during a callback only after a guest heartbeat proves
   progress. Join before inspecting/restoring state. Verify request remains
   latched across returning to host dispatch and all six native ABI witnesses.
6. Negative controls must defeat budget propagation, caller-state restoration
   and malformed-frame validation separately. Each must fail the relevant check
   without running an unbounded body. Run inherited reference/policy suites and
   APK signature/library/alignment checks before publishing a new device probe.

The implemented fixture adds 16 common checks and three native checks, for a
cumulative expectation of 148 native PASS checks.
A native report is needed for the combined worker/callback/ABI contract; cloud
simulation alone does not satisfy it. The phase0.9/code10 diagnostic is the device probe for this increment.

## Subsequent gates

This fixed-frame bridge is not a general kernel ABI, scheduler, exception
unwinder or memory marshaller. REP/helpers/raw-fault cancellation, Xbox instruction
admission, virtual privilege/timers, guest memory and code publication still need
explicit coverage. Work toward a generated controlled XBE with minimal imports
once its required contracts are demonstrated. Then triangle/texture/PCM timing,
retail checkpoint and matched device frame-time/audio/thermal comparison establish
whether this backend actually improves performance over the retained emulator.
