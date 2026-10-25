# Phase 0.8: opt-in instruction checkpoints and cooperative stop

The [Phase0.7 Pocket S report](phase07-android-report.md) accepts all 109 native
checks. This increment implements the first controlled-execution contract in
pinned FEX's existing translator. It uses explicit instruction-boundary checks
rather than widening the word-MMIO handler or installing another signal handler.
It remains a diagnostic subset, not admission of arbitrary XBE/game code.

## Contract and implementation

Reviewed engine patch `0007-controlled-execution.diff` adds an immutable
`InstructionCheckpoints` context option. Default contexts emit no new checkpoint
operations. Controlled contexts reject 64-bit mode and shared/experimental code
caches without a policy key, including rechecking configuration at InitCore.
No GDB-server behavior is enabled as a side effect.

`ExecuteControlledThread(thread, budget)` returns a distinct result:

- `BudgetExhausted`: the next guest instruction was not admitted.
- `StopRequested`: an explicitly latched request was observed.
- `GuestExit`: an ordinary generated guest exception/exit occurred.
- `Busy` or `Unsupported`: the context/thread cannot accept this invocation.

The budget counts **instruction admissions**, not retired instructions, guest
cycles or elapsed time. A faulting instruction consumes an admission. Zero
budget returns before execution. A pending stop wins over a zero budget. Resume
starts at the published next/faulting guest PC with a new supplied budget.

A per-thread atomic state distinguishes an active execution owner and a latched
stop request. `RequestStop` works before entry or during execution. Entry and
return preserve the request; only explicit `ResetStopRequest` on a stopped
thread clears it. Concurrent run/reset attempts are rejected while active.
The frontend owns thread/context lifetime and synchronization: join the execution
worker before observing its architectural state, resetting/reusing or destroying
it. These APIs do not validate arbitrary or freed frontend pointers.

The frontend materializes register/flag state before each admitted checkpoint.
An existing-IR extension emits a native acquire load of the stop flag and a
budget check/decrement, using non-flag-setting instructions. On a stop it stores
the precise guest PC and a separate reason, clears stale generated-exception
metadata, and returns through FEX's existing stack-reset/spill-SRA stop path.
No guest page fault or successful HLT is fabricated. No C++ unwinding or
signal-handler work is involved.

The first prototype also exposed x87 publication requirements: the stack
optimization pass normally synchronizes its virtual stack at block exit. A
checkpoint can return earlier. The pass now publishes x87 values/top/tags before
the new checkpoint, while ordinary contexts retain the prior behavior. Tests
cover pending SSE stores, x87 stack resume and MMX data/EMMS tag state.

This deliberately conservative opt-in mode checks each guest instruction and
materializes state. It has overhead and is not the future performance default.
A later coarser-checkpoint design requires equivalent state/coverage evidence;
VIXL runtime is not a device performance measurement.

## Fixtures and acceptance

Fifteen common checks cover API/configuration guards, zero/one-instruction
budgets, warm resume, distinct guest HLT exits, direct and conditional infinite
loop bodies, linked guest blocks sharing one budget, repeated latched requests,
explicit reset, divide-error repair/resume and SSE1/x87/MMX publication.

A finite two-instruction gate must pass before nonterminating bodies are tried.
Removing checkpoint emission must fail that gate and leave later checks unrun
and failing, not hang or imply success. Removing the x87 publication barrier is
another negative control. All 96 inherited cloud checks and reference suites
remain required; the cumulative cloud expectation is **111 PASS checks** in
both translation modes.

Four additional Android checks exercise two worker executions of a guest loop
with a locked RAM heartbeat. Observing that heartbeat establishes guest progress
before the controller requests stop. Concurrent run/reset calls must be refused.
A large finite budget is the cleanup fallback; the test passes only for an
observed `StopRequested`, not budget exhaustion. State is inspected after join,
and six ABI witnesses wrap all new entries. The emitted acquire-load path and
ordinary API state machine can be checked in the cloud, but concurrent JIT/host
memory behavior under VIXL is not used as Android evidence.

The diagnostic is **phase0.8/code9**. Expected native result: **129 PASS checks**
= 109 accepted checks + 15 common checks + 4 native stop/state checks + 1 ABI
summary across 19 additional controlled calls. Reports include two observed
request-to-join durations in microseconds, including wrapper/TLS/worker-exit
costs. They are fixture-specific observations, not a universal latency bound.
The [Pocket S report](phase08-android-report.md) passes all 129 native checks,
including both worker stops and all six ABI witnesses. Observed request-to-join
times are 43 and 39 microseconds for these fixtures. The broader limits below
remain open; see the [next bridge increment](phase09-plan.md).

## Limits before broader admission

These fixtures establish ordinary instruction boundaries and simple RAM loops.
They do not establish stop latency inside REP/internal instruction loops,
translation, a blocking native helper, callbacks, thunks, syscalls, arbitrary
raw faults or nested execution. The controlled context is not admitted to those
paths by this diagnostic. Cancellation within each requires a separate proven
boundary and lifetime/state contract. The original `ExecuteThread` entry uses a
maximal admission budget in controlled contexts; callers requiring a finite
budget and result must use `ExecuteControlledThread`.

There is no new signal handler to chain/restore; the accepted raw-fault handler
remains separate. The owned allocator and 4 KiB host admission remain in force.
Full instruction/privilege/timer and bridge/memory contracts, general engine
OOM/unload and 16 KiB hosts remain open. This increment does not run games or
claim an FPS, audio or thermal improvement. See the [execution plan](phase08-plan.md)
and [remaining gates](next-gates.md).
