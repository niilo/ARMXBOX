# Phase 0.10: opt-in reference instruction rejection subset

**Native regression:** the user reports phase0.10/code11 closes immediately on
starting fixtures. No device trace is available yet, and native acceptance is
withheld. [Phase0.10.1](phase101.md) adds persistent progress recovery without
changing the CPU cases or claiming an engine fix. Controlled XBE work remains
gated on resolving this failure.

The [Phase0.9 report](phase09-android-report.md) accepts all 148 native checks.
This increment implements the first instruction-policy portion of the
[Phase0.10 plan](phase10-plan.md). It does not implement virtual interrupts,
an Xbox clock, a complete Pentium III decoder or game loading.

## Policy and patch

Reviewed patch `0008-instruction-policy.diff` adds the immutable, per-context
`InstructionPolicy::Default` / `XboxReferenceSubset` choice as the fourth
`CreateNewContext` argument. Its default preserves existing instruction behavior.
The policy is independent of CPUID identity and instruction checkpoints. Unknown
policy values are rejected. A nondefault policy rejects IA64 mode and shared,
experimental or validation caches without a policy key, including a core-init
recheck. It has no runtime setter and changes no global opcode tables.

The translator compares resolved decoded handler identities for legacy HADDPS,
PSHUFB, PTEST, PCMPGTQ, VZERO (both backend implementations), RDTSCP and XGETBV.
A selected instruction is marked potentially faulting before register/flag
materialization and guest-PC metadata emission. Dispatch then uses the existing
`UnimplementedOp` precise SIGILL/#UD path. No source operand is accessed and no
instruction result is committed. When the host-feature model has no VEX table,
the selected policy parses the prefix and produces the existing unimplemented
instruction exit instead of default mode's generic invalid-decode result. The
default early decode behavior is retained. Rejection ends block generation through
the existing break/exit logic. Invalid LOCK forms of these rejected operations
also reject without asking for a nonexistent successful LOCK handler.

This is a named rejection subset, not an allowlist for everything else. Matching
a semantic handler also covers its other operand forms; tests demonstrate the
captured encodings plus four guarded memory forms. Other modern instructions,
prefix combinations and vector families remain outside broad admission. Do not
interpret an instruction outside this subset as Xbox hardware support.

The [executed reference captures](../../../experiments/fex-runtime/reference/README.md#instruction-admission-capture)
are independent of FEX. They retain the observed SSE2 PXOR XMM permissiveness
while recording #UD for the selected newer forms. The policy leaves that SSE2
form unchanged: a retained-reference compatibility finding does not certify
physical Xbox SSE2 support. The generated fixture header comes from the captured
JSON and is checked in both GitHub jobs; expected trap outcomes are not obtained
from the new translator implementation.

The policy decision happens during translation. It adds no executed polling to
contexts with instruction checkpoints disabled. A separate terminating fixture
checks precise rejection without checkpoints; this is not a cancellation claim
for arbitrary unpolled guest code. Existing diagnostic budget/stop behavior is
retained when checkpoints are enabled.

## Fixtures

Twenty-seven new common checks cover:

- independent factory/configuration guards, including changed cache configuration
  before core initialization;
- all 12 captured instruction cases, each with three independent arithmetic-flag
  setups and cold/warm execution in the selected policy context;
- inaccessible memory operands for the four rejected legacy SIMD handlers,
  admitted only after their finite register rejection controls pass;
- modern default contexts before and after a policy context at identical guest
  addresses, CPUID/policy independence and code invalidation from a rejected
  instruction to an allowed one;
- zero-budget and latched-stop boundaries before #UD, exact PC/all eight IA32
  GPRs/canonical flags, following memory sentinel and generated metadata;
- unchanged SIMD/MMX/x87 status/TOP/tags and floating controls on rejection;
- precise HADDPS and disabled-AVX rejection in terminating contexts without
  instruction checkpoints;
- AVX128 VZERO dispatch enabled through FEX's baseline-NEON implementation, with
  default execution and policy rejection at the same guest address. Optional
  SVE256 host execution is not admitted or inferred from that check.

Reference captures use CPL0/IF=0; these runtime fixtures explicitly use the
existing userspace IF=1 contract. Only instruction outcome and the demonstrated
state boundary are compared. This does not implement virtual IF or ring0.

The cumulative expectation is **154 cloud PASS checks** in both translation
modes and **176 native PASS checks**: all 148 accepted native checks plus 27
common policy checks and a six-witness ABI summary across **113 new entries**.
The diagnostic is **phase0.10/code11**. Native acceptance is pending. No FPS,
audio, thermal or physical Xbox accuracy improvement is inferred from this suite.

## Local validation

All 154 unique checks pass in both translation modes, retaining every earlier
127 cloud label and all reference suites. The five ABI witness controls,
54 fault-policy controls and 31,296-case native x86 division oracle outputs match
the accepted prior outputs. Five isolated allocator processes pass their internal
19-check suite. Exact eight-patch preparation, earlier-prefix upgrade, idempotence,
rejection of an extra dependency edit and real-index preservation pass.

Removing the HADDPS handler rejection makes six checks fail; inaccessible-memory
fixtures are left unadmitted. Reverting disabled-VEX #UD handling makes two fail.
Both temporary negative controls return a failed report and are restored before
positive checks. The Android full-core/diagnostic link and local phase0.10/code11
APK signature, exact two-library ARM64 inventory, system-only dependencies,
notices and APK/ELF 16 KiB alignment checks pass. Packaging alignment does not
change the 4 KiB runtime admission. Independent GitHub and native acceptance
remain separate steps.

## Remaining work

The full [instruction/virtual-state plan](phase10-plan.md) still requires broader
admission evidence, virtual interrupt/flag semantics and a deterministic guest
clock contract. General memory/bridge, REP/helper/translation/raw-fault execution
bounds, OOM/unload and 16 KiB host support remain open before arbitrary XBE/game
admission. Follow the controlled XBE, triangle/texture/PCM and retail checkpoints
before comparing frame times, audio underruns and thermals against the retained
backend.
