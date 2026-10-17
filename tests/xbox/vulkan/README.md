# Vulkan draw guard equivalence

Run from a configured native Vulkan build with Clang, LLD and the renderer
development headers available:

```sh
python3 tests/xbox/vulkan/snapshot-guard.py --build-dir build
python3 tests/xbox/vulkan/snapshot-guard.py --build-dir build --thinlto
```

The runner reuses the `render_thread.c` flags from `compile_commands.json`,
compiles the real snapshot helper and links only its needed ELF sections. It
does not need a Vulkan device, firmware or games. Native generated headers
must already exist, as for the other Xbox tests. Outputs stay in the ignored
build directory. This standalone test is not part of the Meson Xbox suite.

`snapshot-guard.c` compares the original full snapshot against an independent
four-field reference for the inputs to the assertions in
`pgraph_vk_flush_draw()`. It checks all 16 combinations of mutations to those
four fields over 512 starting states, both with live registers and with an
active render snapshot. Both versions must accept unchanged fields and reject
each changed field identically. The real snapshot structure and copy function
are used. The test does not execute draws or validate GPU synchronization.

For optional timing, add `--benchmark`. The runner reports 11 interleaved
baseline/candidate samples of 200,000 captures each. Discard the first run and
report variance; concurrent system work can distort these small timings. This
measures host capture overhead only. ThinLTO is included because it might
eliminate some snapshot work itself. Neither benchmark predicts target GPU
performance or game FPS.

The production change retains all four assertions but captures only their
inputs. It can also be checked by compiling baseline and changed `draw.c` at
`-O2 -fstack-usage` and inspecting `pgraph_vk_flush_draw`: the full snapshot call
and its large stack reservation disappear. On the cloud x86-64 Clang 21 build,
the measured stack reservation falls from 42,712 to 1,000 bytes. The Android
Profile build separately checks the real ARM64 translation unit.

## Vertex read lifetime regression

```sh
python3 tests/xbox/vulkan/vertex-read-tracking.py --negative-control
python3 tests/xbox/vulkan/vertex-read-tracking.py --sanitize
```

This standalone runner extracts the actual vertex buffer bind, reorder
snapshot, format-size and upload functions. Stub Vulkan binding and submission
recording preserve references to vertex bytes until submission. It checks that
later writes cannot change an earlier recorded draw, including clean reused
uploads, a finish during setup, inline/remapped attributes and page-boundary
footprints. The original source must reproduce the overwrite. The fixture
does not execute Vulkan, game content, the full draw path or Android threading.

## GPU timestamp consumption regression

From the same configured native build used for the snapshot fixture:

```sh
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build
python3 tests/xbox/vulkan/gpu-timestamps-test.py --build-dir build --thinlto
```

The fixture includes the real draw translation unit, exercising its timestamp
readback and command-buffer begin functions with stub Vulkan query results.
Its recording-complete helper is extracted from the production finish path.
It checks single consumption across immediate/deferred readback, slot reuse,
flush-like repeated visits, unavailable-result retry and unsupported timing.
The original source must fail the duplicate-accounting check. These tests
validate accounting without changing submitted-slot resource ownership or
introducing GPU waits; they do not measure device GPU time or FPS.

## Direct surface reuse and copy/wait measurements

```sh
python3 tests/xbox/vulkan/surface-reuse-test.py --negative-control
python3 tests/xbox/vulkan/surface-reuse-test.py --sanitize
python3 tests/xbox/vulkan/vram-timing-test.py
```

The surface fixture exercises production cached-binding decisions and
descriptor inputs with deterministic stubs, including bindless enabled and
disabled builds. It does not render images. See `surface-reuse.md` for exact
coverage and the original-source negative control.

The VRAM timing fixture executes the production wait/copy measurement blocks
with a fake clock and real byte-array copies, with performance logging enabled
and disabled. It verifies that measurement does not change the copied bytes
or ownership decisions. The output example is synthetic, not a device timing.
See `vram-timing.md` for metric definitions and coverage limits.

## Destination vertex-mirror propagation

```sh
python3 tests/xbox/vulkan/vertex-propagation-test.py --negative-control
python3 tests/xbox/vulkan/vertex-propagation-test.py --sanitize
```

The standalone fixture compiles actual production helpers and both CPU writers
against an independent byte-array oracle, at initial depths 1/2/3. It checks
multi-generation history, first copies, no-write rotations, partial/tail pages,
full flush, recreation and fixed allocation depth. The original writer and
rotation must lose three-slot history in the negative control. Byte reduction
is deterministic; it predicts no device FPS. See `vertex-propagation.md` for
source boundaries, dependency stubs and coverage limits.

The same propagation fixture also checks the guarded RC6 upload filter:
valid identical data avoids a redundant finish, unchanged whole edge pages
can narrow a hazardous request, and a true remaining hazard preserves the
full post-finish copy. Surface-download/finish callbacks and original-source
negative controls exercise the ordering contract. Optional `--benchmark`
measures host comparison-helper cost only.
