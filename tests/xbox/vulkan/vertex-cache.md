<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Vertex binding cache values

Run without Vulkan headers, firmware, games or a GPU:

```sh
python3 tests/xbox/vulkan/vertex-cache-test.py
python3 tests/xbox/vulkan/vertex-cache-test.py --sanitize
python3 tests/xbox/vulkan/vertex-cache-test.py --cc clang-21
python3 tests/xbox/vulkan/vertex-cache-test.py --negative-control \
    --baseline-ref f75153dddd03880c0c6fbfea93b2dea8ceb29af9
```

The runner extracts the production Vulkan vertex binder, its format tables
and value-update helper, plus the common inline-value decoder and getter.
Small host definitions replace Vulkan types, profiling and DMA address
mapping. Each case compares a cache-enabled environment with an independent
copy forced through the existing cold layout path. No decoder or cache
implementation is duplicated in the fixture.

Thirteen scenarios cover all six vertex data formats, mixed strided and
stride-zero attributes, changed guest RAM without a generation change,
unchanged decoded uniforms, changed provoking vertices, nonzero minimum
indices, later count-zero latching, format/offset/DMA generation changes,
disabled immediate attributes and a layout containing only RAM constants.
The checks compare decoded bytes, attribute masks, descriptors and actual
uniform-getter output. Changed current uniform values must set the dirty
flag; unchanged values and changed nonuniform latches must keep it clear.

The optional negative control extracts the original binder from the selected
pre-fix Git revision. It must fail because cached inline values differ from
the forced-cold path after guest RAM changes. The default is immutable pre-fix revision
`f75153dddd03880c0c6fbfea93b2dea8ceb29af9`; use `--baseline-ref REV` to override.

`--sanitize` requires address/undefined sanitizer runtimes for the selected
compiler. The compact cloud image lacks Clang's sanitizer static archives;
host GCC can run that configuration. Temporary sources and binaries are
removed automatically. This standalone test is outside the Meson Xbox suite.

The fixture validates the binder's value and dirty-state contract. It does
not exercise Vulkan command execution, guest method dispatch, DMA objects
modified in place, surface downloads or actual SSX 3 scenes. It establishes
a source regression independently of whether this was the reported popping.
