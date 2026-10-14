#!/usr/bin/env bash
# Native macOS Android builds: macos/dev.sh [Gradle tasks/options], or --check.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

missing=0
for tool in java meson ninja cargo rustup; do
    if ! command -v "$tool" >/dev/null; then
        echo "macos: missing $tool" >&2
        missing=1
    fi
done
if [ -z "$JAVA_HOME" ] || [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "macos: JDK 21 is missing" >&2
    missing=1
fi
for component in platforms/android-36/android.jar build-tools/36.1.0/aapt2 \
    ndk/30.0.15729638/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang \
    cmake/3.22.1/bin/cmake; do
    if [ ! -f "$ANDROID_HOME/$component" ]; then
        echo "macos: missing SDK component $ANDROID_HOME/$component" >&2
        missing=1
    fi
done
if command -v rustup >/dev/null && \
        ! rustup target list --installed --toolchain stable 2>/dev/null | \
            grep -qx aarch64-linux-android; then
    echo "macos: Rust stable needs the aarch64-linux-android target" >&2
    missing=1
fi
if [ "$missing" != 0 ]; then
    echo "macos: run macos/setup.sh to install the toolchain" >&2
    exit 1
fi
if [ "${1:-}" = --check ]; then
    echo "macos: native Android toolchain is ready (SDK: $ANDROID_HOME)"
    exit 0
fi

exec "$repo/android/gradlew" -p "$repo/android" \
    --project-cache-dir "$repo/android/.gradle-macos" "${@:-assembleDebug}"
