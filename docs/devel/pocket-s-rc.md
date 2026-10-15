# X1 BOX 1.2.11 Pocket S RC8

This ARM64 Android candidate contains changes that can be checked without game
files or target hardware. It is intended for the AYANEO Pocket S on Android 13.
Actual startup, game compatibility, frame pacing and sustained performance still
need device testing. Native Linux/Steam Deck benefits are not measured here.

## RC8 uniform and texture freshness

The RC7 full settled SSX 3 runs average 42.89 FPS at 1x and 42.56 FPS at 3x.
Their primary sections average 39.71/39.10 FPS. Shader-state preparation falls
from about 1.5 to 0.7 ms per flip, but the user reports more vertex/texture
popping. Scene progression, adaptive pacing and reversed starting thermal
conditions prevent a controlled speedup claim.

RC8 repairs missing freshness checks in paths made more usable by RC7:

- Material alpha and light vectors stored outside the register arrays mark the
  existing lighting dirty flag when their bits change. Pipeline reuse refreshes
  register-backed uniforms when the register generation changes, including
  after a render pass ends. The last serialized primitive class also prevents
  stale polygon-offset uniforms when rewritten points/lines share a pipeline.
- Repeated texture setup retains a pending descriptor update until actual draw
  consumers use it. Switching between linear textures with different effective
  scales forces their shader uniform to refresh. A separate plain-descriptor
  fallback initializes its image key and avoids an invalid cache slot when
  descriptor caching is disabled; this optional path was not the logged mode.
- A revived shelved surface refreshes from authoritative VRAM because CPU writes
  while shelved have no access callback. Its allocation/image/view are reused.
  The upload transition also orders previous fragment texture reads before
  transfer writes. Active compatible surfaces retain their normal reuse path.

The tests reproduce these source defects against immutable RC7, including
actual uniform values and descriptor images. Their role in the specific SSX 3
popping events needs device confirmation. Correct freshness can require work
that the invalid fast path skipped, so a quality fix may cost some cache hits.
Required assertions, driver workarounds and resource retirement remain intact.

The RC8/RC9/RC10 test batch shares version code 39, with distinct version names.
This permits switching between its APKs using an ordinary update with the same
signing certificate and existing data. Test RC8 first, then RC9 and RC10,
keeping Turnip/options and starting temperatures comparable. Later candidates
add CPU/GPU command-efficiency and audio work on top of this correction.

## RC7 shader state and CPU work reductions

Full RC6 SSX 3 runs settle at about 40.21 FPS at 1x and 40.59 FPS at 3x.
The earlier upload changes reduce vertex-conflict finishes to about 0.5 per
flip and inclusive submission time to roughly 2 ms in the primary sections.
The common 120-second 1x comparison with RC5 is essentially unchanged at
39.80 versus 39.98 FPS. Different scene progression, adaptive pacing and a
hotter 3x start prevent attributing the other FPS differences to one change.
RC6 is committed as `8aa78b9cb878c8cc9f11388ec28bad1d6a3ef086` on
[`perf/pocket-s-performance`](https://github.com/niilo/ARMXBOX/tree/perf/pocket-s-performance).

RC7 acknowledges a shader-change notification after pipeline, descriptor and
constant consumers have used it. Previously it could remain set across
unchanged draws, repeatedly preparing pipeline keys, uniforms and descriptors.
The focused regression reproduces 51 preparation decisions for 51 unchanged
draws before the fix, versus one after it. Real shader changes, clear/skipped
draws, queued setup and fresh command buffers retain their required updates.
These operation counts do not predict an equivalent FPS increase.

Nonincrementing 16/32-bit index methods now ingest their complete payload with
one state/capacity check and length publication. Guest word order, method
consumption, tracing and DRAW_ARRAYS expansion remain equivalent for valid
input. Packed 16-bit capacity checks also reject an observed overrun at the
last slot. New `hakuX-cpu` fields `Idx16:batches/words` and
`Idx32:batches/words` report per-flip coverage; this is a small local CPU saving
whose actual device importance depends on the workload.

Contiguous uniform arrays use one bulk copy instead of repeated small copies.
Padded arrays retain their existing element loop and layout. This reduces
copy setup for the vertex constant array without changing shader values.

Required assertions, synchronization, existing Turnip options and driver
workarounds remain enabled. Host regressions and the ARM64 build establish
source behavior and packaging; RC7 gameplay FPS, sustained thermals and image
quality need the next device run.

## Vertex upload and pipeline reuse retained from RC6

RC5 device logs show that sparse vertex-mirror copying works: requested copy
work is about 0.2–0.3 MiB and 0.03 ms per flip, down from roughly 120–130 MiB
and 11 ms in RC4. Gameplay rates improved, but scene progression and active
pacing differ between runs. The user reports playable performance with some
remaining artifacts; 60 FPS has not been reached consistently.

RC6 checks uploads that overlap vertices used by recorded draws. After the
existing surface download, and only for initialized, already-uploaded mirror
ranges, it avoids rewriting identical bytes and trims unchanged boundary pages.
Actual changed bytes that overlap recorded reads retain the existing finish
and fence path. This targets unnecessary submissions without changing guest
memory semantics or driver synchronization.

Primitive-cache comparisons now use the renderer's rewritten output topology,
as shader construction already does. Comparing a raw strip/fan with a stored
triangle-list type caused repeated misses; the RC5 logs show primitive-type
mismatches account for about 41–43% of sampled first-reason fast-path misses.
Raw primitive assembly, polygon state and other cache invalidation guards
remain in effect. Reuse also rejects clear pipelines, changed shader attribute
masks and changed vertex layouts, so normalized topology alone cannot admit
incompatible state.

Immediate vertex constants also receive a value-change check, since some guest
methods update them without advancing layout generations. Changed values
refresh shader inputs; unchanged values retain cache reuse. This keeps the
newly reusable draw paths from using an earlier draw's constants.

A separate upload-range fix includes the final vertex element's full width,
including when stride is smaller than that width. It also avoids trailing
stride padding. This closes a demonstrated page-tail omission that can leave
stale vertex bytes; its role in SSX 3 artifacts remains unconfirmed.

The candidate keeps the tested Turnip options and existing rendering safeguards.
Host regressions do not establish RC6 device FPS or image quality. Compare
submission counts, upload checks/skips, geometry and gameplay performance with
RC5 on the same later course section.

The `xemu-vram` line adds `VtxCmp`, `NoOp`, `Trim` and `Avoid` counters. These
report candidate upload requests/bytes, fully skipped uploads, omitted boundary
bytes and avoided vertex-dirty finishes per flip. Comparison bytes describe
the candidate range, not exact memory-bus traffic. Compare these alongside
`xemu-work` submits, inclusive `Syn`/`Sub` timing and gameplay FPS.

## Sparse vertex propagation and attribute freshness retained from RC5

RC4's new measurements identified about 119–132 MiB of vertex-mirror copying
per guest flip, taking about 11 ms in the sampled gameplay sections. Measured
next-slot fence calls took about 0.17 ms. Both resolutions averaged about
21 FPS, with different surface-download and scene workloads, so this is not
a pure resolution-cost comparison. Vertex popping remained visible.

RC5 tracks stale pages separately for each destination submission slot. After
the existing destination fence wait, it copies contiguous runs of pages that
slot needs, retaining a full first copy and the original flush/barrier path.
Every actual mirror write marks the other slots stale. This preserves changes
across three-slot histories as well as the tested two-slot configuration.
Slot depth is captured once per renderer initialization; later requests apply
on reinitialization, avoiding unallocated or stale live slot selection.

A separate vertex layout-cache defect left RAM-backed attribute values stale
when layout registers were unchanged. RC5 refreshes decoded values on cache
hits, including stride-zero uniforms and provoking-vertex values later used
as inline constants. Changed current uniform values mark shader inputs dirty;
unchanged values retain their fast path. This fixes a reproducible source
defect, not a confirmed explanation of every SSX 3 popping event.

The `xemu-vram` counters remain: First measures initial full copies; Span now
measures steady changed-page runs. Compare requested copy bytes/time, gameplay
FPS and image quality with RC4. Existing fences, vertex read protection,
required assertions and driver workarounds remain. No device FPS gain or
60 FPS result is claimed before testing RC5.

## Cached surface reuse and measurements retained from RC4

RC3 still showed flashing/disappearing terrain or objects later in SSX 3,
mainly at 1x and occasionally at 3x. Its later gameplay sections averaged
22.76 FPS at 1x and 20.44 FPS at 3x. These sessions started hotter than the
RC2 retests, so their rates do not isolate the effect of the RC3 changes.

Source review found that the unchanged-surface texture-cache path restored
a direct image view without restoring that texture unit's image layout.
The texture cache is shared across units, so a different unit could inherit
an unset or unrelated layout. RC4 refreshes the complete direct binding and
restores depth sampling layout when needed. Driver eligibility and copy
fallbacks remain in place. This corrects a reproducible state omission;
whether it explains the remaining SSX 3 artifacts needs device confirmation.

Optional bindless descriptors track their actual image/view/sampler identity,
retire old recorded/submitted uses before an identity change, and forget
memoized views before destruction. Texture fast paths revalidate draw setup
if texture preparation submits work or ends the render pass. The device runs
used bindless OFF; it remains OFF for the first RC4 regression check.

New `xemu-vram` telemetry separates the actual next-slot fence wait from
CPU vertex-mirror copying, including the bytes/runs/time of first full copies
and later span copies. These are subregions of existing inclusive timers;
do not add them to `Fen`. Copy selection, barriers and waits are unchanged.
The measurements will guide the next performance change. No device FPS gain
or 60 FPS result is claimed for RC4.

## Vertex lifetime and timing fixes retained from RC3

The RC2 device tests booted SSX 3 using Turnip. The later gameplay sections
averaged 27.08 FPS at 1x and 22.08 FPS at 3x. The runs differ in thermal state,
scene progression and active pacing, so this is not a fixed-workload GPU cost
measurement. The user reported flashing/disappearing geometry and broken
triangles, particularly at 1x.

Source review found that already-uploaded, clean vertex pages could be read by
a recorded draw without being registered in the command buffer's read bitmap.
A later dirty upload could then overwrite those vertices before that draw
executed. RC3 registers the actual vertex ranges at final buffer binding and
reorder capture, including cached uploads and synchronization fast paths.
Existing overwrite protection, fences and driver workarounds remain enabled.
This fixes a reproducible lifetime defect; its effect on the reported SSX 3
artifacts still requires device confirmation.

Optional draw reordering also admits captured entries using the queue position
after setup, since setup can flush the previous queue. Reordering stays off in
the tested configuration.

RC3 also prevents a completed GPU timestamp query set from contributing more
than once. GPU timing changes are diagnostic corrections, not an FPS gain.
No new frame skipping, fast-fence mode or rendering shortcut is enabled.
Correctly protecting additional vertex reads may increase synchronization;
measure performance and image quality together.

## Startup fix retained from RC2

RC1 failed ten launches during `qemu_init()` while trying to open another
Android package's default `qemu.conf`. Android now passes `-no-user-config`,
skipping QEMU's desktop config search while preserving explicit TOML settings,
firmware, disks and game inputs. RC2 device logs confirmed successful SSX 3
startup and gameplay with Turnip.

Install RC8 over RC7 with the same package and signing certificate. Existing
test-app setup and files are retained. The production app remains separate.

## Changes retained from RC1

- Vulkan draw flushing captures the four values used by its existing assertions
  instead of copying the full 41,728-byte render snapshot. All assertions and
  render-thread snapshots remain intact. Optimized native compilation reduced
  the affected function's stack use from 42,712 to 1,000 bytes on the native
  host and from 43,328 to 1,632 bytes in Android ARM64 object compilation.
  Those stack comparisons use inspectable objects without LTO; the candidate
  APK separately enables ThinLTO.
- RGBA8 texture swizzling processes 2x2 blocks with one Morton-offset increment
  per block. The ARM64 NEON path remains, and the block path also works on other
  hosts. Other formats and 3D textures keep the generic conversion path.
- The TCG condition/branch optimization retains a boolean result that may be
  used after the branch and avoids rewriting comparisons with aliased operands.
  Existing liveness analysis can still remove dead boolean results.
- Android optimization controls emit explicit zero/one definitions. Their fresh
  defaults preserve the previous effective enabled behavior. Existing CMake
  caches with OFF now actually disable those paths. This candidate uses native
  float, the corrected peephole and TB cache hints enabled; no new floating-point
  shortcut has been introduced.
- **Clear system cache** now includes the Vulkan SPIR-V, pipeline and shader-key
  caches, allowing cold/warm comparisons. The driver identity file is retained.

The build keeps required assertions and the existing Qualcomm/Mali driver
workarounds. No custom driver is bundled. The tested Pocket S/Turnip combination boots SSX 3; that does not establish
compatibility with all drivers or games.

## Install

The `profile` build uses release optimization, LTO and resource shrinking, is
profileable and non-debuggable, and is signed with the local test certificate.
It appears as **X1 BOX Test**, package `com.izzy2lost.x1box.dev`, version
`1.2.11-pocket-s-rc8` (code 39). It installs alongside the production app and
uses separate app data. It shares its package with development/debug builds;
updating one of those requires the same test signing certificate.

Verify the supplied SHA-256, then open the APK on the device or use:

```sh
adb install -r x1box-1.2.11-pocket-s-rc8-arm64-v8a.apk
```

Keep the normal app installed. Select your own firmware and games in the test
app's setup flow. Do not clear the normal app's data to install the candidate.
The delivery manifest records the exact APK hash and certificate fingerprint.

## Next Pocket S session

1. Keep the same Turnip driver used for the RC2 retests. Repeat the same rider
   and downhill section in SSX 3, first at 1x and then at 3x. Continue through
   the later section that showed vertex popping in earlier runs. Do not change other
   graphics/performance options between RC7 and RC8. Warm the
   scene before measurement and keep power/fan/charging settings and starting
   temperature comparable.
2. Record whether terrain/objects still flash or disappear, whether triangles
   break, and the approximate time of any issue in the captured log. Record
   FPS alongside audio, controls and image quality. RC8 does not claim 60 FPS
   or a measured speedup on the device.
3. If SSX 3 is stable, check PGR2 and the remaining titles below. Keep driver
   comparisons and cold-cache runs separate from the first RC8 regression
   check. Extend a promising configuration to 15 minutes for sustained
   behavior. The FPS overlay does not establish emulation speed or frame-time
   percentiles.

| Game | Repeatable scene | Quality checks |
| --- | --- | --- |
| SSX 3 | Same rider and downhill route | Snow effects, terrain, transitions, music |
| Project Gotham Racing 2 | Same car, track and camera | Reflections, textures, geometry, pacing |
| Splinter Cell: Chaos Theory | Same checkpoint and night-vision sequence | Shadows, lighting, post-processing |
| Tony Hawk's Pro Skater 3 | Same skater, level and route | Textures, effects, controls, music |
| Dead or Alive 3 | Same fighters, arena and match opening | Animation, backgrounds, effects, audio |

Capture the device identity and a startup/gameplay log if ADB is available:

```sh
adb shell getprop ro.product.model
adb shell getprop ro.build.fingerprint
adb shell getprop ro.soc.model
adb logcat -v threadtime > pocket-s-rc8.log
```

Start log capture before opening the app and stop it with Ctrl-C after the run.
The startup entries identify the actual Vulkan device, driver and API version.
Report the game region/revision, test scene, cold/warm cache state, settings and
approximate time of any issue along with the log. Compare a different driver only in a later, separately recorded run.

## Reproduce cloud checks

The repository's Android toolchain and Gradle daemon JVM requirements must be
satisfied. From a fresh native configuration, `cd android && ./gradlew
assembleProfile` builds the candidate variant. Confirm the CMake output shows
the intended optimization controls; old cached OFF values are now effective.

Run the existing DSP suite from a configured native build:

```sh
meson compile -C build test-xbox
meson test -C build --suite xbox --no-rebuild
```

The additional regression fixtures are standalone and are not included in that
Meson suite:

```sh
make -C tests/tcg/optimizer check
make -C tests/xbox/swizzle check
python3 tests/xbox/vulkan/snapshot-guard.py --build-dir build
python3 tests/xbox/vulkan/snapshot-guard.py --build-dir build --thinlto
python3 tests/xbox/vulkan/vertex-read-tracking.py --negative-control
python3 tests/xbox/vulkan/vertex-read-tracking.py --sanitize
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build --thinlto
python3 tests/xbox/vulkan/surface-reuse-test.py --negative-control
python3 tests/xbox/vulkan/surface-reuse-test.py --sanitize
python3 tests/xbox/vulkan/vram-timing-test.py
python3 tests/xbox/vulkan/vertex-propagation-test.py --negative-control
python3 tests/xbox/vulkan/vertex-propagation-test.py --sanitize
python3 tests/xbox/vulkan/vertex-cache-test.py --negative-control
python3 tests/xbox/vulkan/vertex-cache-test.py --sanitize
python3 tests/xbox/vulkan/vertex-sync-test.py --negative-control
python3 tests/xbox/vulkan/vertex-sync-test.py --sanitize
python3 tests/xbox/vulkan/primitive-cache-test.py --negative-control
python3 tests/xbox/vulkan/primitive-cache-test.py --sanitize
python3 tests/xbox/vulkan/immediate-values-test.py --negative-control
python3 tests/xbox/vulkan/immediate-values-test.py --sanitize
python3 tests/xbox/vulkan/shader-consumption-test.py --negative-control
python3 tests/xbox/vulkan/shader-consumption-test.py --sanitize
python3 tests/xbox/vulkan/uniform-copy-test.py --negative-control
python3 tests/xbox/vulkan/uniform-copy-test.py --sanitize
python3 tests/xbox/pgraph/index-batch-test.py --sanitize
python3 tests/xbox/vulkan/uniform-freshness-test.py --sanitize --negative-control
python3 tests/xbox/vulkan/texture-notification-test.py --sanitize --negative-control
python3 tests/xbox/vulkan/surface-revival-test.py --sanitize --negative-control
python3 tests/xbox/vulkan/image-upload-ordering-test.py --sanitize --negative-control
python3 tests/xbox/android-startup/test-startup.py --build-dir build
```

See the fixture READMEs for compiler, baseline-comparison and coverage details.
The startup fixture must run as an unprivileged user to verify real file-access
denials. It exercises extracted production argument/options/config code without
booting an Xbox or creating an Android window.
Cloud benchmarks measure host workloads; they do not predict device FPS.
