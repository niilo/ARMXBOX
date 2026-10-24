# Phase 0.5: bounded host allocation and Android coexistence

The [Phase0.4.1 Pocket S report](phase041-android-report.md) passes all 56 native
checks. This cumulative increment switches the diagnostic to a bounded host
allocator and retains those CPU, exception, memory and ABI checks. Its Android
coexistence behavior is pending a new device report; it still does not run games.

## Ownership and setup

Reviewed engine patch `0004-bounded-host-arena.diff` adds a Linux/Bionic
`SetupBoundedHooks` entry point. Rather than scanning and reserving all free VA
above 4 GiB, it requests one kernel-chosen PROT_NONE anonymous reservation without
MAP_FIXED. Concurrent ordinary host mappings therefore cannot be overwritten.
It validates the 4 KiB host-page contract, reservation size/alignment, high-VA
placement and overflow. Invalid inputs and initial reservation/protection failures
return an error before publishing hooks. Repeated initialization is rejected.

The diagnostic currently reserves **2 GiB of virtual capacity**. This is not a
2 GiB physical-memory commitment or a limit on total process VA/RSS. The existing
region allocator places its metadata, new hooked allocations and fixed remaps
inside the owned reservation. Host allocator hooks reject low guest addresses,
out-of-arena fixed mappings/unmaps, zero and overflowing lengths. Direct guest
mappings continue to be owned and managed separately by the frontend.

The allocator's object is intrusively stored inside that reservation. Its
lifetime must not end through the normal rpmalloc object deleter. The bounded
owner is explicitly retained until process exit, following the rationale of
upstream `ReleaseAllocatorWorkaround`. Live runtime/global objects are never
unmapped by probe cleanup. Reinitialization, full library unload and general
recoverable engine OOM are not supported by this increment.

An initial 1 GiB cloud prototype refused a code-buffer allocation: existing
caches, metadata and transient allocations exceeded the admitted capacity.
Upstream code-buffer construction did not recover from that refusal. The 2 GiB
probe passes the full suites and explicit request-level exhaustion controls;
this is not proof that every engine allocation failure is recoverable. An
intermediate intrusive-owner teardown crash was also caught and corrected;
cloud tests now require successful process exit as well as PASS lines.

Pre-initialization library/allocator objects and normal host heaps may lie outside
the bounded reservation. The cap bounds the new hooked mapping arena, not every
allocation ever made by the process. Reported VmSize/RSS snapshots are process
totals; they are not measurements of FEX alone or of title performance.

## New checks

Seventeen setup/ownership/coexistence checks and two post-CPU checks exercise:

- Invalid page-size, too-small, unaligned and overflowing setup inputs without
  changing hooks; valid initialization and rejection of a second initialization.
- Preservation of an existing host page while a host thread concurrently creates
  ordinary malloc/mmap allocations during setup.
- Ordinary malloc/mmap after setup, and two host threads repeatedly using both
  ordinary and bounded allocations. Worker stack addresses are checked too.
- A genuinely fresh `dlopen` of a separate native fixture library, symbol lookup,
  execution and close. It is deliberately not linked into the diagnostic.
- Partial unmap and exact address reuse inside the arena; rejection of fixed
  mapping/unmap outside the arena or into the low guest mapping.
- Invalid/exhausting requests without fallback, followed by successful small
  allocation; a file-backed bounded mapping sharing data with an ordinary mapping.
- Host allocation/thread coexistence and bounded allocation after all CPU tests.

The small mapping library ships only in this isolated diagnostic APK and uses no
FEX types. Packaging checks dependency closure for both native libraries. Reports
include process VmSize/RSS before setup, after host controls and after CPU tests.
Report capacity is increased to retain expanded failure detail.

## Validation and acceptance

All **64 cloud checks** pass in both translation modes: the previous 45 plus the
19 allocator checks. The 54 fault-policy controls, five ABI controls and 31,296
native x86 reference cases are retained. Android full-object compilation/linking,
APK identity/signature, ARM64 ELF, 16 KiB APK/ELF alignment and system-library
closure are checked. Patch preparation recognizes exact reviewed prefixes and
rejects an extra source edit while preserving the actual Git index.

Native Phase0.5/code6 expects **75 PASS lines**: the 56 accepted Phase0.4.1 checks
plus 19 allocator checks. Run CPU fixtures and share the report. Linux host
coexistence does not establish Bionic/ART behavior; successful fixture mappings
also do not prove arbitrary future guest memory layouts or guest concurrency.
Full bridge/memory semantics, Xbox CPU policy, bounded execution and 16 KiB hosts
remain open before broad XBE/kernel/graphics/audio HLE. No FPS gain is inferred.
