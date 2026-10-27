# Phase 0 cloud fixture report

Recorded 2026-10-22 UTC. Linux x86-64 development container, Clang 21,
4 KiB host pages, pinned FEX and its VIXL simulator. This is not an Android
execution or performance result. The matching Android ARM64 core/link and APK
packaging checks passed separately with NDK 30.0.15729638/API 29.

```text
ARMXBOX FEX Phase 0; pin 4e2b509a42b7762a48c407d6a59bb897dca1ded8
Execution: host VIXL simulator; NOT Android device proof
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
