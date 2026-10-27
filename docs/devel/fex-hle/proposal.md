Assessment
The proposed direction is a credible experimental architecture: translate original Xbox IA-32 game code to AArch64, replace selected Xbox services with native implementations, and render through Vulkan. It is a substantial new execution backend with a compatibility tradeoff. Calling it a wrapper refactor understates the work.
Three assumptions must change before implementation:
1. Xbox XDK graphics and audio code is generally statically linked into the XBE. Kernel ordinal imports and recognized XDK machine-code functions need different interception mechanisms. A Box64 ELF/DLL-style loader cannot discover all of them.
2. Vulkan and Oboe supply host rendering and audio output. They do not implement Xbox graphics, audio, synchronization, object, or timing semantics.
3. Xbox physical RAM size does not define its entire guest virtual address space. Backing RAM with mmap does not by itself implement virtual mappings, aliases, protection, or device accesses.
Keep the existing full-system backend available as a reference and as a launch-time compatibility option while the new runtime matures. Removing full-system emulation from the new runtime remains the target. Do not delete working components before demonstrating the replacement.
Evaluate FEXCore first as the CPU translation foundation. Keep Box32 and suitable TCG reuse as alternatives. This preference reflects FEXCore's modular design and FEX's 32-bit guest support; it does not establish Android portability, Xbox compatibility, or a performance advantage. Box64's typed-wrapper design remains a useful reference. The new runtime must supply Xbox semantics regardless of the CPU engine selected.
This review inspected source and documentation. No emulator build, device test, compatibility test, or performance benchmark was executed. Performance improvements remain a hypothesis to measure.
Repository findings to preserve
- ARMXBOX currently packages xemu/QEMU for Android ARM64, with API 29 as its minimum and Vulkan required. The Android build uses C++17.
- The existing Vulkan code is under hw/xbox/nv2a/pgraph/vk/. It already includes rendering, resource handling, and asynchronous shader compilation. Current graphics are not simply software rasterization; NV2A command and state emulation feeds a native Vulkan backend.
- Existing lazy condition-code machinery is present in target/i386/tcg/translate.c and target/i386/tcg/cc_helper.c. Audit and reuse its semantics where practical; do not claim lazy flags are a new optimization over this baseline.
- xemu-xbe.c reads information about an already running XBE through the emulated machine. It is not a standalone executable loader for the proposed runtime.
- Renderer sources reference QEMU synchronization and NV2A state. Reuse requires a dependency assessment and explicit interfaces.
- Existing Android build rules glob source files. Explicitly control new target membership so experimental sources do not enter the old backend accidentally.
Instructions for the implementing coding agent
Objective and scope
Build a feature-gated, application-level original Xbox runtime for a normal Android ARM64 app process. It must execute 32-bit x86 code through DBT and provide Xbox-compatible services through HLE and selected device-command emulation.
Embed FEXCore through a small, project-owned CPU adapter if it passes Phase 0. Use its existing IA-32 decoder, ARM64 translation, flag handling, floating-point support, and code-cache mechanisms where suitable. Keep Xbox loading, kernel HLE, scheduling, memory policy, and device behavior in the ARMXBOX runtime. Do not assume FEXCore is a complete Xbox runtime or that its API is a stable drop-in interface.
Use Box32/Box64 as references for typed wrapping, callbacks, and guest/host pointer conversion, and as an alternative CPU foundation when justified by the feasibility checks. Box64 primarily targets x86-64; compare its Box32 mode for this 32-bit guest. FEX also has thunk and callback infrastructure, which requires adaptation to Xbox calling conventions and static-library interception.
Keep existing project code at C11/C++17 where applicable. Permit C++20 for pinned FEXCore and its adapter targets: current FEX build files require C++20. Verify the selected Android NDK and standard-library requirements, scope compiler options per target, and avoid changing the entire project unnecessarily. Prefer a narrow C-compatible adapter boundary with fixed-width data and explicit ownership/error rules; do not expose dependency-specific C++ types across it.
Keep the existing Android UI and storage integration where useful. Run directly inside a normal Android app process. Do not add a Linux rootfs, proot/container environment, Wine frontend, or root requirement to satisfy this design. Demonstrate that the chosen embedded-core configuration works without those components.
The initial deliverable is a small, compiling, tested vertical prototype, followed by incremental compatibility work. Do not represent interfaces or unsupported-function stubs as a working emulator.
Phase 0 — Establish the baseline and prove FEXCore feasibility
1. Read repository guidance and inspect the actual build graph. Record the exact commit, toolchain, Android API level, Vulkan dependencies, and reusable components.
2. Profile repeatable scenes in a small user-selected title set. Separate translated CPU execution, memory helpers, GPU command processing, Vulkan execution/submission, shader compilation, audio/DSP work, and synchronization waits.
3. Measure frame-time distributions, CPU time, memory use, shader stalls, and audio underruns. Record resolution, driver, power mode, and warmed versus cold caches. Use a repeatable sustained run to expose thermal effects.
4. Start with the pinned FEX commit above. Build only the components needed for an embedded CPU experiment and identify remaining Linux/frontend dependencies. Exercise that experiment inside an APK on Android ARM64 using the project's NDK, rather than treating a Linux/proot demonstration as Android app integration.
5. Execute generated 32-bit x86 instruction fixtures, then implement one Xbox-style guest-to-native call and a reverse callback. Verify exact stack cleanup, guest register/FPU preservation, fault recovery, and resume behavior. Include x87, MMX/SSE, deferred flags, and guest FS/TLS cases. Keep CPU features restricted to the intended Xbox model.
6. Audit and demonstrate the engine's address-space contract. Determine whether it assumes direct/identity guest-to-host mappings, requires low-address reservations, or supports a guest base. Prove the guest mappings, shared RAM aliases, 4 KiB/16 KiB host-page handling, MMIO exits, and code invalidation required by this runtime. Do not assume arbitrary-base guest memory works without changes to generated memory accesses.
7. Audit generated code and assembly for Android ABI compliance, especially reserved x18, stack alignment, thread-local storage, host signal contexts, and instruction-cache synchronization. Verify executable-code publication on the target device. Test sustained execution and bounded code-cache growth on Pocket S when available.
8. Produce an architecture decision record identifying reused components, required engine patches, unresolved gaps, build/dependency costs, and the selected CPU foundation. Pin dependency commits, review all linked components' licenses, and retain required notices. Do not infer that FEX is faster than Box32 from its compiler architecture or Linux game benchmarks.
Candidate selection and acceptance
Candidate	Role in this plan	Evidence required before adoption
FEXCore	First CPU candidate; existing modular translation core	Direct Android APK execution, correct 32-bit state/native bridges, viable memory/device-access handling, acceptable dependency and patch burden
Box32 in Box64	Alternative if its Android integration or adaptation resolves a demonstrated FEXCore blocker	The same correctness and integration checks; verify the precise Box32/Android configuration rather than assuming Box64 support proves it
Suitable TCG reuse	Alternative and correctness/performance reference close to the existing codebase	A concrete integration/dependency assessment and a working experiment; avoid extracting the full system runtime accidentally


Adopt FEXCore when the experiment passes the required contracts with a maintainable patch set. If Android dependencies, memory mapping, guest exits, privilege-sensitive behavior, or callbacks block the design, record the exact blocker and test the relevant alternative. Replacing the translator is not sufficient when the missing behavior belongs to Xbox kernel or device emulation.
Acceptance: a reproducible baseline, a passing direct-Android CPU/bridge experiment, an explicit memory and exception contract, and a documented CPU-engine decision. Do not proceed to broad HLE implementation while those contracts remain unproven. Do not start a custom optimizing JIT solely because a translation-loop stub looks simple.
Phase 1 — Guest state, address space, XBE loader, and kernel minimum
CPU contract
Define CpuState32 as an engine-independent view/snapshot of guest architectural state. Include IA-32 general registers, EIP, reconstructible EFLAGS, segment/TLS state needed by the supported guest, x87/MMX state, SSE/MXCSR state, and guest exception information. Let the selected engine own live translated CPU state; do not maintain an unsynchronized second authoritative register file or force every guest instruction through a state copy.
Define a CpuBackend adapter for initialization, guest-thread contexts, run/resume, native-call exits, state inspection, and code invalidation. These are project-owned operations whose implementation must be matched to the pinned engine's actual APIs. Do not invent FEXCore functions or assume a generic MMIO/hook callback exists.
Define explicit exits for native calls, guest faults, scheduling, unsupported instructions, and device accesses. Establish a correctness oracle using independent TCG/reference execution, native x86 fixtures where applicable, and known architectural results. Do not require an interpreter from an engine that does not supply one.
FEXCore's documented execution scope is userspace/CPL3, with kernel privilege levels and hardware interrupts outside its goals. Xbox behavior involving privileged instructions, port I/O, direct MMIO, segment state, or time reads must enter controlled guest handlers or be identified as unsupported. Prototype the actual exit/emulation mechanism before assuming these operations are covered. Do not silently execute them as host operations or discard them.
Memory contract
Implement GuestMemory with separate concepts for guest virtual addresses, backing physical RAM offsets, and host pointers.
- Default to 64 MiB of guest physical RAM. Make 128 MiB an explicit supported configuration, not a blanket assumption for retail games.
- Choose guest mapping placement from the proven CPU-engine contract. Evaluate sparse reservations and shared backing for the 32-bit guest address space; an arbitrary host-selected base is valid only when all translated guest memory accesses support it. Virtual reservation does not imply 4 GiB of committed RAM.
- If required low-address reservations or alias arrangements are unavailable, implement and benchmark an explicit software translation alternative supported by the adapter/engine, or choose another foundation. A region lookup API in the HLE layer alone does not redirect memory accesses emitted by the JIT.
- Use a fast translated RAM access path where mappings permit it, and slow paths for permissions, faults, aliases, device regions, and unusual accesses.
- Preserve required aliases by sharing backing storage. Do not treat guest virtual addresses as physical offsets.
- Keep guest pointers as uint32_t values. Translate and validate pointer-plus-length ranges without integer overflow; represent native pointers and objects separately.
- Query host page size. Support both 4 KiB and 16 KiB Android hosts. Keep guest protection/accounting granularity independent of host mmap/mprotect granularity; use software checks where subpage permissions cannot be represented.
- Handle accesses crossing guest-page boundaries. Track code and resource writes by backing identity so writes through aliases invalidate the correct JIT blocks and GPU resources.
- Avoid unreserved MAP_FIXED mappings. mmap does not make arbitrary process memory valid guest memory and does not make ordinary CPU allocations automatically importable into Vulkan.
- Ensure direct-address memory paths cannot silently treat unrelated native process memory as valid guest memory. Translate native faults to guest faults using recoverable guest-PC/state information.
A fast path may eliminate guest hardware page-table walks after HLE takes responsibility for mappings. It must retain the required guest memory semantics.
Loader and kernel minimum
Implement an XBE loader that validates headers and section ranges, loads and zero-fills sections, resolves entry-point/kernel-thunk encoding for supported image types, initializes TLS and stacks, and dispatches kernel imports by ordinal. Cover imported data objects as well as functions.
Implement the smallest kernel service set required by the first test program: allocation, guest threads, events/waits, timers, basic file access, handles, and exception delivery. Expand from observed imports and behavior.
Model guest FS/TLS and scheduling explicitly. Do not expose Android libc/pthreads as direct Xbox kernel substitutes. Define guest time, thread-safe-point delivery of callbacks/APCs, and the handling of unsupported services. Host signals or C++ exceptions must not accidentally become guest exceptions.
Acceptance: load a controlled XBE, execute its entry point, call tested kernel services, and exit deterministically with actionable diagnostics for unsupported behavior.
Phase 2 — Native bridge and interception infrastructure
Keep the requested wrappedxbox.c name if helpful, but implement an Xbox hook registry and dispatcher rather than pretending it is an ELF dynamic linker.
Use two resolution paths:
1. XBE kernel imports: resolve verified ordinal entries to guest-callable bridge tokens or guest trampoline addresses.
2. Static XDK routines: identify supported functions using version-aware signatures, cross-reference checks, or explicit verified title profiles. Library-version metadata narrows candidates; it is not a complete symbol table.
Consider XbSymbolDatabase as a source of existing research and detection infrastructure after checking its integration and license. Account for LTCG, inlining, stripped symbols, and compiler variants. Reject ambiguous matches; never patch the first similar byte pattern.
Prefer DBT entry-address interception so ordinary guest code bytes need not be rewritten. Invalidate translated blocks and hook decisions when relevant code is modified or sections are unloaded.
For FEXCore, first verify available interception/thunk mechanisms and their code-cache implications. If entry-address interception needs engine changes, evaluate generated guest trampolines at verified entry points as an explicit alternative. Use instruction-safe patching and invalidation, and retain the original implementation where required. FEX's existing Linux library-thunk setup does not identify static Xbox XDK functions or automatically supply their guest ABI.
Typed wrapper manifest
Use an X-macro/SUPER-style table or equivalent generator. Each entry must identify:
- Hook ID and resolver kind: kernel ordinal, verified signature, or explicit title profile.
- Applicable library versions and matching evidence.
- Exact guest ABI, argument locations, widths, signedness, and return convention.
- Stack bytes removed by the callee.
- Pointer direction, length expression, structure layout, lifetime, and object-handle policy.
- Whether the call may block, invoke guest callbacks, modify guest-visible state, or fall back.
Generate typed C/C++ adapters. Do not cast unrelated native functions to a generic function-pointer type and call them. Initially exclude varargs and unsupported return conventions explicitly.
Bridge semantics
Guest register allocation is an internal DBT choice; guest EAX does not inherently map to host x0.
At each bridge exit, use the engine's tested state synchronization/exit mechanism, decode arguments from the guest architectural state and validated guest memory, invoke an ordinary typed host function using the native ABI, encode the guest return, and resume through the adapter. Keep lazy flags and cached floating-point state reconstructible across these transitions.
Support cdecl/stdcall initially. Implement thiscall, fastcall, and verified custom/LTCG register conventions before intercepting functions that use them.
- Model return-address handling and stack cleanup exactly once. At a normal intercepted function entry, cdecl return consumes the 4-byte return address; stdcall additionally consumes the signature's stack arguments.
- Encode returns into EAX, EDX:EAX, x87 ST(0), or another verified location according to the guest signature.
- Preserve guest callee-saved registers and guest floating-point state according to the actual guest ABI.
- Use separate host execution stacks with AAPCS64 alignment and preservation rules. Reserve Android x18.
- Marshal 32-bit structures, nested pointers, COM-style objects, vtables, and function pointers explicitly. Store host object references behind guest-compatible handles or objects.
- Provide reverse trampolines for callbacks with re-entrancy and lifetime rules. Queue asynchronous callbacks to the guest scheduler rather than executing guest code on an audio callback thread.
- Verify synchronous nested callbacks and independently scheduled guest callbacks separately. Do not assume an engine's host-to-guest callback facility supports arbitrary external threads or out-of-call-context entry.
- Define pointer ownership for asynchronous rendering/audio. Pin, retain, or copy data until consumers finish.
Acceptance: test exact stack deltas, integer and floating returns, preserved registers, invalid pointers, guest-object lifetime, nested callbacks, and blocking-call resumption. Include an ABI test with guest arguments in registers.
Phase 3 — IA-32 execution correctness, then DBT optimization
Cover the CPU feature set actually exposed by the emulated Xbox CPU. Include tested x87, MMX, SSE, unaligned accesses, atomic operations, and exception behavior. Do not accidentally advertise later instruction extensions merely because the chosen engine supports them.
Integrate the selected engine's existing decoder/JIT, flag machinery, floating-point implementation, and code cache. Do not rewrite those subsystems as a parallel custom translator. Use independent reference results and differential tests before changing them. Unsupported instructions must exit explicitly.
Correct lazy EFLAGS
Preserve and validate the engine's existing deferred-flag implementation. Where adapter code or an engine patch manipulates flags, retain the producing operation, operand width, operands/result, and defined/preserved/undefined flag behavior needed to reconstruct them. Resolve only the flags a consumer needs, or retain a provably equivalent host condition representation. A separate lazy_flags implementation is not a required deliverable.
Consumers include Jcc, SETcc, CMOVcc, ADC/SBB, carry rotates, LAHF/PUSHF, and other instructions that read flag state. Preserve or reconstruct flags for guest exceptions and observable state snapshots. Some branches do not read EFLAGS at all.
Handle partial flag updates, especially INC/DEC preserving CF, and instructions such as SAHF/POPF that replace subsets of state. An ARM NZCV cache must account for x86 carry/borrow differences and flags not represented by NZCV, including PF and AF.
Preserve lazy state across block boundaries and native exits, or reconstruct it where the boundary contract requires. Faulting instructions must expose the correct pre-fault architectural state.
JIT runtime
Reuse the engine's code-cache and linking infrastructure. Verify its bounds, safe invalidation/unlinking, self-modifying-code detection through the runtime's aliases, instruction-cache synchronization, and guest-PC recovery for faults. Implement missing adapter behavior explicitly. Preserve correct x86 memory ordering and LOCK semantics on ARM64; relaxed engine options need separate correctness evidence before use.
Publish executable code using a supported write/execute protection strategy; do not assume permanent RWX mappings. Keep code publication and patching safe across executing threads.
Acceptance: differential tests covering arithmetic edge cases, carry chains, partial register writes, partial flags, x87 rounding/precision, SIMD state, faults, atomics, code modification, and block transitions. Do not claim a generic dispatch loop is a functioning optimizing DBT.
Phase 4 — Hybrid graphics frontend and Vulkan backend
Implement wrapped_d3d8.cpp as an Xbox-specific HLE frontend. Desktop D3D8 wrappers are references only; the Xbox API, resource layouts, shaders, and command stream require explicit translation.
First assess which existing NV2A/Vulkan components can be separated behind guest-memory, synchronization, and renderer interfaces. Reuse shader/resource research and driver workarounds where practical.
Provide two explicit frontend strategies:
- Verified XDK HLE hooks lower recognized API operations into renderer commands.
- A supported NV2A push-buffer/device path handles code that submits raw commands, uses inline XDK routines, or bypasses recognized hooks.
Decode push-buffer packets, subchannels, methods, control flow, DMA addressing, and completion semantics for the supported subset. Xbox push buffers cannot be submitted directly as Vulkan command buffers.
Implement incrementally: clear/present, vertices and indices, textures and swizzling, shader translation/register combiners, render targets/depth, blending and relevant fixed-function state, then queries/fences and resource synchronization.
Use explicit command/resource ownership across guest, renderer, and GPU threads. Prevent stale texture uploads, premature reuse, incorrect fence completion, and CPU/GPU visibility errors. Avoid assuming zero-copy is available for arbitrary mmap-backed guest RAM.
Mixed HLE and raw-command execution requires a proven coherent state model, including resources, ordering, and synchronization. Until that exists, choose a supported frontend mode for the title/run. Do not silently switch representations mid-frame.
Acceptance: deterministic image comparisons for controlled scenes, Vulkan validation in debug runs, resource-lifetime tests, and a title that reaches a defined gameplay checkpoint. Measure frontend CPU savings separately from GPU time.
Phase 5 — Audio semantics and Android output
Implement wrapped_dsound.cpp as an Xbox audio frontend. Put an emulated mixer/stream processor between guest APIs and Oboe.
Start with PCM buffer creation, format negotiation, play/stop/pause, looping, volume/pan, and guest-visible playback positions. Add streaming packet completion, resampling, Xbox ADPCM decoding, and 3D/effects behavior according to title requirements.
Do not confuse DirectSound calls with already mixed PCM. Oboe replaces the output sink; it does not replace guest voices, codecs, completion notifications, APU semantics, or custom DSP behavior.
Use a preallocated audio queue and bounded real-time processing. The Oboe callback must not block, allocate, perform file I/O, or execute guest code. Handle stream disconnects/reconfiguration and pause/resume outside the real-time callback.
Define audio clock, guest clock, video synchronization, sample-rate conversion, underrun recovery, and callback delivery through the guest scheduler.
For unsupported raw APU/DSP behavior, report the limitation and select the existing full-system backend on a fresh launch when appropriate. A running HLE runtime cannot magically transfer its state into the full emulator.
Acceptance: tested loop boundaries and playback positions, packet-completion ordering, sustained audio with underrun counters, and measured audio/video drift over a repeatable run.
Phase 6 — Vertical prototype, then compatibility expansion
Use these gates to determine implementation order. CPU, loader, bridge, graphics, and audio work may be interleaved to reach each gate.
Gate	Required observable result
A	FEXCore-first experiment runs in an Android APK, executes IA-32 fixtures, performs a typed native call and reverse callback, and proves guest mappings, fault recovery, and invalidation. Adopt an alternative only with a documented reason and equivalent checks.
B	Controlled XBE loads, initializes guest state, invokes kernel services, and exits.
C	Controlled guest path renders a triangle/texture and plays a looping PCM stream with verified guest timing.
D	First selected retail title reaches an agreed gameplay checkpoint with documented hook and instruction coverage.
E	Repeated scenes show measured frame-time or resource improvements against the retained baseline without unacceptable correctness regressions.


Use homebrew, generated instruction fixtures, and user-supplied assets. Do not make redistribution of commercial games or proprietary SDK libraries a test requirement.
Prefer the AYANEO Pocket S as an Android performance target if available; record actual OS, GPU driver, and device configuration. Use another relevant Android GPU family to distinguish architecture bugs from driver behavior. Steam Deck may be a desktop reference/secondary backend target if retained, but its x86-64 CPU does not validate ARM64 translation.
Expand compatibility by observed missing services and verified title coverage. Keep a per-title report of unsupported CPU instructions, kernel imports, unmatched hooks, raw GPU methods, audio features, and known rendering/timing differences.
Proposed deliverable layout
Names are suggestions to adapt to the repository's build conventions.
Area	Suggested files	Responsibility
Runtime	cpu_state32.h, cpu_backend.h, guest_runtime.cpp	Architectural state view, engine boundary, exits, guest scheduling
Memory and loader	guest_memory.cpp, xbe_loader.cpp	Address translation, RAM/aliases, XBE initialization
Kernel	kernel_exports.cpp	Ordinal functions/data, handles, minimum services
Hooks and bridge	wrappedxbox.c, hook_manifest.inc, abi_bridge.cpp	Resolution, typed metadata, guest/native transitions
CPU engine	fex_cpu_backend.cpp, scoped dependency/build targets	FEXCore integration, synchronized state, guest exits, flags and code-cache lifecycle; replace adapter if another engine is selected
Graphics	wrapped_d3d8.cpp, nv2a_commands.cpp, renderer adapter	HLE/raw frontends and Vulkan integration
Audio	wrapped_dsound.cpp, audio_mixer.cpp, android_audio.cpp	Guest audio semantics, mixing, Oboe
Validation	separate host/Android test targets	ABI, CPU, memory, images, audio, integration


Output requirements for each implementation increment
1. Supply a compiling patch with explicit build-target integration and the commands actually run.
2. Separate implemented behavior from interfaces, test doubles, and unsupported cases.
3. Return structured unsupported results for missing services. Never return success or fabricate resources just to continue boot.
4. Include focused correctness checks for the changed emulator contract. Keep unsupported APIs observable through bounded logging and counters.
5. Report baseline and new measurements using identical scenes/settings. Do not promise a fixed speedup, FPS, or memory reduction without measurements.
6. Keep the original backend buildable and selectable until compatibility and performance evidence justify retiring it.
7. Start with the FEXCore feasibility experiment and bridge demonstration in Phase 0. Produce the architecture decision, dependency map, and next concrete patch scope before broad kernel/HLE work; continue autonomously where evidence supports routine choices.
8. Keep a reproducible record of Android portability patches, host-page assumptions, engine configuration, and upstream commit. Clearly distinguish core-engine issues from Xbox runtime and graphics/audio issues.
Source references
Repository findings retain the reviewed ARMXBOX baseline snapshot. FEX documentation and build files were examined on 22 October 2026; its default-branch candidate commit was confirmed on 23 October 2026. Architectural recommendations above are proposed design decisions, not claims that the prototype already works. Pin source dependencies during implementation and revalidate APIs at the selected commit.
- ARMXBOX README
- Repository architecture/build guidance
- Current Android native build
- Existing x86 condition-code translation
- Existing x86 condition-code helpers
- Current XBE information reader
- Existing Vulkan backend build sources
- Existing Vulkan render-thread dependencies
- FEX overview and 32-bit guest support
- FEXCore library goals and userspace execution scope
- FEXCore C++20 build configuration
- FEX source outline and translation components
- FEX thunk/callback infrastructure
- FEX modular-core/Wine integration overview
- Box64, including its 32-bit compatibility guidance
- Box64 Android and Box32 build options
- Xbox Symbol Database
- Cxbx-Reloaded implementation research
- XBE format research by Caustik
- OpenXDK Xbox execution-environment research
- Intel instruction reference, including ADC and CMOVcc
- AArch64 procedure-call standard
- Android ABI rules, including reserved x18
- Android host page-size requirements and testing
- Android Oboe low-latency callback guidance