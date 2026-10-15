<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Vertex synchronization footprints

Run without Vulkan headers, a GPU, firmware or games:

```sh
python3 tests/xbox/vulkan/vertex-sync-test.py
python3 tests/xbox/vulkan/vertex-sync-test.py --sanitize
python3 tests/xbox/vulkan/vertex-sync-test.py --cc clang-21
python3 tests/xbox/vulkan/vertex-sync-test.py --negative-control \
    --baseline-ref f75153dddd03880c0c6fbfea93b2dea8ceb29af9
```

The runner extracts the real RAM binder, dirty-page scanner, sync merger,
upload writer and its bookkeeping helpers. It reuses the binding-cache
fixture's host type definitions and actual common decoder. The production
Vulkan format-width getter independently checks each bound attribute's byte
width. Bitmap/RCU/DMA definitions and surface downloads are small host stubs;
the actual sync code determines which guest RAM bytes reach the upload buffer.

Nineteen cases cover cold and cached bindings with overlapping strides in
float, normalized byte/short and scaled short formats, including the S32K
integer fallback. Only the omitted tail page is dirty in those cases; bytes
must reach the upload buffer even when the preceding page is already clean.
Other cases cover padded strides without consuming an unrelated dirty page,
packed CMP's four-byte width, nonzero starting vertices ending exactly at the
RAM boundary, repeated clean reuse, and stride-zero uniforms with no GPU
vertex upload request. These tests retain the real writer's hazard check;
they do not replace copies with an oracle that merely inspects requested sizes.

The optional negative control compiles the original binder/sync/writer from
a pre-fix Git revision. It must fail because a consumed tail byte remains
stale in the upload buffer. Choose a revision before the correction after
committing it; the default `HEAD` is convenient only before committing.

The sanitizer run needs address/undefined sanitizer runtimes for the chosen
compiler. Host GCC provides those in the cloud environment; the compact
Clang image lacks its sanitizer static archives. Temporary sources and
binaries are removed automatically. This standalone fixture is outside the
Meson Xbox suite.

This validates upload bytes and range selection using extracted production
code. It does not execute Vulkan, surface aliases or driver synchronization,
and it does not establish which layouts SSX 3 uses or measure game speed.
