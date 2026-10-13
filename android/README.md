# xemu Android

This directory contains the Android frontend and native build for xemu. The
Android entry point in `app/src/main/cpp/xemu_android.cpp` integrates the xemu
core with SDL3 and the Android Vulkan/OpenGL rendering paths.

## Toolchain
The supported setup is the Docker image in `../docker/`, which pins the SDK,
NDK, CMake, JDK, Rust and the other tools. From the repository root:

```
docker/dev.sh bash -c 'cd android && ./gradlew assembleDebug'
```

Rust is needed even with `-DXEMU_ENABLE_XISO_CONVERTER=OFF`, because the DSP
JIT backend is built from source. That flag only disables the ISO->XISO converter.

Building on the host instead (Windows, or Linux without Docker) needs the same
versions: Android SDK 36, Build Tools 36.1.0, NDK 30.0.15729638, CMake 3.22.1,
Meson, Ninja, JDK 21, and Rust. On Windows, use the
`stable-x86_64-pc-windows-gnu` toolchain (to avoid MSVC `link.exe`):

```
rustup toolchain install stable-x86_64-pc-windows-gnu
rustup target add aarch64-linux-android --toolchain stable-x86_64-pc-windows-gnu
```

## Build
From this directory (or through `docker/dev.sh` as above):

```
./gradlew assembleDebug
```

The Android ARM64 coroutine backend has an optional native regression test.
After configuring a build with Gradle, use its CMake build ID below:

```
cmake --build app/.cxx/RelWithDebInfo/<build-id>/arm64-v8a --target test-coroutine-android
adb push app/build/intermediates/cxx/RelWithDebInfo/<build-id>/obj/arm64-v8a/test-coroutine-android /data/local/tmp/
adb shell /data/local/tmp/test-coroutine-android
```

This checks 1,000 coroutine transfers between threads, nested entry, stack and
floating-point state, and termination. On hardware with pointer authentication,
the threads use separate IA keys as Android app threads do. The test is excluded
from normal APK builds.

For a release-optimized APK that Android Studio can profile without making the
app debuggable:

```
./gradlew assembleProfile
```

The resulting APK is written to
`app/build/outputs/apk/profile/app-profile.apk`. Open it with Android Studio's
**Profile or Debug APK** action. A connected arm64 Android device is required
to record CPU, memory, power, or system traces.

Host tools installed outside the normal `PATH` can be supplied in the ignored
`local.properties` file:

```
xemu.cargoExecutable=/absolute/path/to/cargo
xemu.mesonExecutable=/absolute/path/to/meson
xemu.cargoHome=/absolute/path/to/cargo-home
xemu.rustupHome=/absolute/path/to/rustup-home
xemu.hostRustLinker=/absolute/path/to/a/host-linker
```

## SDL3
SDL3 is fetched via CMake (`SDL3_VERSION` in `app/src/main/cpp/CMakeLists.txt`,
currently 3.4.14). To use a local checkout:

```
./gradlew assembleDebug -Pandroid.experimental.cmake.arguments=-DSDL3_LOCAL_DIR=/path/to/SDL
```

## Sega Chihiro (arcade)

The Chihiro baseboard support ported from
[Tovarichtch/xemu](https://github.com/Tovarichtch/xemu) is built into the
Android target. It is off by default and is controlled by one setting rather
than by the RAM size, so plain 128 MiB debug/homebrew configurations behave
exactly as before.

To run a Chihiro game:

1. **Settings → Sega Chihiro (Arcade) → Enable Chihiro mode.** This forces
   system memory to 128 MiB, which the baseboard requires.
2. **Install the media board ROM** with the button in the same section. It
   accepts `fpr21042_m29w160et.bin` (the Cxbx dump, which the LLE boot path
   was written against) or `fpr-23887_29lv160te.ic4`. The file is copied next
   to the BIOS, where `hw/xbox/chihiro.c` looks for it.
3. **Use a 512 KiB Chihiro BIOS** as the flash/BIOS file. The MCPX overlay is
   skipped for BIOS images larger than 256 KiB, because a Chihiro BIOS carries
   its own boot code with a different RC4 key.
4. **Pick the game.** Either select a `.bin` netboot image in the game library
   -- on first launch its FATX contents are unpacked to
   `Android/data/<pkg>/files/x1box/chihiro/<name>/` and reused thereafter --
   or point **Use Chihiro game folder** at an already-extracted game.

   The emulator consumes a *directory*, not an image: it scans it to build the
   mbfs FATX in memory and reads `boot.id` from it. SEGABOOT rejects the game
   with "This game is not acceptable by main board" if `boot.id` never loads.

Cabinet controls are on the pad bound to port 1, which is also what the
on-screen controller drives:

**Cabinet controls** picks which scheme is live. The JVS analog channels mean
different things per cabinet -- channel 0 is the gun's X axis on a light gun
game and the steering wheel on a driving one -- so only one can be reported at
a time.

Light gun (House of the Dead III, Ghost Squad, Virtua Cop 3):

| On-screen | Pad | Function |
|---|---|---|
| touch the screen | - | aim and fire |
| **B** | B | reload |
| **LT** | Left trigger | pedal (Virtua Cop 3's ES MODE) |

Driving (Crazy Taxi High Roller, OutRun 2, Wangan Midnight):

| On-screen | Pad | Function |
|---|---|---|
| left stick / d-pad | Left stick | steering |
| **RT** | Right trigger | accelerator |
| **LT** | Left trigger | brake |
| **A** / **B** | A / B | gear lever, or shift up/down |
| **X** | X | view change |

Shared by both:

| On-screen | Pad | Function |
|---|---|---|
| **◀** | Back | insert coin |
| **▶** | Start | start |
| **WH** | White | service |
| **BK** | Black | test menu |

The overlay consumes every touch so that it can drive the pads, so SDL never
sees a pointer of its own; touches that miss a control are forwarded to the
light gun through `nativeSetLightGun`. Reload needs its own button because the
cabinet reloads by shooting off screen, and at the default Stretch aspect
there is no off-screen area left to shoot into.

The EEPROM is regenerated with the debug key on first Chihiro boot — the
retail key will not boot the arcade kernel.

Input arrives over JVS rather than USB, so the usual gamepad ports are not
created in Chihiro mode. The light gun reads the pointer; SDL synthesises
mouse events from touch, so tapping the screen aims and fires.
