# Phase 0.10 plan: reference-backed instruction and virtual-state policy

The [Phase0.9 Pocket S report](phase09-android-report.md) accepts all 148 scoped
native checks. The next gap is the difference between the Xbox CPU identity and
the instructions/state the engine actually admits. This is a compatibility gate
before controlled XBE admission, not a claimed FPS optimization.

## Source findings

At pinned FEX `4e2b509a42b7762a48c407d6a59bb897dca1ded8`, the immutable
XboxReference profile controls CPUID; it does not restrict modern instruction
dispatch. `OpcodeDispatcher.cpp::RDTSCPOp` considers host Linux/CPU-index support,
not the Xbox's lack of RDTSCP. XGETBV and later SIMD have their own dispatch paths.
A missing CPUID bit alone is not instruction rejection.

`Core.cpp::SetFlagsFromCompactedEFLAGS` forces IF=1 under the original userspace
contract, including the current identity profile. All accepted restore fixtures
use IF=1. CLI/STI currently produce precise controlled #GP exits; these are not
virtual Xbox interrupt semantics. Do not silently convert them to no-ops.

`RDTSCOp` uses `CycleCounter(false)`, the ARM64 backend reads CNTVCT(_SS)_EL0,
and the context constructor can select a power-of-two host-counter scale.
Changing CPUID identity does not create an Xbox virtual clock. The retained
reference `helper_rdtsc` checks virtual CR4.TSD/CPL and reads `cpu_get_tsc` plus
an offset. Clock rate, pause/resume, continuity, scheduler/audio relationships
and overflow need their own explicitly owned policy and tests.

## First reference capture completed

The [instruction capture](../../../experiments/fex-runtime/reference/README.md#instruction-admission-capture)
runs 12 selected encodings twice in protected IA32 mode with SSE enabled, and
repeats identically in a second fresh emulator process. SSE1 XORPS, MMX PXOR and
x87 FLD1 execute; the tested SSE3/SSSE3/SSE4.1/SSE4.2/AVX, RDTSCP, XGETBV and UD2
forms raise #UD with exact PC, preserved sampled flags/registers and untouched
following sentinel.

**The tested SSE2 PXOR XMM form also executes despite CPUID hiding SSE2.** This
is retained-reference compatibility behavior, not a certification of physical
Pentium III instruction support. Do not turn a CPUID bit into blanket decoder
rejection or silently treat reference permissiveness as Xbox hardware support.
Document the policy choice per family and gather architectural/physical evidence
where reference behavior is ambiguous. The first opt-in [instruction-policy increment](phase10.md) is now implemented;
its native acceptance is pending. It does not implement virtual flags or clocks.

The reusable capture tool, immutable binary/ROM/assembly checksums and full
outputs are committed. Replacing the UD2 control with NOP makes an actual ROM
capture fail; corrupted PC/sentinel/flags outputs are also rejected. The generated
ROM needs no proprietary firmware, game data or Pocket S input.

## Sequence

1. **Extend reference admission fixtures (first 12 forms captured).** Generate a firmware-free protected
   IA32 mode ROM for the realized Pentium III Xbox reference machine. Enable
   SSE/MMX correctly in CR0/CR4 and demonstrate accepted SSE1/MMX/x87 controls.
   Capture actual #UD/#GP, fault PC, error, flags and following sentinel behavior
   for selected later ISA, RDTSCP, XGETBV and explicit UD2. Repeat captures and
   record immutable reference revision and binary/fixture checksums. Do not use
   FEX output or the cloud host CPU as the expected result. Reference-emulator
   evidence is distinct from physical Xbox hardware.
2. **Choose immutable instruction policy separately from identity.** Preserve
   default modern engine capability tests and existing Xbox CPUID isolation
   witnesses. Add an explicit opt-in admission policy before translation, with
   incompatible cache/mode guards. Inspect decoder classes and prefix forms;
   do not mutate a shared global opcode table or infer all forms from one opcode.
3. **Implement a documented first rejection subset.** Match executed reference
   cases with exact guest state and sentinel preservation, including cold/warm
   code and both translation modes. Test allowed SSE1/MMX/x87 and unrelated
   default contexts at identical guest addresses. Keep ambiguous instruction
   families unadmitted to broad execution rather than call a small matrix a
   complete Pentium III decoder. Add negative controls that remove rejection.
4. **Define virtual interrupt state before supporting it.** Decide which
   application-level operations are intercepted, virtualized, rejected or deferred
   to the reference backend. Preserve default FEX userspace behavior. Tests must
   demonstrate IF=0/1 across API, execution, snapshots, callbacks and relevant
   guest flag instructions before claiming virtual interrupt acceptance. Native
   Android interrupt/host privilege state must never be affected.
5. **Add a deterministic guest clock contract.** Audit and capture the configured
   reference clock; specify rates and lifecycle rather than merely tune a host
   counter shift. Test monotonicity, pause/resume, wrap, multi-entry consistency
   and scheduler/audio integration. Keep deterministic clock correctness separate
   from throughput benchmarks and actual game timing compatibility.

Stage these as reviewable increments. Establish exact new fixture identities
and native counts only after implementation. Retain all 148 accepted native and
127 cloud checks. Require Android execution of any new native policy paths,
full-core link and APK verification before acceptance. Generated fixtures may
advance independently in the cloud; Android ABI/concurrency cannot be inferred
from them.

## Limits

This does not admit arbitrary XBE entry points. General memory/bridge, REP and
helper/raw-fault bounds, code publication, OOM/unload and 16 KiB hosts remain
open. Continue toward a generated controlled XBE/minimum imports after its
required contracts are established, then triangle/texture/PCM, a retail checkpoint
and matched frame-time/audio/thermal comparisons against the retained backend.
