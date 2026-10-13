# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

X1 BOX (`com.izzy2lost.x1box`) is an original Xbox emulator for Android, forked from [xemu](https://github.com/xemu-project/xemu), itself a fork of QEMU. The repo is a full QEMU source tree; Xbox-specific emulation lives in `hw/xbox/`, and the Android packaging/UI lives in `android/`. Android is the primary target (arm64-v8a, minSdk 29, Vulkan required), but the host build (`./build.sh`) still produces the desktop xemu binary.

## Build Commands

The supported dev environment is Docker. `docker/dev.sh <command>` builds the `x1box-dev` image from `docker/Dockerfile` and runs the command in `/work` (this repo, bind-mounted). Nothing else needs to be installed on the host. Commands below can be run through it, e.g. `docker/dev.sh ./build.sh --debug` or `docker/dev.sh bash -c 'cd android && ./gradlew assembleDebug'`. Gradle, Cargo and ccache state persist in named volumes. `docker/dev.sh --gpu <command>` also passes the host's `/dev/dri` render node into the container (Linux only) for Vulkan: `docker/dev.sh --gpu vulkaninfo --summary` lists the Mesa RADV device. The image pins Ubuntu 22.04, SDL3 3.4.10 and Vulkan-Headers 1.4.309.0, so the host build does not use the meson SDL3 fallback, which does not resolve.

### Android (primary target)

```bash
cd android
./gradlew assembleDebug          # debug APK
./gradlew assembleProfile        # profileable release APK (for Android Studio profiling)
```

Toolchain versions and host-install notes are in `android/README.md`; the Docker image pins them. **Rust is required even if you disable the ISO->XISO converter** (`-DXEMU_ENABLE_XISO_CONVERTER=OFF`) because the DSP JIT backend is built from source.

Tool paths outside `PATH` go in `android/local.properties` (git-ignored): `xemu.cargoExecutable`, `xemu.mesonExecutable`, `xemu.cargoHome`, `xemu.rustupHome`, `xemu.hostRustLinker`.

Release AAB: run `./build-aab.sh` from the repo root. It signs with `android/key.properties` (creates it with placeholders on first run; fill in real keystore values and rerun). Output: `android/app/build/outputs/bundle/release/app-release.aab`.

### Host (Linux/macOS/Windows)

```bash
./build.sh              # configures into build/ and produces build/qemu-system-i386 (the xemu executable)
./build.sh --debug      # --enable-debug + log tracing
```

### Android native regression test

The ARM64 coroutine backend has a native test excluded from APKs. After a Gradle configure, use its CMake build ID:

```bash
cmake --build app/.cxx/RelWithDebInfo/<build-id>/arm64-v8a --target test-coroutine-android
adb push app/build/intermediates/cxx/RelWithDebInfo/<build-id>/obj/arm64-v8a/test-coroutine-android /data/local/tmp/
adb shell /data/local/tmp/test-coroutine-android
```

It checks 1,000 coroutine transfers across threads, nested entry, stack/FPU state, and termination, with per-thread pointer-authentication keys as on real devices.

## Tests

Host meson unit tests run against the `build/` directory created by `./build.sh`. The CI sequence (`.github/workflows/build-linux.yml`) compiles the test targets first:

```bash
meson compile -C build test-xbox                 # build the Xbox test binaries
meson test -C build --suite xbox --no-rebuild    # Xbox-specific tests (tests/xbox/)
meson test -C build xbox-mcpx-dsp --no-rebuild   # a single test
```

`xbox-mcpx-dsp` is the only test in the suite and passes in the Docker image. `dsp_init()` reads `g_config` (the UI settings object), so the test defines its own zeroed `g_config`; new Xbox tests that reach `hw/xbox/mcpx/apu/dsp/` need the same.

`tests/xbox/swizzle/` is a standalone Makefile comparing the swizzle implementations against each other.

## Architecture

**QEMU core / guest CPU.** Emulates i386 (`--target-list=i386-softmmu`) with TCG (`accel/tcg/`, `target/i386/`). The Xbox machine is `hw/xbox/xbox.c`. This fork carries local performance work not in upstream xemu: TCG peephole/optimizer changes, TB cache-hint pre-warming, x87 arithmetic on the host FPU with bit-exact precision, and lockless/TLB fast paths — several gated by `XEMU_OPT_*` CMake options (off by default). Asserts must stay enabled; QEMU refuses `NDEBUG` builds.

**GPU (NV2A).** `hw/xbox/nv2a/` implements the Xbox GPU (pfifo, pmc, ptimer, pvideo, ...). The PGRAPH rasterizer has two backends under `hw/xbox/nv2a/pgraph/`: `gl/` (OpenGL) and `vk/` (Vulkan). The Vulkan backend is derived from the hakuX fork — generation-counter surface caches, descriptor set caching, async shader compilation (`compile_worker.c`), and a threaded render pipeline (`render_thread.c`, `submit_worker.c`). It contains Android/GPU-driver-specific workarounds (e.g. Qualcomm/Adreno, Mali) — read the surrounding comments before changing GPU code. nv2a performance telemetry is compiled in by default (`XEMU_ENABLE_PERF_LOG`, hakuX-*/xemu-* logcat lines).

**Chihiro arcade (Sega).** `hw/xbox/chihiro/` adds the Chihiro baseboard: media board (8051, AN2131), JVS input, card readers, drive boards (V257 wheel, Sega 838), netboard, and the FATX builder for netboot images. Chihiro mode is enabled per-app settings (forces 128 MiB RAM), uses 512 KiB BIOS images (MCPX overlay is skipped for BIOS >256 KiB), regenerates EEPROM with the debug key on first boot, and unpacks a netboot `.bin` game to `Android/data/<pkg>/files/x1box/chihiro/<name>/` on first launch. Cabinet controls map to pad 1; `android/README.md` documents the full flow and control tables.

**Android app.** `android/app/src/main/java/com/izzy2lost/x1box/` — launcher UI (game library, setup wizard, settings, per-game settings, on-screen controller, GPU driver picker, Chihiro game folder import, XISO converter bindings). The SDL Java glue in `android/app/src/main/java/org/libsdl/app/` is a vendored copy of the SDL release sources with xemu edits each marked `// xemu:` — **when bumping `SDL3_VERSION` in `android/app/src/main/cpp/CMakeLists.txt`, re-sync the Java glue from the new release or the JNI signatures will drift and crash on startup.**

The native build (`android/app/src/main/cpp/CMakeLists.txt`) globs `*.c/.cc/.cpp` from the repo root to compile the QEMU core — new source files are picked up automatically; files replaced on Android are the `*_stub.c` siblings there. It also builds glib for Android via meson, SDL3 via FetchContent, `nv2a_vsh_cpu` and `dsp56300` (pinned to match upstream xemu), and the Rust static libraries.

**Rust.** `rust/` is upstream QEMU Rust support (bindings, qemu-macros); `android/rust/xiso_converter/` (with `android/third_party/xdvdfs-core`) is the ISO->XISO converter; the DSP JIT backend is built by cargo. Upstream-versioned dependencies are meson wraps in `subprojects/`.

## Conventions

- QEMU coding style (`docs/devel/style.rst`): 4-space indentation, no tabs, no trailing whitespace; check patches with `scripts/checkpatch.pl`; `.clang-format` and `.editorconfig` are authoritative. Developer docs live under `docs/devel/`.
- Host-side builds are meson-based: new targets/flags need `meson.build` edits; Android-side equivalents live in `android/app/src/main/cpp/CMakeLists.txt`.
- Firmware/BIOS, Xbox HDD images, and game content are user-supplied; nothing copyrighted ships in the repo.