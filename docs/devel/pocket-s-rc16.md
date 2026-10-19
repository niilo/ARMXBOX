# Pocket S RC16: avoid redundant PC24 exactness checks

RC16 builds on v1.2.14/RC15 and makes one CPU change. When **both** softfloat's
inexact flag and x87's precision-exception flag are already sticky, admitted
PC24 binary arithmetic no longer computes an inexact result that cannot change either
flag. It still reconciles pending unmasked exceptions on every successful call.

The guarded binary32 operation, full 80-bit result representation, operand
bounds, guest/host rounding checks and softfloat fallbacks remain unchanged.
Neither PC53 nor renderer, audio, input, scheduling or VBLANK behavior changes.
Clearing either guest flag immediately restores exactness calculation; there is
no cached assumption about flag lifetime. The low-level API accepts an explicit boolean to omit the
inexact output only when its caller does not need that output.

## Expected mechanism and limits

The avoided work consists of float-to-double conversions and double arithmetic
and comparisons used solely to reconstruct inexact. Add/sub may require two
subtractions; multiply, divide use a double product. Square root retains its released path: it clears temporary
softfloat flags before native dispatch, so this skip cannot benefit that helper. A new flag
test/branch has a cost when flags are clear. Historical SSX 3 profiles establish
that these helpers are frequent, but do not measure how often both flags remain
set after RC15 or the net runtime benefit. This is an experiment, not a measured
FPS improvement or a promise of 60 FPS.

GCC ASan/UBSan and Clang each pass all four feature/policy combinations against
the immutable RC13 helpers and actual softfloat: 1,774,436 complete modeled
helper-state comparisons plus 1,774,469 direct numerical/rejection cases per
combination. Each direct binary case also compares the flag-omission API for unchanged
admission/result, rounding, errno and rejection-side hardware flags. Five
negative controls include skipping with either flag instead of requiring both.
The optional profiler fixture also passes its six configurations and five
negative controls with GCC sanitizers after updating its API/accessor shim.
Host tests do not execute Android FPCR, guest games or device lifecycle.

Local optimized ARM64 inspection verifies all three binary exactness bypasses, FPCR
success-edge dominance, no new hot stack canary/property lookup, and unchanged
80-byte binary-wrapper / 64-byte square-root frames. The explicit boolean keeps
flag selection in registers. An earlier optional-pointer prototype introduced
stack-canary work and was superseded before device handoff. These instruction
checks do not establish cycles saved or normal-device FPS.

## Device comparison

Use the production-signed GitHub Actions builds from these branches:

| Branch | APK/launcher test label | Runtime code |
| --- | --- | --- |
| `perf/pocket-s-rc16-control` | `rc16-control` | v1.2.14 emulation, new version metadata only |
| `perf/pocket-s-rc16` | `rc16` | Control plus the redundant-check optimization |

Both use version 1.2.15/code 44, the existing production package and signing
key, so they can replace each other without clearing settings or saves. Their
filenames, launcher labels, Settings/About version, startup identity and exported
log names distinguish their roles. Debug/Profile retain the red Dev icon;
production-signed comparison builds retain the green release icon. No stable
release or master merge is part of this experiment.

Run Control then RC16 on the same SSX 3 course at 3x with Turnip, profiling OFF,
the same power/fan/charging settings and comparable starting temperature.
Include later gameplay, export each log promptly and note visual, sound and
input quality. Repeat in reverse order if the difference is small. Normal FPS
and instrumented FPS must remain separate. Revert to Control if quality changes.
