<!-- SPDX-License-Identifier: GPL-2.0-or-later
-->
# Exact x87 transfer helpers

This fixture compares the six current `FLDS`/`FLDL`/`FSTS`/`FSTL` C helpers
against immutable RC11 helpers from
`28c41dfe9115613248ed5a520c897b0cb6e150a6`. Both sides execute the real repository
`fpu/softfloat.c`; the runner verifies that file is unchanged from the reference.
Only standard includes/configuration and GLib assertion spellings are supplied
by a small test shim. No numerical converter is replaced.

Run from the repository root:

```sh
python3 tests/xbox/x87-transfer/test.py --sanitize --negative-control \
  --output-dir /tmp/x87-transfer-gcc
python3 tests/xbox/x87-transfer/test.py --cc clang-21 --negative-control \
  --output-dir /tmp/x87-transfer-clang
```

Each compiler run covers `XEMU_OPT_X87_TRANSFER=0` and `=1`. Each option performs
2,781,337 comparisons of the complete modeled CPU state, normalizing only the
nonarchitectural tail padding of `floatx80` objects. The actual type declaration
is identical in the helper and softfloat translation units. Tests include:

- All 65,536 prior softfloat flag patterns for each exact helper.
- All three precision modes, four rounding modes, independent `fpuc` masks and
  softfloat precision/rounding settings, and old pending exceptions.
- All 64 exception-status combinations by 64 masks and four old SE/B states.
- All TOP values, signed zeros, normal/subnormal boundaries, infinities, NaNs,
  malformed 80-bit encodings, exact/inexact values, ties and overflow edges.
- 250,000 deterministic random full-state/encoding cases, surrounding canaries,
  MMX aliases, TOP/tags, store-without-pop behavior and transfer hook IDs/paths.

The two negative controls must fail: removing `fpu_set_exception(env, 0)` loses
pending SE/B reconciliation, and dropping the FLD64 PC24 guard changes rounding.
The fixture checks the selected diagnostic branch once per entry. Diagnostic
path names describe eligibility, not measured converter cost or exception flags;
the generic converter already has its own normal-value shortcuts.

The production gate requires Xbox, AArch64, softfloat storage and the transfer
option. The test selects that gate around extracted helpers while using the
generic real `floatx80` ABI for both test translation units. This is a numerical
and helper-state differential test. It does not execute the guest decoder,
guest MMU/fault delivery, an Android CPU or a game. The translator and guest
memory operation ordering are unchanged by this patch.

For actual Android compiler mechanism evidence, use a configured ARM64 build
whose dependencies and generated headers already exist:

```sh
python3 tests/xbox/x87-transfer/codegen.py \
  --commands android/app/.cxx/RelWithDebInfo/CACHE/arm64-v8a/compile_commands.json \
  --output-dir /tmp/x87-transfer-codegen
```

This compiles only the helper TU with the option OFF/ON and the real softfloat
TU, using the recorded Android compiler/include arguments, profiling OFF and an
explicit `-fno-lto`. It records commands/source hashes, disassembly and symbol
sizes. These object files establish a local mechanism, not the final APK call
graph: Android ThinLTO already inlines the baseline normal load conversions.
Use the linked APK ELF to compare final instruction paths. Neither instruction
counts nor helper-entry counts establish device CPU time or an FPS improvement.
