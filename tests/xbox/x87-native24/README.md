<!-- SPDX-License-Identifier: GPL-2.0-or-later
-->
# Guarded PC24 x87 arithmetic

This standalone fixture compares the current twelve binary x87 C helpers and
`FSQRT` with their immutable RC13 versions from
`649b22697867704f6d7c1585f4fb5d574da50b15`. It extracts the actual helper bodies,
native dispatch wrappers, exception merge and status initialization functions;
it executes the real repository `fpu/softfloat.c`. It does not substitute a
numerical implementation. The runner verifies the immutable softfloat source,
existing PC53 native header and `floatx80` type declaration before compiling.

Run from the repository root:

```sh
python3 tests/xbox/x87-native24/test.py --sanitize --negative-control \
  --output-dir /tmp/native24-gcc
python3 tests/xbox/x87-native24/test.py --cc clang-21 --negative-control \
  --output-dir /tmp/native24-clang
```

The runner builds with `XEMU_OPT_X87_PC24=0` and `=1`, then executes each with
native arithmetic policy enabled and disabled. Each of these four combinations
performs 1,774,436 complete modeled CPU state comparisons and 1,774,469 direct
numerical/rejection API checks. Each compiler run therefore covers 7,097,744
helper state comparisons and 7,097,876 numerical/rejection checks. Five additional
actual wrapper admission checks per combination verify the feature/policy gate
and pending-exception reconciliation, so an implementation that always falls
back cannot pass merely through numerical agreement.

Coverage includes:

- All four guest precision encodings and rounding modes, independently combined
  with three softfloat precision modes, four softfloat rounding modes and four
  host rounding modes. The new PC24 subset must fall back under a mismatch.
- Signed zeros, finite normal/subnormal boundaries, infinities, NaNs, malformed
  80-bit encodings, mixed 24/53/64-bit significands, exponent limits and extended
  exponents, midpoint neighbors, signed cancellation, multiplication, division
  and square root. The edge cross-product includes both operand signs.
- All 65,536 prior softfloat exception flag patterns through all thirteen
  helpers, plus every 64-by-64 exception status/mask pair and four prior SE/B
  combinations. Register TOP/tags, aliases when STN is ST0, remaining registers,
  instruction/data fields, other FP status words and surrounding canaries are
  part of the modeled state comparison.
- 350,000 deterministic mixed-precision random full-state cases per combination.
- Direct API result/inexact canaries on rejection, guest result and exception
  flags against real PC24 softfloat on success, unchanged host rounding and
  matching `errno`. A separate 32-case rejection corpus clears hardware flags
  and seeds FE_INVALID before each call, proving no new hardware exception flag
  is introduced by rejected encodings, modes, signs, opcodes or zero divisors.

The four negative controls must fail: removing exact-result pending SE/B
reconciliation, admitting wider operands through truncating conversion, losing
the inexact flag, and replacing the guarded operation with an overbroad binary64
operation followed by a binary32 cast. The last control uses the explicit
`(1 + 2^-24) + 2^-54` witness: the exact answer rounds upward at PC24, while a
binary64 intermediate loses the tiny addend and reaches a binary32 midpoint.
The production implementation rejects that wider input and uses softfloat.

The shim supplies standard includes/configuration and GLib assertion spellings,
sharing the support code in `../x87-transfer/test.py`. Real softfloat and helpers
select the same generic `floatx80` declaration before the fixture selects the
production ARM helper gate. Only nonarchitectural six-byte `floatx80` tail
padding is normalized; all 80 payload bits and the rest of the state are checked.

Accepted native operations may raise host sticky hardware exception flags, as
the existing PC53 path already does. Their host rounding mode remains unchanged.
The test does not require hardware flags to equal an integer-only softfloat run.
Existing PC53 behavior is compared with immutable RC13, including combinations
whose guest and softfloat modes deliberately disagree; this patch does not
change or claim to repair those pre-existing semantics.

These are host numerical/helper-state tests. They do not execute TCG, guest MMU
faults, the Android FPCR branch, or games, and they do not establish an FPS gain.
The ARM FPCR guard and final linked APK instructions require separate code
inspection or device testing. Source/compiler hashes and individual run logs
are retained in the selected output directory.
