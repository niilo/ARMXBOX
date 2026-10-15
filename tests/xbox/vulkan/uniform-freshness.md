<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Uniform freshness across cache hits

Run from the repository root:

```sh
python3 tests/xbox/vulkan/uniform-freshness-test.py \
    --sanitize --negative-control
python3 tests/xbox/vulkan/uniform-freshness-test.py \
    --cc clang --negative-control
```

The test requires a C compiler, Python 3 and Git with immutable RC7 commit
`d2da241bab37c6460fc8832e8ded75d42362e739`.
No Vulkan SDK, GPU or game is needed.
Assertions remain enabled. Expected-failure child processes disable core dumps.

The runner extracts actual material/light setters, register writer and dynamic
mask/category setup, relevant VSH/PSH uniform getter branches, initial SFP
rejection chain, MFP guards and pipeline early-return block. It reuses the
primitive-cache fixture's minimal state model, adapting only register storage
indexing so the real register writer remains unchanged. Backend allocation and
uniform upload are observation boundaries; getter data and dispatch decisions
come from production source. The successful serialization boundary records the
actual source's constant flags and raw primitive class.

Each bindless compile mode runs 495 cases:

- Material alpha and all four light vectors, all eight lights/components,
  changed and repeated bit patterns, signed zero and NaN payloads, preserved
  pending dirtiness, actual getters and SFP rejection/refresh/clean reuse.
- Dynamic alpha-reference and uniform-only register writes with unchanged
  shader/pipeline generations when the render pass has ended or descriptor
  state makes MFP unavailable. Early pipeline hits refresh actual alpha data.
- QUADS in polygon POINT/LINE mode versus native POINTS/LINES. The canonical
  shader mode stays equal while actual PSH depthOffset/depthFactor change
  between 2/3 and zero. Crossing the raw polygon class refreshes uniforms;
  QUADS to QUAD_STRIP keeps clean reuse.

Three independent negative controls per compile mode extract actual immutable
RC7 source. They must fail for stale nonregister values, stale register uniforms
and stale primitive-dependent depth offsets, respectively. Compile errors or
unrelated assertions are rejected as controls.

This validates source-level freshness and cache-dispatch decisions. It does not
execute the complete renderer, real shader reflection/compilation, Vulkan
commands, driver synchronization, images or device timing. The separate uniform
copy and texture notification fixtures cover packing and texture scale inputs.
No FPS effect is inferred from these cases.
