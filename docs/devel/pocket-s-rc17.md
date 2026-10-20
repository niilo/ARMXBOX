# Pocket S RC17: pack proven PC24 results without redundant classification

RC17 is cumulative: it adds one result-conversion change to the untested RC16
candidate. RC16 remains available unchanged, and neither candidate is merged
into master or published as a stable release before device comparison.

## Change and arithmetic contract

The existing PC24 input bounds admit exactly representable binary32 operands
with unbiased exponents in [-60, 60], plus signed zeros. Nonzero add/sub results
are at least 2^-83; products lie in [2^-120, 2^122), and nonzero quotients in
(2^-121, 2^121). Rounding cannot approach binary32 overflow or subnormal values.
Square roots of admitted nonnegative operands are also normal or signed zero.
Division by zero, negative nonzero square roots, specials, wider significands,
incompatible modes and malformed inputs still reject before native arithmetic.

These bounds already guarantee normal or zero results. RC17 packs their bits
directly into the existing 80-bit representation rather than checking again for
impossible subnormal/NaN/infinite results. Signed zeros retain their sign and
zero significand. The existing checked converter remains available. RC16's
sticky-flag optimization, numerical arithmetic, exactness calculation when
needed, fallback paths and pending-exception reconciliation are retained.

The fixture pins the input converter, host guard, checked result converter and
bound constants to immutable RC16 source. Future widening of the operand domain
must revisit this proof rather than reuse the unchecked packing blindly.

## Validation

GCC ASan/UBSan and Clang each pass four feature/policy combinations against real
softfloat and immutable RC13 helpers: 1,774,436 modeled helper-state comparisons
and 1,774,469 numerical/rejection cases per combination. The flag-omission API
is also compared in every direct binary case. A further 97,544 packing checks
per combination cover every normal exponent, both signs, each fraction bit and
its complement, zero/full fractions and signed zeros under all four host
rounding modes, preserving hardware flags and errno. The checked conversion
oracle is pinned to RC16. Seven semantic mutants must fail, including broken
zero and sign packing. The optional profiler fixture retains six configurations
and five negative controls.

These are correctness and emitted-code checks, not device-cycle or FPS results.
Final optimized ARM64 packaging must retain the FPCR guards, RC16 bypasses and
normal-build profiler erasure. No renderer, audio, input, synchronization,
resolution or VBLANK change is included.

## Test both iterations later

| Build | Difference | Compare with |
| --- | --- | --- |
| RC16 Control | v1.2.14 runtime, version metadata only | Baseline |
| RC16 | Skip redundant inexact reconstruction when both flags are sticky | Control |
| RC17 | RC16 plus bounded result packing | RC16; also Control for the cumulative result |

All three signed production APKs use version 1.2.15/code 44 and the existing
production package/certificate, with distinct filenames, launcher/version
labels and startup/export identity. They can replace each other without a
downgrade or clearing data. RC17 has a separate branch/stacked draft PR based
on RC16; the existing two APKs remain valid controls. Actions downloads expire
after 14 days.

Run Control, RC16, then RC17 on the same SSX 3 course at 3x/Turnip, CPU profiling
OFF, same settings and power/fan/charging conditions, cooling to comparable
starting temperatures. Include later gameplay and save each full log and
visual/audio/input feedback. Reverse the order if differences are small.
Retain, reject or rebase each change according to the comparison; neither gain
is assumed. If further CPU work is needed, collect a separate post-PC24 profile
and update the v3 rejection parser before expanding any arithmetic subset.
