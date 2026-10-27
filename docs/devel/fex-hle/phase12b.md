# Phase0.12b: native guest packets to offscreen Vulkan

[Phase0.12a](phase12a-android-report.md) now passes all 206 expected native
checks. This step adds an independent explicit Vulkan consumer under
`experiments/fex-runtime/graphics`, outside the FEX and production build graphs.
It consumes owned OutputPacket snapshots, renders a 64x64 textured triangle,
and verifies pixels through readback. The phase0.12b/code17 candidate now connects
this consumer to the accepted guest producer; its native GPU acceptance is pending.

## Implemented path

The test captures a host-constructed diagnostic record through OutputQueue,
then overwrites its source bytes. The Vulkan consumer uploads the owned vertex
and RGBA8 data, samples a 4x4 texture with nearest filtering and draws one
triangle. GPU input is copied into persistently mapped coherent staging storage;
guest/host packet allocations are not imported into Vulkan.

One reusable GPU slot owns its buffers, image memory, sampler, descriptor,
pipeline, command buffer and fence. Allocation and shader/pipeline creation
happen during initialization. Each render waits on the slot fence, updates
staging data, records commands and waits for readback completion. Texture
layout transitions and transfer/shader/color/host access dependencies are
explicit. No per-frame device-wide idle or Vulkan-object allocation occurs.
Readback and serialized fence waits are diagnostic behavior, not the eventual
presentation path or a throughput optimization. Device-wide idle is used at
final teardown; bounded cleanup after a driver hang is not established.

GLSL sources are compiled reproducibly for Vulkan 1.0 and checked with spirv-val.
The cloud test requires Khronos validation, enables synchronization validation,
and checks errors through final resource destruction. Device/driver/API, queue
family and RGBA8 format capabilities are printed. Unsupported capabilities
fail rather than silently bypassing the test. Host-visible coherent and
device-local memory types are currently required.

## Reference and controls

A separate scalar edge-function/nearest-sample oracle compares all RGBA channels.
It excludes only points within one pixel of triangle edge lines (including their
extensions), and requires over 3000 of 4096 pixels compared per image. This
explicit tolerance handles raster fill-rule differences; it is not a complete
rasterization conformance test. Background clear, channel values and alpha are
checked. Alternating canonical and inset triangles with asymmetric texture
colors expose ignored vertices, stale textures, orientation and channel errors.
The repeated canonical frame must match the first readback byte-for-byte.
PPM and full RGBA evidence images are saved for the first two submissions.

Four packets pass on the cloud llvmpipe software driver, with zero Vulkan or
synchronization validation errors. The application code also passes ASan/UBSan
with leak detection disabled under the managed runtime. Corrupted readbacks
(channel swap, vertical flip, missing copy and missing draw) fail the oracle.
Executable negative controls compile valid but wrong shaders: channel swizzle
and ignoring the supplied vertices. Each actual GPU run fails at pixel validation.
The dedicated `FEX offscreen output consumer` workflow repeats rendering,
shader validation and the executable negative controls, and uploads evidence.

## Native integration and acceptance gate

The reusable consumer accepts immutable packet references and build-time embedded,
validated SPIR-V. Native startup requires no shader files/compiler or validation
layers. Runtime validation is disabled explicitly; a passing native report must
not be described as a validation-layer run. The exceptions-enabled adapter catches
all C++ exceptions before returning to the exceptions-disabled CPU diagnostic.
The bounded details include actual device, driver, Vulkan API, queue and format
capabilities, plus the failure stage when applicable. Persistent journal entries
precede driver initialization, submissions and cleanup.

After the producer thread/context/image have been destroyed, its address range is
replaced with a canary. Only an intact owned packet can reach the first synchronous
GPU callback. It returns after its fenced readback, before that packet's lease is
retired. The second callback runs after replacement memory is unmapped; the same
GPU slot uploads and renders the second owned guest packet. Readbacks must match
the scalar oracle and each other. A separate, clearly host-constructed asymmetric
texture/inset triangle control checks orientation, alpha, channels and vertex input
on the actual driver. Explicit resource cleanup completes before the second
callback returns and its guest packet lease retires. No borrowed packet or guest
pointer is retained across callbacks.

**Expected native result: 213 unique PASS checks**: retain all 206 accepted CPU
identities and add seven GPU checks for packet delivery, initialization, cold/warm
pixel results, identical canonical repeat, the asymmetric host control and explicit
cleanup. Four guest output entries, seven Fill XBE entries, 113 policy entries and
27 native bridge entries remain unchanged. This candidate uses the **system Vulkan
loader**, not production's custom Turnip loader. Report the actual driver rather
than assuming that installing Turnip selected it for this isolated probe.

The optional host-simulator GPU build exercises the same guest producer and adapter:
189 checks (182 CPU plus seven GPU). An unavailable-ICD run must fail the seven GPU
checks while retaining all 182 CPU outcomes, with a readable error instead of a
C++ exception escaping into the CPU frame. A separate bridge test injects renderer
initialization, cold/warm render, pixel-reference and cleanup failures without a
GPU, testing orchestration and exception boundaries. Default CPU-only host runs
remain 182 checks in all three modes; the Android candidate compiles GPU support.

## Next milestone and limits

The code17 device report is needed to accept native driver initialization, guest
packet rendering and cleanup. Cloud software-driver success does not establish
Pocket S behavior. Driver hangs can still block final device-idle cleanup; the
three-second fence waits do not establish bounded driver teardown. If the app
closes or hangs, reopen and share the saved progress before rerunning.

Next add Android surface presentation and the bounded PCM sink/lifecycle tests
from the [plan](phase12-plan.md). This increment is an offscreen diagnostic:
no PCM is played and no retail loading, game rendering, native GPU performance,
surface lifecycle or FPS improvement is established. Keep remaining work focused
on minimum SSX 3 startup services rather than unrelated CPU fixture expansion.
