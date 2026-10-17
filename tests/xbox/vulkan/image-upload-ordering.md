<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# GENERAL image upload ordering

```sh
python3 tests/xbox/vulkan/image-upload-ordering-test.py --negative-control
python3 tests/xbox/vulkan/image-upload-ordering-test.py --sanitize --negative-control
```

The standalone fixture extracts the complete production image-transition
function and its format helpers. It uses actual Khronos Vulkan types from the
repository's SDL vendor headers; pass `--vulkan-include /usr/include` to use
installed Vulkan headers instead. A call-capturing `vkCmdPipelineBarrier` stub
records the full scopes and image barrier without loading a Vulkan driver.

A color surface in GENERAL can have prior fragment-shader texture reads as
well as attachment reads/writes. An upload that reuses that image must order
those reads before the transfer overwrite. The fix adds fragment-shader and
shader-read scope to GENERAL-to-TRANSFER_DST while retaining the previous
attachment and transfer scopes. Shelved-surface revival now refreshes the
allocation from authoritative VRAM, making this upload ownership significant.

Tests check all 32 supported transition pairs across five color/depth/stencil
formats, including exact source/destination stage and access flags, queue
families, image handle, aspects and complete mip/layer ranges. Identity
transitions emit no barrier or transition counter. In particular, existing
SHADER_READ and DEPTH_READ_ONLY uploads, and TRANSFER_DST-to-GENERAL, retain
their previous scopes. The format/layout matrix checks the helper's branch
contract; it is not a claim that every format/layout combination is a legal
Vulkan image usage.

The immutable RC7 negative control must fail specifically for the missing
fragment-read scope, then pass every unchanged transition branch. No compiler
failure counts as detecting the bug. Source extraction does not prove the
renderer's full GPU lifetime: these tests inspect emitted barriers, execute no
Vulkan commands and do not replace device validation or gameplay comparison.
