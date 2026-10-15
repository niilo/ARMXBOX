<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Contiguous uniform copies

Run without Vulkan headers, a GPU, firmware or games:

```sh
python3 tests/xbox/vulkan/uniform-copy-test.py --negative-control
python3 tests/xbox/vulkan/uniform-copy-test.py --sanitize --negative-control
python3 tests/xbox/vulkan/uniform-copy-test.py --cc clang-21 --negative-control
python3 tests/xbox/vulkan/uniform-copy-test.py --benchmark
```

The default baseline is immutable RC6 commit
`8aa78b9cb878c8cc9f11388ec28bad1d6a3ef086`; `--baseline-ref` can select
another deliberate pre-fix revision. Both actual copy functions are compiled
into one byte oracle. The runner also extracts the real std140/std430 layout
helpers and `block_to_uniforms()` reflection mapper. Reflection structures
are small host definitions; no SPIR-V compiler is needed.

Eighty differential cases compare the entire destination allocation and guard
bytes, including all untouched padding. Cases cover legacy std140 layouts,
valid single-element legacy std430 layouts, reflected std140/std430 arrays,
scalar/vector widths, padded vec3 arrays, mat3/mat4 columns and matrix arrays.
Zero-count copies accept a null source without calling memcpy. Whole-element
partial array counts preserve later elements; a changed source byte must
reach the destination. The unused legacy std430 array helper's unrelated
stride behavior is retained and is not used as a valid multi-element oracle.
Runtime layouts are built by the extracted reflection mapper.

Separate subprocesses must abort in production assertions for partial-vector
counts, too many elements and insufficient allocation space. Returning from
the helper causes those checks to fail. Expected assertion/negative children
disable core dumps. For `vec4[192]`, observed memcpy calls fall from 192 to 1.
The operation negative control substitutes the original copy as the optimized
candidate; it must fail the one-copy assertion while preserving byte results.

`--benchmark` compiles a separate binary without memcpy instrumentation or
sanitizers. It times only the 3 KiB contiguous copy, best of five 100,000-call
trials, using runtime layout arguments and an observable destination. Results
are local CPU cost; they exclude uniform serialization/hashing, Vulkan and
game execution and must not be translated into a gameplay FPS prediction.

Sanitizer coverage needs the selected compiler's runtimes. Host GCC provides
address/undefined runtimes in this cloud environment; the compact Clang image
lacks its sanitizer static archives. Temporary sources/binaries are removed.
This standalone fixture is outside the Meson Xbox suite.
