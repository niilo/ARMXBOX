<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Direct-surface cache reuse

Run without Vulkan headers, a GPU, firmware or games:

```sh
python3 tests/xbox/vulkan/surface-reuse-test.py
python3 tests/xbox/vulkan/surface-reuse-test.py --sanitize
python3 tests/xbox/vulkan/surface-reuse-test.py --cc clang-21
python3 tests/xbox/vulkan/surface-reuse-test.py --negative-control \
    --baseline-ref f75153dddd03880c0c6fbfea93b2dea8ceb29af9
```

Each run builds with `OPT_BINDLESS_TEXTURES=0` and `=1`. The enabled build
also exercises runtime-disabled bindless support. `--sanitize` requires the
selected compiler's address/undefined sanitizer libraries; the compact cloud
Clang image lacks its sanitizer static archives, so host GCC supplies that
coverage. Temporary extraction files and binaries are removed automatically.
This standalone fixture is not part of the Meson Xbox suite.

The runner extracts the actual cached-source branches, color/depth preparation
functions, shared direct-binding helper, bindless descriptor identity updater,
view-lifetime invalidator and binding read reservations from `texture.c`. It
also compiles the real descriptor-cache input constructor from `shaders.c`
and the post-texture-binding fast-path guard from `draw.c`. Small Vulkan and
submission fixtures record descriptor inputs, barriers, open draws and waits.

Coverage includes:

- A cache node shared by two texture units, with incorrect prior unit layouts.
- Color and depth images, zero/unchanged timestamps and repeated same-unit use.
- Cached color reuse preserving its no-barrier/no-descriptor-write fast path.
- Restoring depth read-only layout after attachment rebinding.
- A used bindless slot changing from normal texture to surface, back to normal
  texture, or to another surface view. Open and submitted readers must retire
  before a changed descriptor is written; identical identities retain readers
  and avoid extra writes or waits.
- A retained, unbound cache node surviving surface-view destruction, followed
  by a new view with identical raw handle bits. Its descriptor must be written
  again. Unrelated descriptor identities remain valid.
- Post-bind recording/framebuffer/descriptor/uniform invalidation. The real
  guard rejects a fast return after image preparation ends the render pass or
  a descriptor identity change retires the open command buffer.
- Driver eligibility returning false and keeping the existing copy fallback.

There are eight scenarios in the compile-disabled variant and fifteen in the
compile-enabled variant, with multiple assertions and state mutations per
scenario. The optional negative control extracts the original cached branch
and descriptor constructor from the chosen Git revision. It must reproduce
stale per-unit descriptor inputs in both builds. Select a pre-fix revision
after committing the change; `HEAD` is only a convenience default while the
fix remains uncommitted.

These fixtures verify source state transitions and API inputs. They do not run
Vulkan, validate a real driver's synchronization, replay the full draw pipeline
or establish that this omission caused every SSX 3 artifact. Full Android
translation-unit compilation and device comparison remain separate checks.
