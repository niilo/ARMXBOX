<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Immediate draw constants

Run without Vulkan headers, a GPU, firmware or games:

```sh
python3 tests/xbox/vulkan/immediate-values-test.py
python3 tests/xbox/vulkan/immediate-values-test.py --sanitize
python3 tests/xbox/vulkan/immediate-values-test.py --cc clang-21
python3 tests/xbox/vulkan/immediate-values-test.py --negative-control \
    --baseline-ref f75153dddd03880c0c6fbfea93b2dea8ceb29af9
```

The runner extracts five real generic `SET_VERTEX_DATA*` setters and the
fixed-function diffuse setter/macro, the common inline history allocator,
both production binders, the current-value decoder/getter and the actual
push-constant writer. It also executes the SFP uniform-rejection clause,
the pipeline early-hit block and replay invalidation assignments extracted
from production. Host type definitions are shared with the binding-cache
fixture; no setter, binder or value-copy algorithm is reimplemented.

Twenty-one scenarios exercise float, signed-short and packed-byte setters with
both ordinary array and buffered-immediate draws. Changed/new constants must
mark uniforms dirty without changing the array generation; identical values
keep the clean path, and an already pending dirty flag remains set. The real
push writer must expose the current getter output. The pipeline early hit
must dispatch the uniform refresh before a subsequent descriptor upload;
a stub records that call and packs the real getter output into a simulated
CPU uniform allocation. The full shader uniform updater is outside this test.

Other cases cover RAM array enable/disable transitions, buffered uniforms
whose array count is nonzero, populated streams whose array count is zero,
ordinary streamed values, actual packed/buffered binder layout transitions
without generation changes, signed-zero and NaN payload bytes, merged replay
and captured reorder constants emitted without another binder observation.
Replay tests execute the actual invalidation assignments; they do not run
the complete optional queue machinery. Primitive and descriptor prerequisites
are exercised separately by `primitive-cache-test.py`; this fixture holds a
valid POINTS shader binding and extracts only SFP's uniform rejection clause.

The negative control compiles the chosen pre-fix binder/setters. It must
reproduce an immediate change escaping that rejection clause. The default is immutable pre-fix revision
`f75153dddd03880c0c6fbfea93b2dea8ceb29af9`; use `--baseline-ref REV` to override. The fixture compiles with assertions and QEMU's
`-fno-strict-aliasing` convention for existing float method decoding.

Sanitizer coverage requires the selected compiler's runtimes. Host GCC has
address/undefined runtimes in the cloud environment; the compact Clang image
lacks its sanitizer static archives. Temporary sources/binaries are removed.
This standalone fixture is outside the Meson Xbox suite and does not execute
Vulkan, validate driver behavior or establish a particular game's artifact
cause or frame rate.
