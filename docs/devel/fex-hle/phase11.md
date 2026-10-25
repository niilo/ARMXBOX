# Phase 0.11: a generated XBE with one typed kernel import

[Phase0.10.2's Pocket S report](phase102-android-report.md) passes all 177
expected checks and clears the preceding diagnostic crash for that run.
This next increment loads and executes a strictly admitted, project-owned XBE.
It is an unsigned handwritten fixture, not a retail loader or playable emulator.

## Image and service contract

The generated image has a standard little-endian XBEH header and two section
records at a fixed base of 0x20000000: a 4 KiB executable section, plus an 8 KiB
writable section whose second page is zero-filled BSS. It has a bounded empty
certificate, fixed section names, no TLS, no PE relocation/library/debug/logo
records, and one terminated kernel import. Its x86 program pushes three
arguments, calls through that import, reads the filled destination, publishes
a result and exits through a known HLT. A second owned HLT is the import stub.
No BIOS, SDK, XDK library or game bytes are embedded.

Public encoding/export facts were checked against Cxbx-Reloaded commit
`585c49a50af1255ab155099e06f24505f9c5a800`:

- [Xbe.h](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/common/xbe/Xbe.h):
  retail entry XOR 0xa8fc57ab, thunk XOR 0x5b6d40b6, section fields/flags.
- [KernelThunk.cpp](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/KernelThunk.cpp)
  and [EmuKrnlRtl.cpp](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlRtl.cpp):
  ordinal 284 (0x11c), VOID NTAPI RtlFillMemory(PVOID destination, DWORD length,
  BYTE fill). The adapter supports only the admitted 32-byte fill request.

These are format/signature facts; no external emulator implementation is copied
or linked. The independent binary checker uses GNU `as --32` to verify all 26
program bytes and checks header layout, decoding keys and ordinal. Generated
file SHA256 is
`6616d69dd62d205586328895e8821691871d8979f9ab5ba0880d8a3955be1a53`.

## Admission and ownership

The parser reads explicit LE bytes, never packed host structs or guest pointers.
It accepts only the fixed generated profile: 12 KiB file, 16 KiB virtual image,
contained metadata, fixed nonoverlapping raw/virtual sections, expected entry,
terminated ordinal, exact handwritten code and supported initial data. It
refuses other encodings/addresses, TLS, imports, section flags, extents and
payloads. File-length checks precede all fixed-offset reads; subtraction bounds
check file/image extents. Output is cleared on failure. This is intentionally
narrower than a general XBE parser. The marker and zero signature are not image
authentication; passing this parser is no evidence of retail compatibility.

Image and stack use kernel MAP_FIXED_NOREPLACE reservations and exact returned
address checks. If an older kernel ignores that flag, an alternate allocation
is released and admission fails. Existing host mappings are never replaced.
Mapping/protection failure cleans up only owned reservations. Header/text are
host read-only: x86 guest code is data to the ARM host, not host executable code.
Only the text page is admitted as guest executable in a dedicated syscall
handler. Data and two stack pages are RW; stack guard pages remain PROT_NONE.
The image remains mapped until after its stopped thread and context are destroyed.
The existing process-owned high-VA FEX arena remains alive.

The single validated thunk is patched to the owned exit stub. Host dispatch
requires precise HLT/GP metadata, the exact stub PC, a complete owned stack frame,
the known executable return PC and exact supported argument values. It performs
one bounded typed body, advances ESP by return plus 12 stdcall argument bytes,
then resumes the caller. Repeated/malformed dispatch cannot repeat a host effect.
No Linux syscall is forwarded and no arbitrary host function pointer is called.

## Execution and verification

Execution uses the accepted Xbox identity, instruction subset and instruction
checkpoints, retaining all six native ABI witnesses. One allowance is shared
across host dispatch and continuation: cold/warm runs consume five instructions
to reach the import and three more to complete (64 -> 59 -> 56). Additional
entries test zero allowance, a three-instruction stop before the CALL, and a
latched stop before image entry. Total: seven engine entries. These observations
are fixture instruction counts, not performance timings or general cancellation
bounds. No virtual IF, timing instruction, REP/helper or raw fault is admitted.

Fifteen common checks cover admission, malformed inputs, low-VA conflict
preservation, actual /proc/self/maps protections, raw/BSS initialization, the
service, guest continuation, warm reuse, invalid/repeated dispatch and execution
limits. A native-only summary requires all six ABI witnesses across seven entries.
Expect **170 complete host checks** in each translation mode and **193 native
checks** on Pocket S, retaining all earlier checks and 113 policy/27 bridge entries.

The separate image test checks every truncated file length, metadata/section/
encoding/import/code mutations, actual overlap and writable-code cases, plus
4,000 seeded mutations. ASan/UBSan run with leak detection disabled because the
managed executor's ptrace environment does not support LeakSanitizer; no leak
verification is inferred. Executable negative controls remove import admission,
break stdcall cleanup, or reset the allowance across dispatch; the complete
suite must detect each before correct sources are restored.

The APK is **phase0.11/code14**. Native mapping/protection/ABI evidence is pending.
No retail, renderer/audio, game FPS, sustained thermal or production acceptance is
inferred. Next after native acceptance: controlled graphics/PCM and expanded
application contracts, followed by matched title checkpoints and performance.
