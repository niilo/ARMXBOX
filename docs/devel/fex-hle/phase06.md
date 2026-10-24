# Phase 0.6: reference-backed Xbox CPU identity

The [Phase0.5 Pocket S report](phase05-android-report.md) passes all 75 checks.
This cumulative diagnostic adds an immutable Xbox **CPUID identity** to FEX.
Instruction admission, virtual privilege and bounded execution remain separate
open gates; this increment does not run XBE files or games.

## Reference and implementation

The [reference capture](../../../experiments/fex-runtime/reference/README.md)
executes CPUID in the existing Xbox backend using a generated ROM. It records 54
input pairs twice, confirms identical output and verifies normal process exit.
The reference binary's embedded source revision is checked, and its CPU/model
source files match this branch. The committed JSON includes executable and ROM
checksums. A generated C++ golden table is checked against this JSON in CI.

Two source-audit assumptions are resolved by execution. The realized reference
has leaf-1 EDX **`0383fbfd`**, excluding VME despite its declared model mask.
Hypervisor leaves `40000000` and `40000001` return zero; most other unsupported
inputs return the maximum basic leaf's Xbox cache descriptors. The profile uses
these measured answers, including repeated and maximum ECX inputs.

Reviewed engine patch `0005-xbox-cpu-identity.diff` adds an explicit
`CPUID::IdentityProfile` argument to `Context::CreateNewContext`. Its default
retains ordinary FEX behavior. `XboxReference` is fixed during construction,
before CPUID state and translation are created, with no runtime setter. The
public CPUID API, name query and generated guest CPUID share the profile. Both
runtime calls and the engine's post-register-allocation constant-folding path
use its results. Normal FEX capability tests, including SSE2, remain in the
default context; they do not imply SSE2 support on Xbox.

The Xbox profile rejects 64-bit guest mode, unknown profile values, disk caching,
experimental code caching and code-cache validation. These caches currently lack
an identity-profile key. Setup checks again at InitCore; cached code from a
modern FEX identity must not be reused for Xbox identity. In-process testing uses
default -> Xbox -> default contexts at the same guest address. Ordinary guest
code buffers remain owned by their individual contexts.

The shared diagnostic execution boundary retains adapter-owned FPCR/FPSR and all
six native ABI witnesses for each Android entry. The new profile performs **111
entries**: 54 immediate CPUID cases, 54 runtime-loaded cases and three context
isolation witnesses. It checks exact HLT guest PC and generated exception
metadata, flags, ESP and unrelated GPR preservation. Fresh contexts initialize
IA-32 GDT and call/return state explicitly. The implementation does not replace
the accepted raw-fault handler or weaken its ownership checks.

## Validation and device acceptance

All **76 cloud checks** pass in both single-block and multiblock translation:
64 inherited checks plus 12 identity checks. Restoring the model's unfiltered VME
bit in the expected golden is a negative control: API, name API, immediate and
runtime-generated CPUID checks all fail. Restoring the reference golden passes.
This demonstrates sensitivity to an actual reference-model discrepancy.

The cumulative Android diagnostic is **phase0.6/code7**. Expected native result:
**88 PASS checks** = 75 accepted checks + 12 identity checks + one six-witness ABI
summary across the 111 new entries. This new native increment is pending a Pocket
S report. Cloud VIXL results do not establish Android native call-path behavior.

A correct CPUID answer does not enforce instruction availability. In particular,
XGETBV and SSE2/later opcodes are not disabled merely by hiding their CPUID bits.
CLI/STI, control registers, MSRs, port I/O, system entry and timestamp policy still
need explicit reject/virtualize/defer decisions and fault-state fixtures. Full
MMIO/unaligned/atomic/vector/concurrent memory semantics, bridge contracts,
bounded cancellation, general engine OOM/unload and 16 KiB hosts remain open.
See the [remaining plan](phase06-plan.md) and [embedding gates](next-gates.md).
No gameplay, FPS or sound improvement is claimed.
