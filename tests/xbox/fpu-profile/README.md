# Optional runtime FPU helper profiling

Run from the repository root:

```sh
python3 tests/xbox/fpu-profile/profile-test.py --sanitize --negative-control
python3 tests/xbox/fpu-profile/profile-test.py --cc clang --negative-control
```

`--output-dir` isolates concurrent fixture binaries. The runner executes three
configurations: ordinary host stubs, Android/AArch64 with hooks compiled out,
and the instrumented Android/AArch64 configuration. The active configuration
runs on the host with deterministic clock and Android-log boundaries; it is
not execution on ARM hardware or against an Android audio/graphics driver.

The fixture compiles the complete actual `fpu-profile.c` and header. It extracts
the six relevant x87 stack/register helper bodies and a scalar SSE add from
current source and immutable RC10 `fa8b312c49292e60c53f5f8221a33d4223e282bb`.
Helper register/tag state and scalar SSE output, including aliases, must agree.
It also executes the actual guarded x87 binary-operation wrapper and native
arithmetic header to check hit/fallback counting and single operand evaluation.
Minimal CPU structs and flag accessors are fixture boundaries; this is not a
complete QEMU CPU or arithmetic-conformance test.

Other checks cover:

- Inactive-until-initialized behavior and disabled zero clock/log calls.
- One completed timing sample per 1,024 category entries, including the first.
- Integer interval counts, report reset without sampling-phase reset, and no
  more than six report lines per five seconds per helper-executing thread.
- Successful and failed clock calls preserving errno.
- Logger-induced rounding-mode, exception-flag and errno changes being restored.
- A nonlocal guest-style exit counted as an entry without a completed sample,
  followed by successful future sampling without stale depth state.
- Independent thread-local counters and sampling phases.
- Every load/store ID/path counter cell through the macros (not execution of all
  13 load/store bodies), lazy and single-evaluation detail hooks,
  prioritized rejection reasons and original policy/arithmetic call counts.
- All 16 guest precision/rounding combinations under four host rounding modes,
  operand classes/exponent/significand limits, policy rejection, division zero,
  negative square root and result rejection; output, flags, FENV and errno agree
  between runtime-off and runtime-on profiling.
- Complete maximum-width detail records with shared sequence and counter reset.

Executable negative controls remove errno restoration, mislabel PC24 as PC64,
and reexecute arithmetic in the reason classifier. The errno, reason and actual
attempt-count oracles must reject them. Expected assertions have core dumps
disabled. No installed binary, guest file or production source is modified.

`--benchmark` additionally runs nine alternating host trials of original,
runtime-disabled and sampled tiny stack helpers, plus empty-scope controls.
Each case uses five million iterations; only that host benchmark process is
pinned. Its results include helper-call and clock overhead and vary with host
load. They do not predict Pocket S performance. In particular, runtime-disabled
hooks still change generated code; clean device A/B builds must compile them
out using the gate below.

## Build and runtime gates

`XEMU_ENABLE_CPU_PROFILE` defaults to 0. Unless it is 1 on Xbox Android AArch64,
scope/result hooks compile away and the API reports profiling inactive. The
Android diagnostic companion is selected by Gradle property
`-Pxemu.cpuProfileHooks=true`; ordinary control/optimized APKs leave this false.
The RC13 diagnostic companion defaults profiling on using its separate v2
Settings preference; an old legacy preference does not silently disable it.
The Settings switch or `debug.xemu.cpu.profile=0` can turn it off before launch.
Startup initializes the flag before creating vCPU threads; it must not change
during emulation. The ordinary APK compiles all hooks out and hides this switch.
Desktop compilation retains inactive API stubs.

Reports use separate tags from the old translation counters:

- `xemu-cpu-run`: actual selected helper entries and interval wall/thread-CPU ns.
- `xemu-cpu-native`: guarded x87 hit/fallback outcomes, not all SSE operations.
- `xemu-cpu-sample`: completed sample count / summed sampled wall ns / summed
  sampled host-thread CPU ns for each category.
- `xemu-cpu-ls`: version 2 individual helper counters in the fixed order
  normal-exact-fast / precision-fallback / non-normal-fallback / generic.
- Two `xemu-cpu-reject` records: version 2 first/prioritized rejection reasons
  for binary arithmetic and square root, respectively.

The original three records retain their fields. All six share the report
sequence and Android log thread ID. New counts add no clock reads or extra
per-helper logs. Counters are collected for every selected/rejected call, not
only the timed sample. `fpu-profile-reasons.h` classifies rejected outcomes using
integer operand/control reads; it never retries the arithmetic. Existing
native hit/fallback accounting remains separate and occurs exactly once.

## Detail semantics

Thirteen load/store IDs distinguish f32/f64 loads into FT0/ST0, i32/i64 loads,
f32/f64 stores, raw80 loads/stores and BCD helpers. Six floating transfer helpers
may select the new exact C-helper branch. The four path labels describe which
branch/eligibility check was selected, not the cost of the remaining softfloat
work or newly raised flags:

- `normal_exact_fast`: actual new helper exact branch selected.
- `precision_fallback`: mode/significand eligibility requires the reference
  conversion path; this does not prove that an actual rounding exception occurs.
- `non_normal_fallback`: other rejected source/destination encodings or ranges,
  including values outside the destination's normal exponent range.
- `generic`: unchanged helper path, including transfer optimization compiled off.

The generic/reference conversion can use preexisting softfloat exact-normal
shortcuts. Do not label every fallback as slow software arithmetic. An inherited
sticky exception flag does not determine these labels.

Arithmetic reasons are exclusive and prioritized: guest PC24/PC64/reserved
precision before non-nearest RCdown/up/zero, then policy off, first invalid
operand A/B class or exact-53-bit/exponent eligibility, division zero or negative
sqrt, then later result rejection. Class keys distinguish denormal,
pseudo-denormal, unnormal, infinity and NaN. If PC and operands both fail, only
the PC bucket increments; it does not prove the operands were eligible. Result
rejection combines later result-range/class/underflow checks because the failed
operation is not repeated and its unwritten output is not inspected. Compare
and integer-conversion native outcomes retain coarse hit/fallback counts only.

These are helper calls, not guest instruction counts. Register/constant helpers
also contribute to the stack category; the six raw-stack candidates are a
subset. Non-helper TCG work is absent. Report intervals begin/end at completed
sample boundaries and may exceed five seconds; the final partial interval is
not flushed at process exit. Nested/exceptional helpers can put entries and
completed samples in different reporting windows. Timings are inclusive,
periodic sampling can correlate with workloads, and tiny-helper samples can be
dominated by clock overhead. Do not sum categories or multiply them into an
exact whole-CPU share. Interval thread-CPU/wall ratio describes that host thread
only; wall time includes scheduling and waits, and thread CPU includes all its
work between report boundaries, including diagnostic overhead.
