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
cmake --build /tmp/armxbox-fex-host --target armxbox_fex_selftest -j 2
timeout 60s /tmp/armxbox-fex-host/armxbox_fex_selftest
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
then builds and uploads `armxbox-fex-phase0.1-debug.apk`. It uses read-only GitHub
permissions and no production signing secrets. Its debug key is generated for
each fresh output directory; uninstall an older probe if signatures differ.

Install alongside ARMXBOX (`org.armxbox.fexprobe`), open **ARMXBOX FEX Probe**, tap
**Run CPU fixtures**, then **Share report**. No BIOS, game, rootfs, Wine or root
permission is required. The application requests no storage/network permission.
Force-stop and relaunch to repeat; allocator/configuration ownership is currently
one-shot and process-wide. Capture `adb logcat -s ARMXBOX-FEX` if it crashes before
returning a report. A hang/crash is a failed probe, never an implied success.

A 16 KiB host reports unsupported before FEX initialization. ELF/APK 16 KiB
alignment is packaging compatibility only; it does not prove runtime page support.
The diagnostic uses only constructed, bounded fixtures. It installs no general
fault handler and cannot safely run arbitrary guest code.

## Verified increment and remaining work

See [Phase 0 findings](../../docs/devel/fex-hle/phase0-findings.md) for the engine
pin, licenses, source patch, embedding obligations and acceptance matrix.

All ten checks passed both in the host simulator and in the
[user Android report](../../docs/devel/fex-hle/phase0-android-report.md). They cover
integer carry, SSE2, MMX, x87, FS base, resume, callback
FPU/SIMD/flags preservation, typed stdcall/reverse callback/stack cleanup,
generated accesses through aliases, and explicit invalidation. The bridge is an
exit-based prototype using HLT stubs and nested `ExecuteThread`, not an upstream
Linux thunk or a completed production callback ABI. SSE2 here exercises engine
state; the final Xbox CPUID/instruction policy remains to be implemented.

Basic direct Android ARM64 execution on 4 KiB pages is now demonstrated.
The next gates are controlled fault/MMIO exits,
x18/AAPCS64/cache-publication witnesses and the remaining bridge/memory contracts.
Broad XBE/kernel, graphics and audio HLE must wait for those gates. Keep original
backend comparisons separate from CPU fixture results.
