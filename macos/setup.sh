#!/usr/bin/env bash
# Install the native macOS Android toolchain; Docker remains independent.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

brew install openjdk@21 meson ninja rustup
brew install --cask android-commandlinetools
source "$repo/macos/env.sh"

rustup toolchain install stable --profile minimal
rustup target add aarch64-linux-android --toolchain stable
if ! env -u RUSTUP_TOOLCHAIN rustup show active-toolchain >/dev/null 2>&1; then
    rustup default stable
fi

# pipefail would otherwise treat yes's expected SIGPIPE as a license failure.
(
    set +o pipefail
    yes | sdkmanager --sdk_root="$ANDROID_HOME" --licenses >/dev/null
)
sdkmanager --sdk_root="$ANDROID_HOME" \
    "platform-tools" "platforms;android-36" "build-tools;36.1.0" \
    "ndk;30.0.15729638" "cmake;3.22.1"

# Add missing settings for Android Studio, preserving existing local overrides.
python3 - "$repo/android/local.properties" "$ANDROID_HOME" \
    "$(command -v cargo)" "$(command -v meson)" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text() if path.exists() else ""
keys = {line.split("=", 1)[0].strip()
        for line in text.splitlines() if "=" in line}
settings = dict(zip(
    ("sdk.dir", "xemu.cargoExecutable", "xemu.mesonExecutable"), sys.argv[2:]
))
additions = "".join(
    "{}={}\n".format(key, value.replace("\\", "\\\\"))
    for key, value in settings.items() if key not in keys
)
if additions:
    if text and not text.endswith("\n"):
        text += "\n"
    path.write_text(text + additions)
PY

exec "$repo/macos/dev.sh" --check
