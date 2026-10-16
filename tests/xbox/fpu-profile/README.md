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
  more than three report lines per five seconds per helper-executing thread.
- Successful and failed clock calls preserving errno.
- Logger-induced rounding-mode, exception-flag and errno changes being restored.
- A nonlocal guest-style exit counted as an entry without a completed sample,
  followed by successful future sampling without stale depth state.
- Independent thread-local counters and sampling phases.

The executable negative control removes only errno restoration in the extracted
core; the errno oracle must reject it. Expected assertions have core dumps
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
The diagnostic APK additionally requires the CPU profiling Settings switch
before launch. Startup initializes the flag before creating vCPU threads; it
must not change during emulation. The existing Android property override can
force profiling off. Desktop compilation retains inactive API stubs.

Reports use separate tags from the old translation counters:

- `xemu-cpu-run`: actual selected helper entries and interval wall/thread-CPU ns.
- `xemu-cpu-native`: guarded x87 hit/fallback outcomes, not all SSE operations.
- `xemu-cpu-sample`: completed sample count / summed sampled wall ns / summed
  sampled host-thread CPU ns for each category.

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
