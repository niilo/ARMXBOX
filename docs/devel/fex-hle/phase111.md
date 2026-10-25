# Phase 0.11.1: low-address XBE mapping compatibility and diagnostics

The [Phase0.11 native report](phase11-android-report.md) retains all 177 inherited
PASS checks and four new admission checks, but image/stack setup fails before
any XBE engine entry. The exact reservation/protection stage and errno are absent;
the device cause is not confirmed and native XBE acceptance remains blocked.

The new fixed generated profile uses image base **0x00010000** and guarded stack
reservation **0x00020000**, replacing 0x20000000/0x20010000. The old image range
can overlap Android runtime heap reservations. This choice avoids that candidate
range without relocating an arbitrary retail image, scanning/stealing host VA,
or changing ownership of FEX's high arena. The code's four absolute operands
are derived from the admitted addresses and independently checked with GNU
`as --32`. The entry is 0x11000, import stub 0x11100, thunk 0x12000, destination
0x13000, continuation 0x1100f and completion 0x11019. Header/section extents,
service signature, 32-byte body and all execution/ABI expectations are retained.
The generated file SHA256 is
`2cf9e18078dca651d9c25c5f83b97677f929b4c389b25989ef513c768c40ba10`.

## Exact ownership without replacement

`reserveGuestExact` first attempts MAP_FIXED_NOREPLACE. EEXIST or ENOMEM fails
without a hint retry. Errors consistent with an unsupported/restricted flag
(EINVAL, EOPNOTSUPP, ENOSYS, EPERM) permit a normal anonymous address **hint**.
An older kernel returning an alternate mapping from the first request also
permits that path after releasing the alternate. A hint never uses MAP_FIXED
or asks to replace an existing mapping. Only an exactly matching returned
address is accepted; a different allocation is released and reported as
EADDRNOTAVAIL. Reservations are page-aligned, nonzero and bounded by 32-bit VA.
A real occupied-range test verifies canary preservation on both normal and
forced hint-only paths. This is compatibility, not general memory/OOM recovery.

The final report records setup stage, image/stack addresses, NOREPLACE errno,
hint use/error and protection errno. Journal records also show alternate returns,
request sizes, and the exact failed protection range. Inspection of
/proc/self/maps reports whether the former 0x20000000 range currently overlaps
a host mapping and whether it has a Dalvik tag, without printing file paths.
That observation describes the new run, not proof of the earlier run's cause.
If setup is blocked, downstream execution checks remain unmet; none are skipped
or counted as successful.

## Verification and next native gate

Nine separate syscall controls exercise exact success, unsupported/denied flag
fallback, EEXIST/ENOMEM refusal, alternate cleanup, ignored-flag behavior, hint
failure and invalid/overflowing ranges. Negative controls that accept an
alternate address or omit fallback are detected by those tests. Parser and
mapping helper pass ASan/UBSan; LeakSanitizer is disabled because the managed
ptrace environment does not support it.

Complete host suites pass **170 identical checks** in single/multiblock modes.
A third full suite uses host-only syscall injection to refuse NOREPLACE for the
XBE range and exercises real kernel hint reservations, conflict preservation,
protection, service dispatch and all seven XBE entries. It also passes all 170
checks. That injection is excluded from the Android target. No engine patch or
production app/backend path changes.

The APK is **phase0.11.1/code15**. Expect all **193 native PASS checks**, **seven
XBE entries**, **113 policy entries** and **27 native bridge entries**. These
remain unverified until the Pocket S run. If it fails, the complete report now
identifies the setup stage and errors; if the process closes, reopen and Share
recovered report before starting another run. No title FPS or general XBE/HLE
acceptance is inferred from the cloud fixtures.
