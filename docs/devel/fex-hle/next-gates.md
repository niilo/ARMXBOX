# Remaining CPU embedding gates

These are findings from the pinned engine and the reference backend, not
implemented features or performance results. Phase0.4's Android signal recovery
must first be accepted from a native device report; broad HLE remains gated.

## Bounded host allocator

The current `SetupHooks(page)` path in FEX's `FEXCore/Source/Utils/Allocator.cpp`
constructs `OSAllocator_64Bit`. Its constructor in
`Utils/Allocator/64BitAllocator.cpp` calls `StealMemoryRegion` from 4 GiB through
the detected host VA limit. The reservation uses PROT_NONE and does not commit
that entire span to physical RAM. However, it occupies the normal kernel mapping
space. Later Bionic/ART allocations, library loads or thread stacks can fail or
be placed in the low address space intended for direct guest mappings.
A successful one-shot CPU report does not settle that coexistence contract.

Reuse the existing region-based allocator, rather than add another general heap,
through a narrowly scoped embedding entry point. The internal
`Create64BitAllocatorWithRegions` implementation already exists, but mutates the
region descriptors and uses MAP_FIXED inside them. Its input must therefore be
an actual, exclusively owned reservation, never merely a desired address range.
Its metadata also consumes space inside that reservation; arena sizing must
account for this overhead as well as JIT, lookup tables and runtime allocations.
The Windows-only `HookPtrs` API is not an Android implementation path.

The next allocator increment should:

- Reserve a bounded, page-aligned high-VA arena with no replacement of existing
  mappings. Check its complete extent, overflow and exclusion of guest VA.
- Publish hooks only after successful setup. Reject exhaustion explicitly and
  roll back failed initialization without leaving half-installed global state.
- Permit internal fixed remapping only within the owned arena. Keep ownership
  until all FEX allocations are dead; do not unmap live global objects.
- Exercise normal libc allocations, ordinary mmap, thread creation and an actual
  library load before and after setup. Record mapping ranges and verify that
  host mappings remain outside the reserved guest window.
- Test fragmented/exhausted reservations and concurrent host allocations. Repeat
  every CPU/ABI/fault fixture against this allocator on Android, with memory
  budget measurements. Do not infer Android coexistence from Linux alone.

## Xbox CPU and privileged-instruction policy

The reference machine selects `pentium3` in `hw/xbox/xbox.c`.
`target/i386/cpu.c` selects family 6, model 8, stepping 10 under XBOX, with the
Pentium III feature set. In contrast, pinned FEX's
`Interface/Core/CPUID.cpp::Function_01h` advertises SSE2, SSE3, SSSE3, SSE4.1 and
other later features, some depending on host capabilities. IA-32 mode alone does
not reproduce the Xbox CPU contract. The diagnostic's SSE2 fixture intentionally
checks engine state and is not a statement that the Xbox supports SSE2.

Implement a deterministic Xbox CPUID profile, including supported leaves,
vendor/signature, cache information and stable topology. Validate it against the
reference backend. Separately decide instruction availability: masking a CPUID
bit does not prevent execution of its associated instructions. Add explicit
unsupported-instruction tests and preserve fault PC/state rather than silently
accepting unknown opcodes.

Pinned FEX's dispatcher routes CLI/STI and control-register accesses to its
permission-restricted path, appropriate to its original userspace model.
An Xbox application runtime needs an explicit virtual privilege/interrupt policy
for any retained low-level operations. It must never execute them against the
host, silently treat them as no-ops, or equate a generic GP exit with complete
Xbox kernel semantics. Record which operations are intercepted, virtualized,
rejected or deferred to the reference backend.

## Execution and performance admission

Add a demonstrated bounded return mechanism before accepting arbitrary guest
entry points. Constructed terminating fixtures and the CI process timeout do not
provide cancellation of a non-terminating native guest in an app process.
Verify cooperative stop latency, safe state publication and cleanup across guest
loops, callbacks and faults without unsafe C++ unwinding.

After the CPU/bridge/memory contracts pass, advance to a controlled XBE and
minimum kernel imports, then triangle/texture and PCM timing, then a retail
checkpoint. Use the same Pocket S power mode, course, resolution and driver for
reference comparisons. Measure frame times, CPU/GPU work, memory and audio
underruns. Prior reference runs had mild crackling. No synthetic throughput or
successful fixture count establishes a title FPS improvement.
