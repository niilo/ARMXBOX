# Phase0.12a: guest output packets and ownership

This increment implements stage 1 of the [graphics/PCM plan](phase12-plan.md).
The generated IA-32 XBE produces vertices, a texture and PCM, then stops at an
owned exit. Host validation captures immutable packets into two reusable slots.
It does not render, play sound, load retail games or measure game FPS.
Production ARMXBOX and the pinned engine plus nine patches are unchanged.

## Record and publication contract

The diagnostic OUT1 record has a 64-byte little-endian descriptor with version,
sequence, guest addresses, sizes, formats and a final CMIT marker. It describes
three binary32 XY vertices (24 bytes), a 4x4 RGBA8 checker (64 bytes) and 64 mono
S16LE frames at 48 kHz (128 bytes). These are diagnostic records, not Xbox kernel
ordinals or an implementation of D3D8/DirectSound interception.

The Output XBE profile is independently admitted at 0x11200. Its 706-byte,
72-instruction program withdraws the old commit, writes the EAX sequence and
payloads, commits last and halts at 0x114c1. The original Fill profile remains
unchanged. Completed cold and warm entries consume 72 of 4096 instructions.
A three-instruction budget stops at 0x11219 without a commit; a latched stop
prevents entry and publication.

The trusted adapter checks stopped completion metadata before capture. The queue
rejects cancellation, stale generations, incomplete/unsupported descriptors,
replayed sequences, bounds/alignment errors, overlapping ranges and nonfinite or
out-of-range vertex coordinates before copying. It copies the 216 payload bytes
once into an unpublished slot, then publishes sequence/count. No guest pointer
survives capture. Saturation fails before guest reads. Failed submissions leave
queue state intact.

The queue is externally serialized, with one active consumer lease. Owner,
generation, slot and sequence identify leases; FIFO retirement and drained-only
generation reset reject stale access. It is not a concurrent audio callback API.
Borrowed packet access ends at retirement or queue destruction. A probe destroys
the producer context and mappings, replaces the guest range with a canary, and
verifies both captured packets remain intact.

## Verification and device gate

Cloud simulator runs pass **182 checks** in normal, single-block and forced
hint-only modes. The packet unit suite passes **52 controls**, including 4000
seeded mutations, rollback, saturation and lifetime checks. Image tests retain
all Fill checks and reject truncated Output files and all 706 single-byte code
mutations. Packet and image suites pass ASan/UBSan; leak detection is disabled
because the managed runtime cannot support its ptrace requirement. Deliberately
removing commit validation, corrupting the copied texture or removing capacity
validation causes the unit suite to fail.

An independent Python literal oracle and GNU assembler verify the descriptor,
triangle, checker, PCM and all 72 instructions. Output XBE SHA-256:
`3d86ac283d9264664e72aee31b3bd6117851b4663ae83c690f97400ff44ecdb8`.
Canonical wire fixture SHA-256:
`6b0c1ed9d42078303484985b568eb3c66caf899927b6654edeb72e5a5a4e9cbe`.

The candidate is **phase0.12a/code16**. The [Pocket S report](phase12a-android-report.md) accepts this scoped increment:
expect **206 unique PASS checks**, four output entries with all six ABI witnesses,
seven Fill XBE entries, 113 policy entries and 27 native bridge entries.
All 193 [Phase0.11.1](phase111-android-report.md) native identities remain clean. Host simulation and APK alignment do not establish native output
execution or 16 KiB runtime support.

Next is the offscreen Vulkan consumer and independent pixel oracle, then Android
presentation/PCM lifecycle and device acceptance. Keep that work directed toward
the minimum contracts for SSX 3 startup; synthetic throughput is not game FPS.
