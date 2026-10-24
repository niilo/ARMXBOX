# Phase 0.3: precise IA-32 divide errors

Phase0.2 passed 21 checks on the Pocket S. Phase0.3 addresses the independently
confirmed FEX gap where IA-32 DIV zero/overflow continued instead of raising #DE.
It is a cumulative CPU diagnostic, not a playable backend or an FPS result.

## Change and arithmetic bounds

The immutable FEX revision stays unchanged. Reviewed patch
`experiments/fex-runtime/patches/0002-ia32-divide-errors.diff` adds checked 8/16/32-
bit DIV and IDIV to the IA-32 dispatch path; x86-64 dispatch is not changed.

For unsigned division, high-half >= divisor detects both zero divisor and a
quotient too large for the destination. For signed division, let M=2^(w-1).
Using unsigned magnitudes, the first invalid dividend magnitude is
`|divisor| * (M + negative_quotient)`. Comparing `|dividend|` against that bound
also rejects divisor zero. For w<=32 the product fits in uint64, including the
INT64_MIN dividend case; no overflowing signed C++ division is used in the guard.

A conditional exception IR operation exits before storing quotient or remainder.
It records the faulting guest PC, SIGFPE/#DE, error zero and FPE_INTDIV, then uses
the existing generated-exception exit path. The false branch continues without
resetting the native stack or modifying fault metadata. Common exception emission
is shared with the existing Break operation. The flag-optimization pass treats
this conditional exit as reading all guest flags, so later guest instructions
cannot erase state visible on the fault path. A local mutation control that
removes that observation causes all six register matrices to fail; restoring it
returns them to passing.

The conditional exit keeps operand values in one IR block. In particular, the
memory divisor is loaded once, preventing duplicate MMIO reads and a changed RAM
divisor between the check and divide. The normal path uses one native divide.
Register-width stores preserve AL/AH/AX and upper-register semantics; 32-bit
results retain FEX's canonical zero extension. The patch introduces no custom
parallel JIT, rootfs, Linux syscall frontend or production-backend change.

## Validation

The probe adds 18 matrix summaries: register, memory and aliased operands for
signed/unsigned division at three widths. Together they execute **66,700 cases**
per translation mode. Cases cover zero, quotient limits, signs, signed minimum,
upper-register preservation, exact fault PC, flags preserved on faults, memory
operands, AL/AH/DL/DH/AX/DX/EAX/EDX aliases, and deterministic random inputs.
Each register matrix also tests real guest ADD deferred flags and repair/resume
at the faulting divide after changing only the divisor.

Both multiblock and single-block modes pass: **133,400 generated-code cases**.
An independent C++ integer reference is validated by executing **31,296 native
x86 DIV/IDIV cases**, including real SIGFPE delivery, dividend preservation and
arithmetic flags on faults. Signed-reference MIN64/-1 is handled before C++
division to avoid undefined behavior. The native oracle runs only in the isolated
x86-64 Linux test process; its signal handler is not included in the Android app.

The original 15 CPU fixtures and five ABI witness controls also remain passing.
Android ARM64 full-object linking, APK dependency closure, signing and alignment
are checked. Fresh/repeated dependency preparation verifies the exact combined
portability and CPU patches, refusing unrelated dependency edits. Hardware tests
and generated-code tests remain distinct; simulator timing is not device speed.

Expected healthy native report: **39 PASS lines**, including the six native ABI
checks previously validated in phase0.2. New division behavior still needs the
Pocket S run. The probe report shows whether multiblock or single-block mode was
used; `ARMXBOX_PROBE_SINGLE_BLOCK=1` selects the latter in host CI.

## Next work

Raw memory faults, guest-PC recovery from native faults, MMIO exits, full bridge
semantics, 16 KiB hosts, Xbox CPUID/privilege policy and bounded execution remain
open. Division memory reads follow normal fault precedence, but protected/unmapped
memory recovery is not supplied by this arithmetic patch. Continue those Phase 0
contracts before broad XBE/kernel/graphics/audio HLE. There is no measured title
performance gain yet.
