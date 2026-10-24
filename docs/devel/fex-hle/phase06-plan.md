# Next increment: deterministic Xbox CPU policy

This is an implementation plan and source audit, not an implemented CPU profile
or a new APK. The [Phase0.5 Pocket S report](phase05-android-report.md) accepts
the bounded allocator increment. CPU policy can now advance independently of
further device input. Full memory/bridge and bounded-execution gates remain open.

## Reference behavior to preserve

`hw/xbox/xbox.c` selects `pentium3`. In `target/i386/cpu.c`, the XBOX-specific
model has family 6, model 8, stepping 10, basic level 2, Intel vendor and extended
level zero. Its architectural signature is therefore `0x0000068a`. Leaf 1 EBX
is explicitly zero for XBOX. Leaf 2 has a dedicated Xbox override:

| Output | EAX | EBX | ECX | EDX |
| --- | --- | --- | --- | --- |
| Xbox cache descriptors | `03020101` | `00000000` | `00000000` | `0c040841` |

Use this override, not the generic legacy Intel cache description beside the
model definition. CPUID inputs above the selected range's limit are redirected
to the maximum basic leaf by `cpu_x86_cpuid`; an unsupported leaf is not simply
an all-zero result. Preserve this behavior, including high and extended inputs,
instead of introducing modern topology or host-dependent information.

The declared `PENTIUM3_FEATURES` mask is subject to model expansion and
`x86_cpu_filter_features`. Capture the **realized Xbox machine's outputs** before
freezing a feature golden; a copied model declaration is not a runtime oracle.
Distinguish emulated CPU capabilities from operations that an application HLE
runtime can actually service, especially MSRs, system entry and interrupt state.

## Pinned engine integration findings

At FEX pin `4e2b509a42b7762a48c407d6a59bb897dca1ded8`:

- `Interface/Core/CPUID.cpp::Function_01h` exposes modern features such as SSE2,
  SSE3 and SSE4.1. Other feature bits depend on host capabilities. Its topology
  includes the host-derived CPU identifier. This must not define Xbox identity.
- `Interface/Core/OpcodeDispatcher.cpp::CPUIDOp` emits a CPUID IR operation.
  `Interface/Core/JIT/BranchOps.cpp` calls pointers installed by `JIT.cpp` to
  `CPUIDEmu::RunFunction`. Public `ContextImpl::RunCPUIDFunction` also delegates
  to that object. Changing only a frontend query wrapper would leave generated
  guest CPUID unchanged. The profile must cover both paths.
- `PermissionRestrictedOp` produces a GP-class guest exit. That is a useful
  rejection mechanism, but it does not implement Xbox kernel privilege state.
- XGETBV has its own dispatch and handler path. Hiding XSAVE in CPUID does not
  disable XGETBV or other later instructions. Instruction admission requires a
  separate policy and exact fault-state checks.

## Implementation sequence and acceptance

1. Capture reference CPUID leaves 0, 1 and 2, repeated subleaf values, unsupported
   basic leaves, extended/hypervisor ranges and maximum uint32 inputs from the
   actual Xbox configuration. Record reference commit and configuration. Compare
   repeated calls to establish stable results; do not query the cloud host CPU
   as a substitute for the emulated Xbox.
2. Add an explicit, immutable per-context Xbox profile through a reviewed engine
   patch. Select it before translation and keep the default FEX diagnostic mode
   available. Prevent cross-profile translated-code reuse. Validate public API
   and executed IA-32 CPUID instructions against the same reference captures in
   both single-block and multiblock translation.
3. Add instruction and privilege policy independently. Establish a documented
   supported/rejected/deferred matrix for later ISA, CLI/STI, control/debug
   registers, MSRs, port I/O, system entry, HLT and timestamp access. Every
   rejected fixture must preserve the faulting PC, GPRs and flags and must not
   execute its following sentinel. Implement only explicitly admitted virtual
   operations; do not turn privileged instructions into silent no-ops.
4. Keep existing SSE2 tests as engine-capability regression checks in the default
   mode. Add Xbox-mode witnesses for supported SSE/MMX/x87 and rejection of
   unsupported later ISA. A passing SSE2 fixture is not Xbox ISA acceptance.
5. Re-run the accepted 75 native checks alongside the new policy checks after
   host/reference validation and Android packaging. Report any changed check
   inventory explicitly. Retain 4 KiB host admission and the bounded allocator.

Do not run arbitrary XBE entry points after these policy checks alone. Demonstrate
bounded guest return/cancellation, complete the required bridge/memory contracts
and establish ownership on callbacks/faults before advancing to the loader.
No speedup is inferred from this policy increment; its purpose is deterministic
compatibility and controlled execution before title performance work.
