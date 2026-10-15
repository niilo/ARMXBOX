<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Texture descriptor and scale freshness

`texture-notification-test.py` checks pending texture changes through dirty
memory polling, repeated texture binding, descriptor preparation and drawing.

```sh
python3 tests/xbox/vulkan/texture-notification-test.py --sanitize \
    --negative-control
python3 tests/xbox/vulkan/texture-notification-test.py --cc clang-21 \
    --negative-control
```

The negative control defaults to immutable RC7 revision
`d2da241bab37c6460fc8832e8ded75d42362e739`; override `--baseline-ref` only with
another revision containing the corresponding production functions.

The first reproduction changes `SET_TEXTURE_OFFSET` from cached image A to
cached image B while A's guest RAM is dirty. The real dirty poll binds B;
pipeline preparation binds textures again. RC7 clears the pending notification
at the second bind and leaves the actual bound descriptor referring to A.
Keeping the notification until draw-state consumption delivers B. The fixture
also exercises pending changes across repeated setup, async shader unavailability,
clears, skipped setup and command-buffer resets. A real descriptor cache hit
consumes the pending change at the draw, and 50 unchanged repeats do not write
additional descriptor sets.

A separate reproduction switches a linear texture from effective scale 3 to 1.
The real PSH `texScale` getter, post-bind fast-return guard and descriptor UBO
upload check that the new value reaches staging. Cases include unchanged
scales, nonlinear formats, disabled/dummy units, initially NULL bindings,
preexisting uniform dirtiness and a recycled cache node with the same pointer
but a new sequence number. Snapshots taken before lookup catch node recycling.

The runner compiles with bindless support both disabled and enabled, exercising
plain, push and optional bindless descriptor modes. It executes the complete
production descriptor update, cache reuse and descriptor bind/push functions.
Disabling the plain descriptor cache still produces current images and leaves
the descriptor cache unchanged. RC7's separate cache-OFF control fails on invalid
descriptor images before its invalid cache-slot store; the fixed path passes
with address and undefined-behavior sanitizers. Automatic variable pattern
initialization makes that old uninitialized-key failure deterministic.
An additional isolated control initializes only RC7's descriptor key, then
verifies that its remaining `cache[-1]` store corrupts a sentinel before the
cache array. This separates the two old defects without masking either one.

The fixture extracts production register writes, the offset method, dirty
polling, the full texture binder, cached-node selection, descriptor preparation,
the final nonclear draw-state consumer, the scale getter and fast-return guard.
The allocator/key lookup is a deterministic lookup of two existing cache nodes.
Vulkan calls, memory dirty bits, uniform serialization and buffer allocation are
test boundaries. The test checks descriptor identity and bytes passed to UBO
staging; it does not run a GPU, shader compiler, game or device. Existing direct
surface tests cover the separate alias/view and layout contracts. No FPS or
confirmed SSX-specific trigger is inferred from these reproductions.
