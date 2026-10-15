<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Vertex read lifetime and reorder admission

Run without Vulkan headers, a GPU, firmware or games:

```sh
python3 tests/xbox/vulkan/vertex-read-tracking.py
python3 tests/xbox/vulkan/vertex-read-tracking.py --sanitize
python3 tests/xbox/vulkan/vertex-read-tracking.py --cc clang-21
python3 tests/xbox/vulkan/vertex-read-tracking.py --negative-control \
    --baseline-ref f75153dddd03880c0c6fbfea93b2dea8ceb29af9
```

`--sanitize` needs the selected compiler's address/undefined sanitizer runtimes.
The compact cloud image lacks the Clang sanitizer static archives; host GCC
can run the sanitizer case. Temporary binaries and extracted functions are
removed automatically. This standalone test is not in the Meson Xbox suite.

The runner extracts the actual production vertex binding, reorder snapshot,
RAM upload and read-registration functions from `draw.c` and `vertex.c`. It
also extracts the actual reorder admission block from `pgraph_vk_draw_end()`.
Vulkan types and submissions are replaced with small fixtures. A deferred
draw retains a reference to its vertex buffer; fake submission reads its bytes
only when `finish()` retires it, reproducing the original overwrite failure.

Seven vertex scenarios cover already uploaded clean reads, a finish after
sync but before binding, normal and queued bindings, unrelated dirty pages,
multiple attribute layouts, page-crossing final elements, explicit bound
offsets, nonzero starting vertices and inline/remapped attribute exclusion.
Five admission cases cover successful array/indexed captures with and without
a finish resetting the window, plus failed captures with and without reset.

The optional negative control compiles the original functions and admission
block from a chosen Git revision. It must reproduce both changed vertex bytes
at deferred submission and a draw admitted into a stale entry after capture
resets the window. The default is immutable pre-fix revision
`f75153dddd03880c0c6fbfea93b2dea8ceb29af9`; use `--baseline-ref REV` to override.

The fixtures test ownership decisions and byte preservation. They do not
execute Vulkan command buffers, prove driver synchronization or establish that
every SSX 3 artifact has the same cause. Full translation-unit compilation and
device replay remain separate validation.
