# Phase 0: first FEX embedding increment

## Decision

Retain FEXCore provisionally. Its full CPU core compiles and links against Android
ARM64/Bionic with one narrow source patch. Ten constructed fixtures execute
successfully through FEX's generated ARM64 under the upstream VIXL simulator on
x86-64 Linux. The [user Android report](phase0-android-report.md) also passes
all ten fixtures in a native ARM64 process with 4 KiB pages. Basic direct Android
execution is demonstrated; the complete Phase 0 acceptance gate remains open. There is no measured title-performance improvement yet.

The first experiment build is **phase0.1**, a separate diagnostic application.
The existing full-system backend, Android build graph and C++17 setting are
unchanged. The separate FEX build uses C++20 and NDK 30.0.15729638/API 29.

## Immutable dependency inventory

FEX repository: https://github.com/FEX-Emu/FEX

Pin: `4e2b509a42b7762a48c407d6a59bb897dca1ded8`.

Exact submodule revisions live in
[`dependencies.json`](../../../experiments/fex-runtime/dependencies.json).
No floating branch or invented embedding API is used.

| Component | Purpose | License / source notice |
| --- | --- | --- |
| FEXCore, CodeEmitter, FEXHeaderUtils | DBT, ARM emitter and platform helpers | MIT; FEX `LICENSE`, core `.cpp` SPDX markers inspected |
| fmt | Formatting | MIT, `External/fmt/LICENSE` |
| rpmalloc | FEX-owned allocation outside guest VA | Permission/disclaimer notice, `External/rpmalloc/LICENSE` |
| unordered_dense | Hash containers | MIT, `External/unordered_dense/LICENSE` |
| xxHash | Core/cache hashing | BSD-2-Clause, `External/xxhash/LICENSE` |
| SoftFloat-3e | Extended x87 arithmetic | BSD-style three-clause notice in bundled source/header |
| Cephes | Extended math fallbacks | Upstream BSD relicensing correspondence in `External/cephes/LICENSE` |
| VIXL | Host-only ARM64 simulator | BSD-3-Clause, `External/vixl/LICENCE`; not linked into Android APK |

Android artifacts bundle the actual upstream notice texts. SoftFloat/Cephes are
in-tree at the FEX pin. Linux ELF loader, Linux syscall frontend, Linux thunks,
glibc jemalloc override, rootfs management, Vulkan headers, test binary archives,
Catch2, Zydis and Tracy are not dependencies of this probe. Range-v3 is unnecessary
because this increment does not compile upstream Common/HostFeatures.cpp.

## Source and build adaptations

The checked-in `0001-bionic-getdents.diff` adds an Android-only `SYS_getdents64`
wrapper where Bionic lacks glibc's exported helper. Dependency preparation checks
both the exact pin and exact source diff, and verifies every fetched submodule.

The upstream top-level CMake rejects Android and unconditionally includes Linux
frontends. Our independent CMake project instead adds the real upstream core and
its minimal dependency targets. It does not relabel Android as Linux or import
Linux syscall behavior. The `BUILD_STEAM_SUPPORT` CMake variable suppresses
upstream installation logic; no Steam code/dependency is added. The version string
is shortened to fit upstream's 32-byte CPUID string limit; the full hash remains
recorded separately.

`armxbox_fex_link_probe` links every FEXCore/FEXCore_Base object with
`--no-undefined`, avoiding a misleading success from static-library dead stripping.
The diagnostic also links against the real static engine. Native-library and APK
16 KiB alignment, APK signing and signature/alignment verification pass locally.
No general Android execution claim follows from these build checks.

## Contracts discovered during execution

1. `CreateThread()` alone is insufficient: the decoder dereferences the CS GDT
   descriptor. The adapter must own a live descriptor table and explicitly select
   IA-32 mode. A missing table caused the first harness crash and is now supplied.
2. Calls/returns require FEX's separate 4 MiB shadow stack and initial internal
   stack pointer. The Linux frontend normally supplies them. The probe now owns a
   guarded stack; omitting it caused the first CALL fixture to crash.
3. Code invalidation requires exclusive ownership of `GetCodeInvalidationMutex()`
   and both shared code-buffer invalidation and thread-local cached-range
   invalidation. Calling the latter without the lock caused a subsequent compile
   to hang: even release assertion macros evaluate their lock-probing expression.
   The fixture now follows the full contract and observes modified code.
4. Generated memory operations use guest addresses directly in this configuration.
   A C++ region lookup does not relocate them. The probe requests a low address
   as a hint and verifies the exact result without overwriting an existing mapping.
   Actual generated stores/loads observe two mappings of the same memfd backing.
5. FEX's IA-32 GPR allocation pool excludes x18; its x86-64 pool uses r18. This
   experiment forces IA-32 and adds `-ffixed-x18` for Android core compilation.
   This source inspection is not yet a device preservation witness.
6. `EnableExitOnHLT()` also selects the generated SIGSEGV-class exit path. It is
   useful for known fixture stubs, not a complete fault policy. Unexpected native
   faults still terminate this diagnostic. Unsupported guest syscalls terminate
   with a failure message instead of pretending to succeed.
7. The runtime allocator and interrupt-page protections contain 4 KiB assumptions.
   A non-4 KiB host is rejected before setup. Completing 16 KiB support requires
   more than padding APK/library segments or changing a single constant.
8. The typed bridge uses a known HLT entry, validates fixture stack/arguments,
   saves architectural state, runs a reverse guest callback, restores state,
   sets a uint32 return and performs exact stdcall cleanup. It is an exit-based
   prototype, not a general production hook API. Callback code deliberately
   clobbers SIMD and x87 controls; preservation is checked after resume.

## Phase0.2 follow-up

[Phase0.2](phase02.md) replaces the HLT shortcut with explicit pinned generated-
exception stop handlers and adds five exception/PC/resume checks. It also adds
native x18/SP/GPR/vector/FP-control witnesses and adapter-owned FPCR/FPSR
restoration. The 15 CPU checks and five witness controls pass in the cloud. The
[Phase0.2 Pocket S report](phase02-android-report.md) passes all 21 native checks
on Android 13/API 33, including the six ABI witnesses. Raw
memory-fault and MMIO recovery are not supplied by these stop handlers.

## Acceptance and next autonomous work

| Gate | Evidence | Status |
| --- | --- | --- |
| Pin/licenses/dependency boundary | Inventory, lock, reproducible preparation | Cloud increment complete |
| Android full-core compilation/link | NDK ARM64/API29, all-object link, no undefined symbols | Passed locally |
| Diagnostic packaging | Separate app, signed APK, signature/alignment verification | Passed locally |
| CPU/bridge/alias/invalidation fixtures | 10 passing checks under VIXL and in the user Android report | Passed for constructed fixtures |
| Actual Android ARM64 execution | User report: native ARM64 process, 4096-byte pages, all ten checks pass | Basic execution passed |
| Full typed bridge and state contract | Current controlled stdcall and reverse-call subset only | Partial |
| Fault recovery, MMIO and guest-PC recovery | Generated exceptions/PC/resume tested in phase0.2 cloud; no raw memory-fault adapter | Generated fixture cases pass on Pocket S; raw memory/MMIO and divide errors open |
| x18, native stack/TLS/signals, instruction cache | Phase0.2 adds six boundary witnesses; limited witness controls pass under VIXL | Six boundary witnesses pass on Pocket S; internal calls/signals/TLS/cache contracts remain partial |
| 4 KiB guest / 16 KiB host mapping semantics | Known assumptions identified; fail explicitly | Unsupported in phase0.1 |
| Xbox CPU policy | IA-32 forced; CPUID and instruction restrictions not complete | Open |
| Performance versus reference | No title runs on experimental backend | Unmeasured |

Finish the remaining Phase 0 runtime contracts before advancing to controlled
XBE/kernel HLE. Then proceed through triangle/PCM, retail checkpoint and matched
performance gates from the proposal. Audio acceptance must measure underruns and
callback timing; prior reference runs had slight crackling. Neither the working
legacy GPU/audio code nor FEX's instruction throughput establishes a title FPS
prediction for this architecture.
