# Next milestone: controlled graphics and PCM ownership

[Phase0.11.1 native acceptance](phase111-android-report.md) establishes one
strictly admitted XBE, one bounded typed kernel service and seven native entries.
The next milestone connects guest-produced data to host graphics/audio, retaining
all 193 checks. This document records the source audit and implementation order;
[Phase0.12a](phase12a.md) implements stage 1 below; its native gate is pending.
Rendering and playback are not implemented yet.

## Existing backend audit

The existing Vulkan backend is a useful reference but not a standalone library.
`pgraph_vk_init` in `hw/xbox/nv2a/pgraph/vk/renderer.c` requires NV2AState/PGRAPHState,
initializes worker/command/surface/shader/pipeline/texture/report/display state,
and obtains VRAM backing through QEMU MemoryRegion functions. Its synchronization
path also uses PFIFO locks, events and dirty/download state. Calling it with a
dummy device would omit memory, command and resource-lifetime contracts.

`se_frame` in `hw/xbox/mcpx/apu/apu.c` operates on MCPXAPUState, runs voice/DSP
mixing, accesses shared FIFO state and uses QEMU clock/condition pacing. It is
hardware emulation, not a host PCM sink that can simply replace a DirectSound
call. Reuse must identify which device semantics remain modeled and which
application-level services are intercepted. Existing production rendering,
DSP and audio behavior stays separate during this increment.

The proposal distinguishes XBE kernel imports from statically linked XDK
routines. A frame or audio submission must not be assigned an invented Xbox
kernel ordinal. A constructed fixture may write an explicitly diagnostic output
record into owned RAM and stop at an owned exit. That demonstrates the bridge
and consumer boundary; it does not establish NV2A, D3D8 or DirectSound coverage.
Real title interception needs verified signatures/profiles or a modeled device
submission path, with ambiguous matches rejected.

## Implementation order

1. **Guest output records and ownership.** Define bounded diagnostic vertex,
   texture and PCM records using guest addresses, explicit lengths/formats and
   sequence numbers. Reject overflow, unowned ranges, unsupported formats and
   incomplete records before host work. Build project-owned x86 payloads that
   produce deterministic data under a finite allowance. At a stopped submission
   boundary, validate and capture an owned immutable packet. Never retain raw
   guest pointers after image cleanup or publish data before CPU state is final.
   Exercise mutation-after-submit, malformed/cross-boundary ranges, cancellation,
   failed publication and bounded queue saturation with independent byte oracles.
   Keep diagnostic record IDs separate from the kernel import namespace.

2. **Offscreen Vulkan triangle and texture.** Feed the verified packet into a
   small explicit Vulkan consumer before adapting the complete NV2A renderer.
   Use Vulkan-owned staging/device memory and bounded reusable frame slots;
   ordinary guest allocations are not assumed importable. Verify a colored
   triangle and checkerboard texture via readback, including format/channel and
   orientation checks. Compare stable interior pixels and explicitly bounded
   edge tolerances with an independent CPU oracle. Compile shader sources
   reproducibly. Record actual device/driver/queue/format capabilities; a cloud
   software driver does not establish Pocket S/Turnip behavior.

3. **Presentation and PCM sink.** Add Android surface lifecycle and explicit
   user-triggered playback. Use a fixed PCM format and bounded preallocated
   producer/consumer buffers, with backpressure and clear cancellation/ownership.
   No emulation, heap allocation, logging or unbounded blocking in the audio
   callback. Keep packet memory until GPU fences/audio consumption retire it;
   no per-frame device-wide idle or per-sample JNI calls. Measure actual submitted,
   consumed and dropped frames, underruns and stop latency. Treat pause/resume,
   surface loss, activity teardown and queue-full failure as required cases.

4. **Device acceptance, then measured workloads.** Preserve all prior native
   identities. Validate payload hashes, image output, PCM duration/continuity and
   lifecycle on Pocket S. Only then expand device/XDK contracts and admit a
   controlled retail checkpoint. Introduce reproducible CPU workload and driver
   measurements before optimization claims; do not label a synthetic present
   counter as game FPS. For title comparisons retain the same scene, power mode,
   resolution and driver, and capture frame-time distributions, CPU/GPU work,
   audio underruns and sustained thermal behavior.

## Efficiency constraints and scope

Copy or serialize once per submitted packet, with bounded reusable storage and
an explicit retirement point. Avoid per-instruction state copying, host calls,
per-draw allocation, synchronous GPU readback in ordinary presentation and
busy-waiting audio. Keep guest time distinct from host presentation/audio clocks;
this output fixture must not imply that virtual Xbox timing is solved.

Cloud work can implement the format/ownership checks, generated payloads,
independent oracles, malformed/lifetime controls and offscreen consumer tests.
Native driver, presentation, playback/lifecycle and thermal acceptance require
device reports. This is an integration milestone, not a claim that a new GPU
backend or sound emulator is needed, nor a measured FPS improvement.
