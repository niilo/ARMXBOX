# FEX-first Xbox application runtime experiment

Branch: `experiment/fex-hle-runtime`
Starting point: stable `master` / v1.2.14,
`b524318725b3e0053a5b267b579cbb643550cf61`.

This branch starts the application-level IA-32-to-AArch64 Xbox runtime described
in the [user-supplied proposal](proposal.md). The RC16 and RC17 optimization
experiments are paused and preserved on their existing branches. Their
unvalidated runtime changes are not part of this starting point.

**Status: Phase 0 implementation underway.** [Phase0.3](phase03.md) fixes
IA-32 divide errors with hardware-checked arithmetic matrices. [Phase0.2](phase02.md) adds
controlled generated-exception exits and native ABI witnesses; the
[Pocket S report](phase02-android-report.md) passes all 21 checks. The pinned FEXCore builds and links
for Android ARM64, and the isolated CPU diagnostic passes all ten checks both
in the cloud simulator and in the [user Android report](phase0-android-report.md).
Basic native Android execution on 4 KiB pages is demonstrated. A separate probe
APK and CI workflow are implemented. Remaining fault/ABI/memory gates and
performance remain unverified;
broad HLE has not started. See [findings and gates](phase0-findings.md) and the
[probe build/run instructions](../../../experiments/fex-runtime/README.md).

## Objective and constraints

Execute original Xbox IA-32 code inside a normal Android ARM64 app process,
using an embedded DBT engine plus Xbox-specific kernel/HLE and selected device
command handling. FEXCore is the first candidate, subject to demonstrated
Android, memory, ABI and exit contracts. Box32 and suitable TCG reuse remain
alternatives when supported by a concrete blocker assessment.

Keep the working full-system emulator available as the compatibility/reference
backend. Reuse Vulkan/NV2A, audio, UI and storage components only through audited
interfaces. Vulkan is the host graphics backend, not Xbox semantics; Oboe is an
output sink, not a replacement for the guest mixer, APU or DSP. Kernel ordinal
imports and statically linked XDK functions require separate interception paths.

No rootfs, proot, Wine frontend, root permission or Android container is a runtime
requirement. Existing cloud build containers are development tools only. Retain
C++17 for existing code; scope any C++20 requirement to pinned FEX/adapter targets.
Do not create a parallel custom optimizing JIT or claim a fixed FPS gain.

## Phase 0: first implementation scope

The linked findings distinguish completed cloud checks from open gates. Do not begin broad kernel,
graphics or audio HLE before the CPU/bridge/memory gates are demonstrated.

1. **Baseline and dependency decision.** Inspect the current build graph and
   capture the exact upstream FEX commit, dependency/license inventory, C++20/NDK
   requirements and required portability patches. The supplied proposal refers
   to a pinned FEX commit but includes no hash; resolve and record it before
   fetching/building an embedded dependency. Do not invent or silently float it.
2. **Isolated Android CPU fixture.** Add explicit opt-in targets outside existing
   source globs. Compile the minimal embedded engine configuration in an APK and
   execute generated IA-32 fixtures directly on Android ARM64. A Linux/proot run
   or host-only build does not satisfy this gate.
3. **Typed bridge and callbacks.** Demonstrate an Xbox-style guest/native call,
   exact return/stack cleanup, guest GPR/FPU preservation, nested reverse callback,
   controlled faults and run/resume. Include x87, MMX/SSE, flags and FS/TLS cases;
   expose only the intended Xbox CPU feature set.
4. **Memory and code publication.** Prove actual generated-access mapping rules,
   low-address placement or supported guest-base translation, aliases, 4 KiB guest
   versus 4/16 KiB host pages, device exits, guest-PC recovery and code invalidation.
   Verify Android reserved x18, AAPCS64 stack alignment, TLS/signals, cache
   synchronization and executable publication on the device. A region-lookup API
   alone does not redirect JIT-emitted loads/stores.
5. **Architecture decision record.** Record implemented versus unsupported
   contracts, engine patches, ownership/error boundaries, dependency costs and
   the decision to retain FEXCore or evaluate an alternative. Do not fabricate
   success for an unsupported operation.

Cloud checks may establish compilation, emitted-code structure and host fixture
correctness. Android execution, sustained thermal behavior and title performance
need the device. Preserve those distinctions in every increment.

## Subsequent milestones

- **B:** controlled XBE loader, architectural state, guest memory/TLS and minimum
  kernel imports; deterministic execution and exit.
- **C:** verified hook/ABI metadata and scheduler, then a controlled path that
  renders a triangle/texture and plays a PCM loop with measured timing.
- **D:** a selected retail title reaches a documented gameplay checkpoint with
  explicit missing-service, instruction, raw GPU and audio-feature coverage.
- **E:** repeatable scenes show measured improvement over the reference backend
  without unacceptable rendering, audio, timing or correctness regressions.

The full proposal defines the loader, typed bridge, memory, CPU, hybrid graphics
and audio contracts. Use generated fixtures, homebrew and user-supplied assets;
redistribution of commercial games or proprietary SDK libraries is not required.

## Recorded repository baseline

| Area | Existing starting point |
| --- | --- |
| Android | ARM64, minimum API 29, compile SDK 36 |
| Native tools | NDK 30.0.15729638; CMake 3.22.1 |
| Language | Android native C++17 |
| Full-system core | xemu/QEMU with existing TCG and lazy-condition-code machinery |
| Graphics | Existing NV2A-to-Vulkan backend under `hw/xbox/nv2a/pgraph/vk/` |
| Loader research | `xemu-xbe.c` inspects a running emulated image; it is not a standalone loader |
| Build integration | `android/app/src/main/cpp/CMakeLists.txt` recursively globs selected core directories |

Place future runtime sources under a dedicated directory, such as
`experiments/fex-runtime/`, with explicit target membership. Recheck actual glob
coverage before adding code; do not put experimental files into existing `hw`,
`target`, `ui` or root source globs accidentally. The Phase 0 probe now lives there as a separate explicit CMake project.

The recent SSX 3 3x Control capture is a useful user reference, not a complete
repeatable Phase 0 benchmark: first38 settled reports averaged 38.12 logged FPS,
with increasing temperature and severe thermal status later. RC16/17 were hotter
and did not establish a causal speedup. The user corrected all three runs to
report slight audio crackling. Future profiling must measure audio underruns and
callback/producer timing rather than treating missing error logs as clean sound.
Do not copy private raw device logs or game paths into this public branch.

## Initial branch verification

The original proposal is retained byte-for-byte. Its SHA-256 is
`60638505b97a491586df1cfd5de310d5483ca5a00892988ada705cdfc9ba2aa6`.

This initialization changes documentation only. Check whitespace, the proposal
checksum and the diff against the pinned master baseline. Emulator builds and
prototype/device tests are not claimed for this commit.
