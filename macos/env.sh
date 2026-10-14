#!/usr/bin/env bash
# Shared environment for the native Android development scripts.
if [ "$(uname -s)" != Darwin ]; then
    echo "macos: these scripts require macOS; use docker/dev.sh on Linux" >&2
    exit 1
fi
if ! command -v brew >/dev/null; then
    echo "macos: install Homebrew from https://brew.sh first" >&2
    exit 1
fi

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
brew_prefix="$(brew --prefix)"
export PATH="$brew_prefix/opt/rustup/bin:$brew_prefix/bin:$PATH"
jdk_home="$brew_prefix/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
if [ -d "$jdk_home" ]; then
    export JAVA_HOME="$jdk_home"
else
    JAVA_HOME="$(/usr/libexec/java_home -v 21 2>/dev/null || true)"
    export JAVA_HOME
fi
if [ -n "$JAVA_HOME" ]; then
    export PATH="$JAVA_HOME/bin:$PATH"
fi
export RUSTUP_TOOLCHAIN=stable

# Match Gradle's SDK selection when Android Studio has written local.properties.
sdk_from_properties="$(python3 - "$repo/android/local.properties" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
if path.is_file():
    for line in path.read_text().splitlines():
        key, separator, value = line.partition("=")
        if separator and key.strip() == "sdk.dir":
            value = value.strip().replace("\\:", ":").replace("\\ ", " ")
            print(value.replace("\\\\", "\\"))
            break
PY
)"
sdk_default="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
export ANDROID_HOME="${sdk_from_properties:-$sdk_default}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmake/3.22.1/bin:$PATH"
