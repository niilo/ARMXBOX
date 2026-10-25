# Phase 0.10.2: code allocation failure checks and bounded cold tests

The Pocket S recovery journal from phase0.10.1/code12 contains 146 PASS lines,
including the inherited CPU, native ABI, privileged-exit, controlled-execution
and bounded callback checks. All six native ABI witnesses passed in those
stages. Policy context 0 (default instruction policy, checkpoints enabled,
AVX/SVE256 disabled) returned from its first six guest entries. Then:

```text
policy return entry6 reason=0 pc=10000017 trap=13
Failed to mprotect last page of code buffer.
instruction policy entry6 policy0 pc=10000000 budget=64
```

There is no corresponding return or final report. Entry 6 is the first default
PCMPGTQ cold case, after HADDPS, PSHUFB and PTEST cold/warm pairs. The protection
error occurs during the preceding code-cache recreation, before that guest entry.
This locates the failure, but does not establish the failing allocation address,
errno, crash signal or exact cause. The journal does not record successful
completion of the new policy tests. Native acceptance and controlled XBE remain
gated. Input SHA256:
`2a9a6a7ffbfddfe913c4fc2d0f4ff89445ee2b425c6aabcc813486c456408025`.

## Changes

The policy diagnostic now uses `ClearCodeCache(thread, false)` for its cold
translation cases. This clears the lookup caches, including the shared lookup
map, without replacing the whole JIT code buffer. The previous default overload
recreated and doubled the buffer: 16, 32, 64, then 128 MiB, even though each case
contains only a few guest instructions. Existing engine buffer-recreation,
explicit invalidation and callback tests elsewhere remain. All 113 policy
entries, cold/warm comparisons, expected exceptions, budgets and ABI checks are
retained. This reduces unnecessary diagnostic allocation pressure; it does not
prove that arena exhaustion caused the device failure or improve gameplay FPS.
Cold cases now record their names and scenario before resetting the lookup.

Patch 0009 fixes two independently testable allocation error paths:

- The non-fixed region allocator returned its candidate address after an OS mmap
  error because the error branch incorrectly required a null region pointer.
  It now returns the error without publishing allocated pages. A real invalid-FD
  file mapping verifies EBADF and a subsequent valid allocation verifies reuse.
- The code-buffer constructor checked only for null, which accepted Linux
  `MAP_FAILED`. Both allocation failure and guard-protection failure now stop
  unconditionally, including release builds with debug assertions disabled.
  Continuing with an invalid buffer or missing guard is not permitted.

Bounded mapping failures record request size, protections, flags, FD and errno.
Code-buffer fatal diagnostics include allocation size/errno or base/guard/errno.
These use the existing normal logging/recovery journal; no signal handler is
added. A fatal allocation can still terminate the process. This is not general
OOM recovery, a larger arena, a fallback outside owned memory, or a confirmed
Pocket S crash fix.

## Verification and device gate

The isolated host test exercises the real code-buffer constructor with injected
MAP_FAILED and guard-protection errors, plus successful allocation/release.
Both the OS mapping test and code-buffer test fail against the previous engine
patch state and pass with patch 0009. Complete host CPU fixtures must pass 155
checks in both translation modes, with the same 113 policy entries. Three
journal controls and three code-buffer controls are independent host checks.

The APK is **phase0.10.2/code13**. The native expectation is **177 PASS**:
all prior 176 plus the failed-OS-mapping/reuse control. A device run is still
required. If it closes, reopen and Share report before starting another run;
otherwise share the complete report. No game or production emulator settings
are changed, and no performance gain is claimed.
