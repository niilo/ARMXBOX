# Pocket S release findings and continuation plan

## Baseline to continue from

Release **v1.2.11**, Android versionCode **40**, promotes the performance branch
through RC14 to master. The last emulation-code change before release preparation
is `8143dfa789503192e210c6a808181e521f745674`. Release preparation changes version
metadata and documentation; future work should branch from the released master.

| Android option | Release default |
| --- | --- |
| `XEMU_OPT_NATIVE_FLOAT` | ON |
| `XEMU_OPT_TCG_PEEPHOLE` | ON |
| `XEMU_OPT_TB_CACHE_HINTS` | ON |
| `XEMU_OPT_X87_PC24` | **ON** |
| `XEMU_OPT_X87_TRANSFER` | **OFF** |
| `XEMU_OPT_X87_STACK` | **OFF** |
| `XEMU_ENABLE_CPU_PROFILE` | **OFF** |
| `XEMU_ENABLE_LTO` | ON for Release/Profile |

Assertions stay enabled. The ARM64 guest x87 register representation remains
80-bit. The source retains the optional transfer, stack and profiling experiments
for separate comparisons; their presence does not make them release defaults.
Gradle passes explicit gate values, preventing older CMake caches from selecting
an experiment silently.

The production Release APK uses `com.izzy2lost.x1box`, the existing GitHub release
signing key and no development suffix. The tested RC14 Profile APK uses `.dev`
and the separate test certificate. Both are optimized builds, but the final
production APK's package/signing/lifecycle still needs a device smoke test.

## Improvements retained since the previous release

- **Rendering and Vulkan:** vertex/index propagation and buffer reuse, normalized
  primitive handling, uniform/shader state consumption, texture/descriptor
  notification, surface freshness and transfer/read ordering. RC8 corrected
  stale-state regressions found during earlier optimization experiments. RC9
  avoids redundant dynamic-state commands while keeping command dependencies.
  Existing Adreno/driver workarounds and correctness fences remain.
- **CPU and copies:** exact guarded native helpers, set-condition/branch peephole
  fusion, cache hints, compact index handling, swizzle/copy work and reduced
  repeated renderer setup. Broad inlining or removing synchronization is not
  part of the release decision.
- **Audio:** RC10 reuses local attenuation gains in voice mixing, with the same
  PCM behavior in retained fixtures. Backend/rate/period/FIFO/worker startup
  reporting helps diagnosis; audio scheduling, resampling and DSP timing are
  not deliberately relaxed for FPS.
- **PC24 arithmetic:** separate guarded ARM64 binary32 operations and square root
  round once for eligible inputs; bounded binary64 checks recover inexact.
  Full 80-bit state, old sticky flags and pending exceptions are preserved.

## Device evidence

The recent normal SSX 3 runs used 3x resolution on an AYANEO Pocket S (Android 13,
Adreno 740) with Turnip Mesa 26.3 git `1f4952878d`. The first below-45 FPS report
marks the heavier section; that and two following transition reports are excluded.
All subsequent reports, including faster ones, are retained. FPS is total logged
guest flips divided by logged milliseconds, not unique presented frames or a
frame-time percentile. Comparisons below use the same first 15 settled windows.

| Candidate | Flips / logged ms | FPS | Initial / peak skin temperature |
| --- | --- | ---: | --- |
| RC14 Control | 1926 / 75324 | 25.56954 | 31.780 / 41.979°C |
| RC14 PC24 | 2355 / 75296 | **31.27656** | 38.626 / 44.796°C |
| RC14 Combined | 2311 / 75175 | 30.74160 | 29.435 / 41.870°C |

The PC24 pair shows **+22.32%** observed FPS. The first eight windows show
+32.70%, the following seven +11.09%; removing the first two, including RC14's
fastest report, still gives +20.49%. RC14 started warmer, so a cooler start does
not explain that pair. This is encouraging device evidence rather than a proven
pure CPU-cost reduction or a guaranteed gain in other games.

Combined is −1.71% against normal RC14 over 15 windows and −1.76% over 16.
Its early/late direction changes, so neither a transfer benefit nor a regression
is established. Its full 42-window capture is 6982 flips / 210640 ms = 33.14660
FPS, with five-second reports from 21.80 to 55.29. The middle section is faster
than the beginning/end. That longer average must not be compared directly with
normal RC14's 16-window 31.46205 FPS average.

For historical context, the same first 15 windows in RC11 and RC13 logged
29.05292 and 24.93789 FPS. There is no paired device capture of the previous
public release under this method, so do not label +22.32% as a measured gain
over that release. The user reports an overall improvement over their current
release, which is useful feedback but does not supply the missing measurement.

Runtime CPU flags, saved settings, driver and audio match the intended control
and PC24 configurations in those exports. Combined's startup CPU-mode line and
saved settings dump are absent; its identity and resolution are user-supplied.
Only its driver/audio records are independently comparable. Missing fields must
remain unknown rather than being treated as disabled or changed settings.

Current thermal severity stays 0 in all three sessions. No fresh fatal/assertion,
Vulkan/device-lost, OOM or audio-error signature appears. Historical crash-buffer
records and previous-session markers are excluded. Visual/audio quality is not
established from logs; the earlier RC8/RC9 user feedback was clean, while explicit
RC14 quality feedback has not been supplied. Sustained 60 FPS, other games and
Steam Deck runtime results remain unmeasured. Clocks, power/fan/cache state and
exact route/frame alignment are not observed.

Adaptive unlock is Y in 25/39 shared-window samples for RC14 versus 3/32 for
Control. These are sample counts, not precise time fractions. Unlock state can
react to faster execution; its correlation with FPS is not a pacing bug proof.
GPU timings are partial, overlapping EWMA values. Neither FIFO idle nor their
sum is pure guest CPU time. Mirror copy around 0.03 ms and GPU render around
5–6 ms do not identify a new copy-throughput bottleneck.

## Arithmetic and validation contract

PC24 requires nearest-even in both guest control and softfloat state, native
policy ON, and a safe host FPCR. Inputs must already be exact binary32 values
with nonzero unbiased exponents in [-60,+60], or signed zero. Wider significands,
extended exponents, specials, incompatible modes, zero divisors and negative
nonzero square roots retain the existing implementation. Operands are never
rounded merely to enter the native path. The existing PC53 path is retained.

The numerical oracle is unchanged real softfloat plus immutable RC13 helpers.
GCC ASan/UBSan and Clang each pass four feature/policy combinations: 1,774,436
modeled state comparisons and 1,774,469 direct numerical/rejection checks per
combination, independently repeated. Four semantic negatives catch incorrect
admission, double rounding, lost inexact and pending-exception reconciliation.
Profiler checks exercise six configurations and five negative controls per
compiler. See the retained [fixture and limits](../../tests/xbox/x87-native24/README.md).

Actual RC14 packaged ARM64 ThinLTO inspection verified FPCR success-edge
dominance over new arithmetic/exactness work, inline hardware FSQRT and erased
normal profiling. The binary wrapper grows 696→1332 bytes and sqrt 644→1056
bytes; other twelve arithmetic helper sizes stay unchanged. The 1048-byte growth
and eligibility checks are real costs, not measured cycle counts. Host fixtures
do not execute Android lifecycle, real ARM FPCR, guest decoder/MMU faults or games.
Cross-platform GitHub CI passed the emulation commit. Release signature/manifest,
native dependencies/alignment and source identity are checked separately.

## Prioritized next options

The first focused implementation after release is
[RC15's cold policy initializer](pocket-s-rc15.md), addressing option 2 below.
It has a fresh release-equivalent control and requires device comparison;
the released baseline and the device findings above remain the references.

1. **Keep a reproducible baseline and improve evidence.** Pin release/tag, APK
   hash, actual CPU gates, driver, settings, course and starting temperature.
   Preserve these in exported logs even when the rolling log buffer loses
   startup. Repeat normal runs separately from diagnostic runs. Update the
   optional rejection parser for the retained v3 `pc24Env`/`hostEnv` fields before
   analyzing a new PC24 profile. Never compare instrumented FPS with normal FPS.
2. **Separate cold native-policy initialization from the hot arithmetic path.**
   The existing outlined wrapper's 208-byte frame includes cold Android property
   lookup storage. Isolating initialization may reduce hot-call overhead without
   widening the numerical subset. Check final ARM64 paths/frame size, preserve
   policy/thread behavior and rerun the numerical oracle; use one device A/B pair.
3. **Measure remaining PC24 rejection and helper cost.** The RC13 profile's first
   PC24 guard explained nearly every binary fallback and all sqrt fallbacks, but
   did not establish operand eligibility under RC14. Use a separate diagnostic
   build to distinguish mode, host and operand rejection before expanding bounds
   or exact subsets. The old profile's call counts are not CPU-time shares.
4. **Investigate Android input and scheduling overhead.** InputEventSender warning
   streams remain around 74–80/s in these runs. Attribute their origin and thread
   CPU cost before changing dispatch/acknowledgement behavior. Collect CPU clocks,
   runnable/wait time and thermal/power data; retain controller responsiveness and
   sound quality as acceptance criteria.
5. **Target the next measured CPU helper/SSE/stack hotspot.** Re-profile after
   PC24; the ordering can change. Optimize actual state/flag work at existing
   helper boundaries. Raw stack lowering and transfer wrappers remain separate
   OFF experiments until repeated normal-device evidence supports enabling them.
   FEX-style representation/dataflow ideas may inform narrow changes; FEXCore
   is not a drop-in replacement for this full-system CPL0/MMU/interrupt emulator.
6. **Broaden graphics, audio and device acceptance.** Repeat later SSX 3 sections
   and 1x cases that previously exposed popping. Test PGR2, Chaos Theory, THPS3
   and DOA3, then Steam Deck separately. Attribute Vulkan CPU setup costs and
   surface-to-vertex alias/freshness leads before removing barriers. For sound,
   measure callback/FIFO/worker/underrun behavior before changing resampling or
   worker counts. Desktop binaries compiling is not Steam Deck gameplay evidence.

Each iteration should have one main change, a control, retained source/flags,
appropriate semantic negatives, final ARM64 inspection where applicable and
normal-device measurements with explicit graphics/audio feedback. Keep guest
memory ordering, interrupt/fault boundaries, full x87 state and required GPU
synchronization. Do not force unlock or remove intermediate VBLANKs to raise a
counter; genuine 30 FPS titles depend on those events.
