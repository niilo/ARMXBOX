<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Destination vertex-mirror propagation

Run with a C11 compiler and Python, without Vulkan, firmware or games:

```sh
python3 tests/xbox/vulkan/vertex-propagation-test.py --negative-control
python3 tests/xbox/vulkan/vertex-propagation-test.py --negative-control --sanitize
```

The optional original-source negative control defaults to the immutable
pre-fix revision `f75153dddd03880c0c6fbfea93b2dea8ceb29af9`. Use
`--baseline-ref REV` to select a different pre-fix revision.
`--sanitize` requires the selected compiler's address/undefined runtimes.
Outputs and extracted functions stay in `build/cloud-vulkan/propagation` unless
`--output-dir` is provided. This standalone test is outside the Meson suite.

The runner compiles the actual production init/finalize, write-bookkeeping and
mirror-propagation helpers from `vertex.c`, together with the normal CPU upload
writer, the dormant raw render-worker writer, `pgraph_vk_flush_all_frames`,
frame-depth setters and command-initialization depth capture. Function bodies
are extracted unchanged. Small type, bitmap, allocation, clock and Vulkan
stubs make them executable on a host without renderer dependencies. The bitmap
stubs perform bounded individual-bit operations; the expected data comes from
an independent byte array updated by the test's write inputs.

Every write/rotation checks that the current mirror equals those latest bytes.
Each initialized destination page without stale debt must match too. Coverage
includes initial depths 1/2/3, first full copies, no-write rotations, accumulated
multi-generation history, 1,800 seeded writes, overlapping and disjoint writes,
multiple bitmap words, cross-page ranges, a 13-byte final page, zero-length
writes, whole-RAM writes, full flush followed by more writes, both writer paths,
a finish during an overlapping upload, allocation-failure cleanup and renderer
recreation. Inactive slots retain null storage/maps. Live depth requests must
leave the active allocation count fixed; a fresh initialization honors them.

Both performance-logging configurations run the same byte checks. Enabled
logging must match observed memcpy bytes/runs; disabled logging makes zero
measurement clock calls and counter updates. Two separated 4-byte writes in a
64 KiB warmed mirror copy 8,192 bytes in two runs; the previous min/max range
would copy 65,530 bytes. This deterministic byte comparison is not a timing or
FPS benchmark. A separate negative control compiles the actual original writer
and rotation block and must reproduce lost history across three slots.

The fixture substitutes submission/fence behavior and models rotation at the
existing safe point. It validates host byte selection, debt and bookkeeping,
not GPU visibility or real driver/thread scheduling. Android compilation,
source review of wait/barrier placement and device replay remain separate.

Each destination now accumulates stale page bits until it is activated. The
existing next-slot fence wait completes before the helper copies contiguous
stale page runs from the authoritative current mirror. First use still copies
the entire mirror; uploaded bits still clone from the source. A full flush does
not erase stale debt. At depth 1 no mirror memcpy is needed. Flush bounds still
cover the union of copied runs, retaining the existing VMA flush behavior.
Frame depth is captured once before slot allocation and remains fixed until
renderer reinitialization; a later native setter request applies then.

## Guarded upload comparison

RC6 also extracts the actual `trim_unchanged_vertex_ram_pages` helper and tests
the normal upload writer with all initial depths. A request is compared only
if it overlaps recorded reads and the post-download current slot is initialized
with all requested uploaded bits set. Identical valid requests retain read
reservations but perform no copy/finish or mirror-debt/flush changes. An
aligned request may trim unchanged whole leading/trailing pages and avoid a
finish if the changed range no longer overlaps. Partial requests use exact
whole-range equality only. If a finish is still needed, the writer restores
the original request and copies it after finish and current-slot reacquisition.

Cases include equal partial/single-page/multi-page uploads, invalid/unuploaded
fallback, no comparison for nonhazards, whole-page prefix/suffix trimming,
unchanged destination debt/flush metadata, zero-size bounds, a surface download
that rotates and changes source bytes, and a finish callback that changes an
initially equal prefix. A memcpy interceptor rejects writes into current pages
that recorded draws still read. The original writer must reproduce an
unnecessary unchanged-data finish. A targeted mutation of the candidate that
removes only original-range restoration must fail the byte oracle after a
finish changes source bytes. The retained three-slot history control also runs.

For an optional synthetic host comparison-cost sample:

```sh
python3 tests/xbox/vulkan/vertex-propagation-test.py --benchmark
```

This times the actual pure helper through a volatile function pointer, with
seven samples of 2,000 calls at 4/16/64/256 KiB. It reports medians and ranges for
equal data and changes at the first/last byte. Buffers are ordinary warmed host
RAM; results exclude caller bitmap checks, Vulkan memory behavior, driver and
fence waits. They do not predict Pocket S FPS. Device `VtxCmp`, `NoOp`, `Trim`
and `Avoid` counters show the actual hit rate and requested comparison volume.
