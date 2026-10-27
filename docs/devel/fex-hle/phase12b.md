# Phase0.12b cloud step: offscreen Vulkan packet consumer

[Phase0.12a](phase12a-android-report.md) now passes all 206 expected native
checks. This step adds an independent explicit Vulkan consumer under
`experiments/fex-runtime/graphics`, outside the FEX and production build graphs.
It consumes owned OutputPacket snapshots, renders a 64x64 textured triangle,
and verifies pixels through readback. There is no new Android APK in this step.

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

## Next integration

This test uses a host-constructed captured packet. It does not yet feed a native
FEX-produced packet into Vulkan or exercise Android/Turnip. Next, extract the
consumer into a reusable component, keep validation optional outside cloud tests,
embed checked SPIR-V at build time, connect the accepted guest producer and
surface driver/capability/fence/readback results in the Android probe. Preserve
the accepted 206 CPU identities. Then add surface presentation and the bounded
PCM sink/lifecycle tests from the [plan](phase12-plan.md).

No PCM is played and no retail loading, game rendering, native GPU performance,
surface lifecycle or FPS improvement is established. Keep remaining work focused
on minimum SSX 3 startup services rather than unrelated CPU fixture expansion.
