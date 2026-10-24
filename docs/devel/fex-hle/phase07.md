# Phase 0.7: precise privileged exits and architectural EFLAGS

The [Phase0.6 Pocket S report](phase06-android-report.md) passes all 88 checks,
accepting the scoped CPU-identity increment. This cumulative diagnostic verifies
controlled rejection of privileged instructions and fixes a public EFLAGS
reconstruction bug. It does not implement Xbox virtual privilege, later-ISA
admission, timers or bounded execution, and does not run games.

## EFLAGS correction

New arithmetic-before-fault fixtures exposed a reserved high bit in public
`ContextImpl::ReconstructCompactedEFLAGS` output. The source walked all 32 entries
of the flag-storage array, including private packed ARM NZCV bytes at slots
24..27. For some flag combinations this contributed bit 31 to the supposed x86
EFLAGS value. Different arithmetic operations could change that reserved bit
without changing any intended guest control state.

Reviewed engine patch `0006-canonical-eflags.diff` limits this accumulation to
architectural bit positions through ID (bit 21). Private and reserved high slots
are never accumulated. Arithmetic flags still come from their existing explicit
NZCV/PF/AF reconstruction. This also avoids scanning ten nonarchitectural slots;
no guest-time performance or FPS gain is inferred.

The fixture checks all **128 combinations of CF/PF/AF/ZF/SF/DF/OF**, each with
and without supplied reserved high bits: **256 API round trips** with independent
expected values. Architectural control bits through ID also alternate in these
API-only checks; they are reset before executing the guest fixtures. Tests also verify that entry/exit after guest arithmetic returns
architectural EFLAGS without those high bits. The preceding accepted checks
compared preserved state and did not establish this canonical-value property.

## Controlled privileged-instruction exits

Nineteen cases run in the Xbox-identity context, using the pinned engine's
userspace rejection behavior. Each case runs four initial/arithmetic scenarios,
then repeats from the same entry with warm translated code: **152 entries**.
Scenarios include carry/zero, signed overflow, subtraction-to-zero and preserved
externally supplied flags. The byte sequences never forward host syscalls or
perform host privilege operations.

| Cases | Expected controlled exit |
| --- | --- |
| CLI, STI | GP/error0 at the instruction |
| MOV from/to CR0 and DR0 | GP/error0 at the instruction |
| RDMSR, WRMSR, RDPMC | GP/error0 at the instruction |
| Immediate/DX IN and OUT | GP/error0 at the instruction |
| INSB, OUTSB with inaccessible destination/source | GP before any data access |
| SYSENTER, SYSEXIT | UD at the instruction |
| UD2, HLT controls | UD and GP respectively |

Every case checks the exact PC and generated exception metadata, all eight
IA-32 GPRs, EFLAGS, and a following memory/GPR sentinel that must not execute.
The string-I/O cases use a PROT_NONE page inside the already owned fixture
mapping; its permissions and the generated-exit pointers are restored afterward.
All six native ABI witnesses wrap each new entry independently of the existing
111 CPUID entries. Failure detail identifies the case, arithmetic scenario,
warm/cold entry, PC, flags, signal/trap, register and sentinel.

These exits are **not equivalent to physical Xbox ring-0 execution**. The
application runtime must eventually intercept and virtualize admitted privileged
operations, or reject/defer them explicitly. CLI/STI must not become silent NOPs:
FEX's existing flag-restoration policy forces IF on for its userspace model.
Likewise RDTSC currently reads the host counter with FEX scaling; it does not yet
implement an Xbox virtual timer. SSE2/later ISA and XGETBV are not disabled by
CPUID identity. Those policies and bounded return/cancellation remain open.

## Validation and device gate

The cumulative cloud suite expects **96 PASS checks**: 76 inherited checks,
19 precise-exit checks and one canonical-EFLAGS check. Both translation modes
must pass. The original 32-slot reconstruction is a negative control and must
fail the new canonical/exit-state checks; restoring the reviewed correction must
pass. Six-patch preparation retains exact-prefix upgrade/idempotence,
dirty-source rejection and real-index preservation.

The cumulative Android APK is **phase0.7/code8**. Its native expectation is
**109 PASS checks**: 88 accepted checks plus those 20 common checks and one
six-witness ABI summary over all 152 new entries. Native acceptance of this
increment is pending a new Pocket S report. Full bridge/memory semantics,
recoverable engine OOM/unload and 16 KiB host support remain open too. No FPS,
audio, thermal or gameplay improvement is claimed.
