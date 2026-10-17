<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Shader change notification consumption

Run without Vulkan headers, a GPU, firmware or games:

```sh
python3 tests/xbox/vulkan/shader-consumption-test.py
python3 tests/xbox/vulkan/shader-consumption-test.py --sanitize
python3 tests/xbox/vulkan/shader-consumption-test.py --cc clang-21
python3 tests/xbox/vulkan/shader-consumption-test.py --negative-control
```

The default negative-control revision is the immutable RC6 commit
`8aa78b9cb878c8cc9f11388ec28bad1d6a3ef086`. Override it with `--baseline-ref`
only when deliberately choosing another revision before this correction.
The original functions must retain the consumed shader notification and
fail the repeated-draw assertion. A second baseline run records operation
counts rather than aborting at the first stale flag.

The runner extracts the actual shader-change producer, initial SFP guard,
pipeline early/prepare branches, input predicate/dirty checker, descriptor
preparation condition, pipeline binding prefix, ordinary draw consumer block,
queued replay restoration assignment and command-buffer invalidation flags.
It shares type scaffolding with the primitive-cache fixture. Dynamic raster
commands are compiled out; descriptor, uniform and Vulkan functions use
call-observing stubs, while production expressions decide whether to call them.

Fifteen mode/scenario combinations cover plain and push descriptors with
bindless compiled OFF/ON, plus runtime bindless when enabled. A new shader
must remain pending through pipeline/descriptor binding and attribute/texture
pushes. After consumption, fifty identical draws use the initial SFP guard
without new uniform preparation, pipeline-key preparation or descriptor
preparation decisions. The original path performs each preparation fifty-one
times; the corrected path performs it once for the same fifty-one draws.
These are extracted-path operation counts, not driver timings. In particular,
the plain descriptor cache may fulfill a preparation without a Vulkan write.

Other scenarios preserve changes during clear/skipped draws, setup that has
not reached the draw consumer (snapshot or async skip), queued flag restoration
and a fresh command buffer requiring descriptor/render-pass setup. A genuine
later shader change must still prepare and bind again. Real scene behavior,
complete optional queue execution, driver synchronization and gameplay frame
rate remain outside this fixture.

`--sanitize` requires the chosen compiler's sanitizer runtimes. Host GCC
provides address/undefined runtimes in this cloud environment; the compact
Clang image lacks its sanitizer static archives. Temporary sources/binaries
are removed automatically. This standalone fixture is outside the Meson suite.
