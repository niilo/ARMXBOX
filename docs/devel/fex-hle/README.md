# FEX-first Xbox application runtime experiment

Branch: `experiment/fex-hle-runtime`
Starting point: stable `master` / v1.2.14,
`b524318725b3e0053a5b267b579cbb643550cf61`.

This branch starts the application-level IA-32-to-AArch64 Xbox runtime described
in the [user-supplied proposal](proposal.md). The RC16 and RC17 optimization
experiments are paused and preserved on their existing branches. Their
unvalidated runtime changes are not part of this starting point.

**Status: Phase 0 implementation underway.** The [Phase0.4.1 Pocket S report](phase041-android-report.md)
passes all 56 native checks, including the corrected PC/flag recovery and scoped
word-MMIO service/resume. The previous [Phase0.4 failures](phase04-android-report.md)
are retained as regression evidence. [Phase0.5](phase05.md) adds a bounded host allocator
and host coexistence checks; the [Pocket S report](phase05-android-report.md)
passes all 75 native checks. Full bridge/memory,
[Phase0.6](phase06.md) adds reference-backed CPUID identity; the
[Pocket S report](phase06-android-report.md) passes all 88 native checks. [Phase0.7](phase07.md) adds precise privileged-exit checks and fixes reserved
high bits in EFLAGS reconstruction; the
[Pocket S report](phase07-android-report.md) passes all 109 native checks.
[Phase0.8](phase08.md) implements opt-in instruction budgets and cooperative
stop for constructed loops; the [Pocket S report](phase08-android-report.md)
passes all 129 native checks. [Phase0.9](phase09.md) implements a constructed bounded exit-based bridge
and callback diagnostic; the [Pocket S report](phase09-android-report.md)
passes all 148 checks. [Phase0.10](phase10.md) adds an opt-in reference-backed instruction rejection
subset. After a native allocation regression, [Phase0.10.2](phase102.md) passes
[all 177 native checks and 113 policy entries](phase102-android-report.md).
This accepts the scoped increment on Pocket S. [Phase0.11](phase11.md) now
implements a strictly admitted generated XBE, one typed kernel import and finite
execution. Its [native report](phase11-android-report.md) stops at mapping setup;
[Phase0.11.1](phase111.md) now passes [all 193 native checks](phase111-android-report.md),
including seven XBE entries and their ABI witnesses. The next scoped milestone
is the [graphics/PCM ownership and integration plan](phase12-plan.md).
[Phase0.12a](phase12a.md) now implements guest-produced immutable output packets;
182 simulator checks pass and the [Pocket S report](phase12a-android-report.md)
passes all 206 expected native checks. The broader
[virtual-state plan](phase10-plan.md) remains open. General execution,
callback/fault, instruction/privilege and timer contracts remain open.
Broad HLE has not started. See the
[remaining embedding gates](next-gates.md) and [build/run instructions](../../../experiments/fex-runtime/README.md).

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

See the [remaining embedding gates](next-gates.md) for source-backed allocator,
CPU-policy and bounded-execution work before broad integration.

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
