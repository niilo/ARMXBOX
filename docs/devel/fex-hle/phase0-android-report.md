# Phase 0.1 Android execution report

User report received 24 October 2026 (Europe/Helsinki). The report itself does
not timestamp the run or identify the hardware model or Android version.

All ten fixtures passed in an Android ARM64 native process with 4 KiB host pages.
This clears the basic direct-Android-execution gate and reproduces the cloud
fixture results on Android. It validates these constructed cases only, including
the exit-based bridge, callback state preservation, direct-access aliases and
explicit invalidation. It does not validate arbitrary Xbox code or title speed.

The final line's generic “device” gate text is hard-coded in phase0.1; it is not
an additional failed check. Dedicated ABI/cache/fault and full memory-contract
tests remain open. The report contains no timings, FPS or reference comparison.

Source report SHA-256: `91b484dacdaf6df32d30ea4ba4c00e11f3e546c99093e1017dcdde1cd603b2c9`.

```text
ARMXBOX FEX Phase 0; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: Android ARM64 native process
Host page size: 4096
PASS IA32 arithmetic/carry and HLT exit
PASS SSE2 packed integer
PASS MMX and EMMS
PASS x87 extended arithmetic/store
PASS FS cached base load
PASS run/resume retains GPR state
PASS callback restores FPU/SIMD/control/flags state
PASS exit-based typed stdcall/reverse callback/stack cleanup
PASS generated direct accesses share backing aliases
PASS explicit code invalidation observes modified instruction
Result: CPU FIXTURES PASS; full bridge/fault/16K/device/performance gates remain open
```
