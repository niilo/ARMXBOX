# Pocket S RC15: isolate cold x87 policy initialization

RC15 continues from released v1.2.11. Its single runtime change moves the
one-time Android property/desktop environment lookup into a cold, non-inlined
function. The cached policy check stays at the existing helper boundaries.
Arithmetic admission, full 80-bit registers, exception state and softfloat
fallback are unchanged. PC24 remains ON; transfer, raw stack and profiling
remain OFF. Rendering, audio and adaptive VBLANK/unlock retain release behavior.

The Android lookup previously placed its 92-byte property buffer in the outlined
binary arithmetic wrapper and several other hot helpers. Even after startup had
cached the policy, calls still paid for larger frames, register spills and
buffer-related stack-canary checks. Outlining the lookup removes that buffer
from the hot functions while retaining normal compiler stack protection.

## Builds and device comparison

| APK version name | Source role |
| --- | --- |
| `1.2.12-pocket-s-rc15-control` | Released emulation code, with version metadata changes only. |
| `1.2.12-pocket-s-rc15` | Control plus the cold policy initializer. |

Both are optimized ARM64 Profile builds, with assertions and ThinLTO enabled,
CPU profiling compiled out, package `com.izzy2lost.x1box.dev`, label **X1 BOX
Test**, versionCode 41 and the same retained test signing certificate. They can
replace one another through the normal Android installer without a version
downgrade or clearing test-app data. The production X1 BOX app is separate.

The existing Android GitHub Actions workflow can also build **signed Release**
artifacts from either branch. Those install as production **X1 BOX**, version
1.2.12/code 41, and preserve its settings with the existing production signing
key. They are development candidates available as workflow artifacts, not a
published stable v1.2.12 release. Both branches use the same version code so
their signed Release APKs can also replace one another normally. Select the
same app/package for both sides of a comparison; Test and production settings
are separate.

Start with Control, then RC15, on the same SSX 3 route at 3x with the same
Turnip driver, settings, power/fan/charging state and comparable starting
temperature. Include the heavier and later gameplay sections, export each full
log promptly, and note any vertex/texture popping, audio or input changes.
Repeat in reverse order if the difference is small. The existing log-buffer
limitation can still lose startup; retain the APK names and settings with each
export. Compare matching gameplay coverage, keeping diagnostic runs separate.

## Validation and limits

Linked ARM64 ThinLTO inspection compares the fresh control with RC15. The shared
binary wrapper's stack frame decreases **208 to 80 bytes**, and `FSQRT` decreases
**176 to 64 bytes**. The property lookup and its buffer move to a separate
initializer; the cached arithmetic paths no longer contain the corresponding
TLS canary load/check. This is emitted-code evidence, not a device cycle count
or FPS measurement. Numeric host FPCR guards must still dominate native work.

The real-softfloat/immutable-RC13 numerical suite passes with GCC ASan/UBSan and
Clang, PC24 ON/OFF and native policy ON/OFF: 1,774,436 helper-state comparisons
and 1,774,469 direct numerical/rejection checks per combination. All four
semantic negative controls are detected. The optional profiler's retained six
configuration checks and five negative controls also pass with GCC sanitizers.

The new [policy fixture](../../tests/xbox/x87-policy/README.md) compares the
released and current Android/desktop branches for ten fresh-process settings,
10,000 cached calls per setting and reentrant initialization. It verifies the
same default-before-query and leading-`0` semantics and detects repeated queries
or reversed policy. Initialization/cache lifetime is preserved; no concurrent
initialization guarantee is added.

The Android startup CPU-mode report initializes policy before vCPU creation.
Changing the external policy still requires a new process. Host fixtures do
not execute actual ARM64 FPCR, Android lifecycle or games. Final APK signature,
manifest, native alignment/dependencies and linked helpers are checked
separately. No RC15 device FPS gain, visual/audio result or 60 FPS claim is
established in the cloud.

## Following this comparison

If RC15 passes quality checks, retain or reject it based on repeated normal
runs. Then collect a separate PC24 diagnostic capture and update the rejection
parser for `pc24Env`/`hostEnv` before choosing another arithmetic subset or
helper change. Call counts are not CPU-time shares. Input warning/scheduling
and sound callback/FIFO costs remain measurement targets from the
[release continuation plan](pocket-s-performance.md); they do not justify
changing worker counts, affinity or synchronization without new evidence.
