<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Frame-slot VRAM timing

```sh
python3 tests/xbox/vulkan/vram-timing-test.py --cc clang-21
```

This standalone host fixture needs only a C11 compiler and Python. It compiles
the exact next-slot wait block from `draw.c` and propagation helper from
`vertex.c`, the
real work/statistics structures from `debug.h`, and the real per-flip snapshot
and formatter from `profile.c`. Outputs live under `build/cloud-vulkan` unless
`--output-dir` is provided. It does not require Vulkan, games or Android.

A deterministic clock and fence stub verify actual call boundaries. A memcpy
wrapper copies real buffers and checks that the upload-bitmap clone happens
outside the mirror data timer. Tests check first/full and normal span copy
bytes, unchanged bytes outside changed pages, empty-copy behavior, submitted versus
unsubmitted waits, negative elapsed clamping, two clock samples per measured
operation, per-flip EMA/reset, and zero clock calls/counter updates when
`NV2A_PERF_LOG=0`. Printed numbers are synthetic accounting examples.

Android's `xemu-vram` line is emitted with the other telemetry every 60 flips.
Its statistics update every flip using the existing 0.2 EMA convention:

- `Rot` counts rotations reaching the existing copy block, including a
  single-slot rotation back to itself.
- `NextWait` is host elapsed time inside the next-slot `vkWaitForFences` call,
  including driver-call overhead. It excludes the producer's completion-event
  wait, render-worker waits, deferred-submit spin, query readback and cleanup.
- `MirrorCopy` sums the elapsed times and requested lengths of actual memcpy
  calls. `First` is the initial full-mirror copy; `Span` is the normal
  changed-page propagation runs. Before RC5, `Span` measured the broad min/max
  copy including unchanged gaps. The metric label is retained for comparison.
- `#` values are runs/calls per flip after smoothing, so they can be fractional.
  Byte units are MiB (1,048,576 bytes), also per flip and smoothed.

The existing `Fen` phase stays inclusive. The new subregions separate specific
host operations within it; they do not turn Fen into pure GPU waiting. Elapsed
copy/wait time can include CPU preemption and is not an instruction-cycle
measurement. Requested memcpy bytes are not memory-bus traffic: caches and
page rounding can change the physical work. The timers exclude bitmap scanning
and bookkeeping. Telemetry itself changes no barriers, submissions or waits.
See `vertex-propagation.md` for the RC5 copy-selection tests.

RC6 appends upload-filter counters, also per-flip EMA:

- `VtxCmp` counts guarded hazardous upload checks and their requested MiB.
  Requested lengths bound comparison work; they are not measured bytes read.
- `NoOp` counts fully identical requests skipped and their requested MiB.
- `Trim` is MiB omitted from changed uploads whose trimmed range avoids a
  finish. Full no-ops are counted separately. Requests that still need a
  finish restore/copy the original range and contribute no trimmed bytes.
- `Avoid` counts vertex-dirty finishes avoided by full no-ops or trimming.

These counters add no clock samples and compile out with `NV2A_PERF_LOG=0`.
The older `xemu-vsync` dirty-byte total is recorded before the upload filter:
it remains dirty candidate bytes, not the amount copied after filtering.
