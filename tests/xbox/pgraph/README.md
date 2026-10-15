<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Packed index ingestion

Run without Vulkan, firmware, games or native generated headers:

```sh
python3 tests/xbox/pgraph/index-batch-test.py
python3 tests/xbox/pgraph/index-batch-test.py --sanitize
python3 tests/xbox/pgraph/index-batch-test.py --benchmark
```

A C11 compiler and Python are sufficient. Sanitizers require the selected
compiler's address/undefined runtimes. Outputs stay in
`build/cloud-pgraph/index-batch`, or `--output-dir`. These standalone tests are
outside the Meson Xbox suite. The immutable default baseline is the RC6 commit
`8aa78b9cb878c8cc9f11388ec28bad1d6a3ef086`; `--baseline-ref` can select another
revision with the original per-word handlers.

The runner compiles the actual old and new ARRAY_ELEMENT16/32 handlers, the
original non-incrementing wrapper and argument macro, the actual draw-array
expansion/reset functions and the actual CPU counter snapshot/formatter.
Renderer flush, tracing, little-endian loads and state types have small host
fixtures. The odd fixture capacity is 4095 instead of production 0x7ffff, so
near-capacity cases run quickly with the same packed-word boundary arithmetic.

Four configurations cover TRACE and PERF logging enabled/disabled. Each
compares 4,000 deterministic randomized states plus 14 explicit boundary,
maximum command-run, expansion/flush and INC NULL-input cases. Full final
state, word consumption, flushed data hash and per-word trace sequence must
match the original. Additional decoded-value assertions check input byte order
and packed low/high half ordering. INC reads only the supplied parameter and
leaves the caller's initial one-word consumption unchanged. Non-incrementing
runs publish consumed words once after appending the whole batch.

The fixture records logical begin/end checks: one per new batch, versus one
per old word. These operation counts are not a claim about surviving machine
instructions: compilers already inline/devirtualize the original wrapper and
can eliminate the debug-only begin/end check. Both kernels still perform the
required little-endian loads and output stores.

Empty non-incrementing runs, full capacity, packed overflow and capacity lost
after DRAW_ARRAYS expansion must assert before returning zero progress or
writing past the array. The original packed handler is a negative control:
starting at capacity minus one, it overwrites an adjacent canary because its
old assertion checked room for only one of the two indices. Its sanitizer log
intentionally contains the original out-of-bounds diagnostic.

The optional benchmark uses a separate build with fixture instrumentation,
tracing and PERF updates disabled. A volatile function target prevents the
compiler from inlining/hoisting calls out of the loop. Seven interleaved
samples of 20,000 calls compare 1/8/32/128/512/2047 words. The runner also saves
assembly for checking the real optimized loops. Warm host arrays and a local
kernel comparison do not predict Android/Steam Deck game FPS. The baseline
compiler may remove the indirect inner handler call itself; do not count that
as a guaranteed saving. Remaining concrete changes hoist expansion/capacity
checks and publish the shared length once per batch.

Android `hakuX-cpu` telemetry appends `Idx16:batches/words` and
`Idx32:batches/words`, using per-flip EMA. These count only non-incrementing
runs and compile out with NV2A_PERF_LOG=0. They establish how much of the old
aggregate Ni word count actually uses these methods. No fence, synchronization,
GPU resource lifetime, guest FP or TCG behavior changes here.
