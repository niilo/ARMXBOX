<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Primitive cache guard fixture

Run from the repository root:

```sh
python3 tests/xbox/vulkan/primitive-cache-test.py --negative-control
python3 tests/xbox/vulkan/primitive-cache-test.py --cc cc --sanitize
python3 tests/xbox/vulkan/primitive-cache-test.py --cc clang-21
```

The runner compiles extracted production code with small surrounding types,
real NV2A register masks and deterministic register access. It includes the
existing primitive rewrite mapping, unchanged geometry-state builder, live
primitive-mode and attribute-mask matchers, complete shared shader dirty
checker, actual vertex-input matcher, all six Vulkan
draw guard conditions, the full first SFP guard chain, and the first pipeline
cache-hit block. It builds with bindless texture support compiled both off
and on. No game, Vulkan device or Android SDK is required.

Coverage:

- All ten supported guest primitive types and three polygon modes agree with
  the actual geometry builder: 30 modes, 180 draw guard matches.
- 900 cached/live primitive and polygon combinations compare against freshly
  built geometry state. Raw guest primitive values are unchanged.
- Real output topology changes, dirty SETUPRASTER, shader/pipeline/register
  generations, dirty program data, constants, uniforms, descriptor state and
  framebuffer state retain the relevant cache fallbacks.
- All four shader attribute masks can change without a shader generation
  update and still force a recheck. Clear pipelines cannot be reused for
  normal rendering. Shrinking attribute/binding descriptor counts cannot
  match only a surviving prefix.
- Same-generation input stride, format and layout changes reject early
  pipeline/SFP/BL/MFP reuse; per-draw buffer-offset-only changes still reuse
  compatible input state. The real pipeline dirty checker is also executed.
- The actual pipeline cache-hit block calls the uniform update when required,
  including when descriptors are unavailable for MFP; clean state skips it.
  That call is stubbed, so this test verifies control flow and arguments, not
  the complete shader uniform serializer or GPU upload.
- `--negative-control` compiles the original HEAD guard expressions and dirty
  checker. They reject 21 of 30 valid builder matches per guard, or 126 of 180
  total. A compile failure cannot count as the negative control succeeding.

The register accessor, render-pass dirty helper and uniform update dependency
are synthetic. This fixture does not execute complete draw setup, primitive
assembly, command submission or a GPU driver. Those raw assembly/topology
paths are unchanged. Separate vertex freshness tests must cover immediate
attribute mutation before newly reachable fast paths are enabled.
