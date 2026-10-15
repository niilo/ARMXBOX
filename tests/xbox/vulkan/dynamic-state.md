# Vulkan dynamic-state command cache regression

Run from the repository root:

```sh
python3 tests/xbox/vulkan/dynamic-state-test.py --negative-control
python3 tests/xbox/vulkan/dynamic-state-test.py --sanitize --negative-control
python3 tests/xbox/vulkan/dynamic-state-test.py --cc clang-21 --negative-control
```

`--output-dir PATH` retains generated sources, binaries and normal-run logs for
independent inspection. Each run uses separate temporary outputs by default.
`--baseline-ref` defaults to immutable RC8 commit
`beb317063ded5b2a198ef044201682de0562ced2`.

The runner extracts the actual current `begin_draw` dynamic-state block, its
cache type and its command argument map tables. It compares that code against
the actual block from RC8. It does not transform the candidate's comparisons.
Vulkan commands record argument identities and effective state in a CPU-only
fixture; the Vulkan driver is not loaded. Stable stand-in enum values preserve
identity in the actual production map tables. The blend-color conversion is a
fixture helper shared by both sides.

Both `OPT_DYNAMIC_BLEND=0` and `=1` builds execute one million randomized
transitions, split among the four combinations of extended dynamic state and
runtime dynamic blend support. The sequence mixes full-word updates, single-bit
changes, repeated unchanged state, clear draws, and new command buffers with
invalid cached state. It compares effective recorded arguments after every
draw. Another 760 explicit valid single-bit set/clear cases cover all six
registers in all four capability combinations. Out-of-range comparison-function
encodings are excluded because production asserts reject them.

The negative controls check two different properties:

- Removing `STENCIL_WRITE_ENABLE` from the depth-group dependency mask must
  produce a different effective stencil write mask in the randomized oracle.
- Substituting actual RC8 code for the candidate must fail the targeted
  operation-count check on an alpha-reference-only update.

Exact operation counts also cover color-write-only, depth-write-only,
logic-operation-only, unused stencil-bit and polygon-mode-only changes. An
alpha-reference-only update needs no depth/stencil or blend/color command:
it falls from seven calls to zero when both dynamic extensions are active,
or four to zero without dynamic blend.

The production change retains the existing five command-group conditions and
all command bodies, assertions, cache stores and validity resets. Seven
whole-register comparisons now consider only the bits consumed by their group.
The test does not validate Vulkan driver behavior, measure driver CPU time, or
estimate how often these changes occur in a game. Captured RC7 draw counts show
that the surrounding path is active; actual redundant-command frequency still
requires a device comparison. No synchronization or shader-uniform behavior
changes are involved.
