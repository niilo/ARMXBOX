# ARMXBOX FEX Phase 0 probe

An isolated, game-free CPU/embedding diagnostic, not a playable emulator. The
production app and full-system backend do not depend on this directory.

The goal remains faster title execution than the reference backend. This
increment establishes buildability and CPU contracts; it measures no title FPS.
Do not interpret VIXL simulator timing as Android performance.

## Build and run

Tools: Python 3.9+, Git, Ninja, CMake 3.22+, Clang, Android SDK 36,
build-tools 36.1.0, NDK 30.0.15729638, JDK 21+. Cloud development used Clang 21.
The x86-64 Linux simulator build additionally requires an AVX-capable host.

```sh
python3 experiments/fex-runtime/scripts/prepare.py /tmp/armxbox-FEX
cmake -S experiments/fex-runtime -B /tmp/armxbox-fex-host -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DARMXBOX_HOST_SIMULATOR=ON \
  -DFEX_SOURCE_DIR=/tmp/armxbox-FEX
cmake --build /tmp/armxbox-fex-host --target armxbox_fex_selftest armxbox_abi_witness_test armxbox_division_native_test armxbox_fault_policy_test -j 2
timeout 30s /tmp/armxbox-fex-host/armxbox_abi_witness_test /tmp/armxbox-fex-host/abi_witness.bin
timeout 30s /tmp/armxbox-fex-host/armxbox_fault_policy_test
timeout 30s /tmp/armxbox-fex-host/armxbox_division_native_test
ARMXBOX_PROBE_SINGLE_BLOCK=1 timeout 120s /tmp/armxbox-fex-host/armxbox_fex_selftest
timeout 120s /tmp/armxbox-fex-host/armxbox_fex_selftest
```

The fixture suite runs IA-32 through FEX-generated ARM64 under upstream VIXL;
it is not an interpreter substituting for the engine and is not device proof.

```sh
export ANDROID_HOME=/path/to/android-sdk
export FEX_SOURCE_DIR=/tmp/armxbox-FEX
cmake -S experiments/fex-runtime -B /tmp/armxbox-fex-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/30.0.15729638/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release -DFEX_SOURCE_DIR="$FEX_SOURCE_DIR"
cmake --build /tmp/armxbox-fex-android \
  --target armxbox_fex_link_probe armxbox_fex_diagnostic -j 2
bash experiments/fex-runtime/scripts/package-apk.sh \
  /tmp/armxbox-fex-android /tmp/armxbox-fex-apk
```

The `FEX Phase 0 diagnostic` GitHub Actions workflow runs the host fixtures first,
then builds and uploads `armxbox-fex-phase0.5-debug.apk`. It uses read-only GitHub
permissions and no production signing secrets. Its debug key is generated for
each fresh output directory; uninstall an older probe if signatures differ.

Install alongside ARMXBOX (`org.armxbox.fexprobe`), open **ARMXBOX FEX Probe**, tap
**Run CPU fixtures**, then **Share report**. No BIOS, game, rootfs, Wine or root
permission is required. The application requests no storage/network permission.
Force-stop and relaunch to repeat; allocator/configuration ownership is currently
one-shot and process-wide. Capture `adb logcat -s ARMXBOX-FEX` if it crashes before
returning a report. A hang/crash is a failed probe, never an implied success.

The current build is phase0.5; see [bounded allocation and coexistence](../../docs/devel/fex-hle/phase05.md).
Phase0.4.1 passes all 56 checks on Pocket S. This cumulative build adds 19 host
allocator checks for 75 expected native PASS lines. Its bounded arena reserves
2 GiB of virtual capacity; this is not a physical-memory commitment. Native
Android coexistence validation is pending.

A 16 KiB host reports unsupported before FEX initialization. ELF/APK 16 KiB
alignment is packaging compatibility only; it does not prove runtime page support.
The diagnostic uses only constructed, bounded fixtures. Phase0.4 temporarily
handles only owned, typed word-fault cases and restores the previous registration.
It cannot safely run arbitrary guest code or provide general fault recovery.

## Verified increment and remaining work

See [Phase 0 findings](../../docs/devel/fex-hle/phase0-findings.md) for the engine
pin, licenses, source patch, embedding obligations and acceptance matrix.

All ten checks passed both in the host simulator and in the
[user Android report](../../docs/devel/fex-hle/phase0-android-report.md). They cover
the original integer carry, SSE2, MMX, x87, FS base, resume, callback
FPU/SIMD/flags preservation, typed stdcall/reverse callback/stack cleanup,
generated accesses through aliases, and explicit invalidation. The bridge is an
exit-based prototype using HLT stubs and nested `ExecuteThread`, not an upstream
Linux thunk or a completed production callback ABI. SSE2 here exercises engine
state; the final Xbox CPUID/instruction policy remains to be implemented.

Basic direct Android ARM64 execution on 4 KiB pages is demonstrated. Phase0.4.1
passes all 56 CPU/division/metadata/raw-fault/word-MMIO/ABI checks on Pocket S.
Phase0.5 adds bounded allocation and coexistence; its new behavior needs the device
run. Full bridge/memory, internal signal/TLS/cache, CPU policy, bounded execution
and 16 KiB contracts remain open before broad XBE/kernel, graphics or audio HLE.
Keep title performance comparisons separate from diagnostic fixture results.
