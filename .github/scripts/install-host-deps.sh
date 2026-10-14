#!/usr/bin/env bash
# Install pinned SDL3 and Vulkan headers when runner/toolchain packages lag.
set -euo pipefail

prefix="${1:?Usage: install-host-deps.sh INSTALL_PREFIX [CMAKE]}"
cmake_cmd="${2:-cmake}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Read the same versions and checksums as the host build's dependency pins.
read -r sdl_version sdl_hash < <(python3 - "$repo/subprojects/sdl3.wrap" <<'PY'
import configparser
import sys
wrap = configparser.ConfigParser(interpolation=None)
wrap.read(sys.argv[1])
section = wrap['wrap-file']
print(section['directory'].removeprefix('SDL3-'), section['source_hash'])
PY
)
vulkan_pin="$repo/ubuntu-win64-cross/vulkan-headers.mk"
vulkan_version=$(sed -n 's/.*_VERSION *:= *//p' "$vulkan_pin")
vulkan_hash=$(sed -n 's/.*_CHECKSUM *:= *//p' "$vulkan_pin")

download() {
    curl --fail --location --retry 3 "$1" -o "$2"
    python3 - "$2" "$3" <<'PY'
import hashlib
import pathlib
import sys
digest = hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest()
if digest != sys.argv[2]:
    sys.exit('Dependency archive checksum mismatch')
PY
    tar -xzf "$2" -C "$work"
}

sdl_url="https://github.com/libsdl-org/SDL/releases/download"
vulkan_url="https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags"
download "$sdl_url/release-${sdl_version}/SDL3-${sdl_version}.tar.gz" \
    "$work/sdl.tar.gz" "$sdl_hash"
download "$vulkan_url/${vulkan_version}.tar.gz" \
    "$work/vulkan.tar.gz" "$vulkan_hash"

shared=ON
static=OFF
if [[ "$cmake_cmd" == *mingw* ]] || [ "${XEMU_SDL_STATIC:-OFF}" = ON ]; then
    shared=OFF
    static=ON
fi
"$cmake_cmd" -S "$work/SDL3-${sdl_version}" -B "$work/sdl-build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DSDL_SHARED="$shared" -DSDL_STATIC="$static" \
    -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
"$cmake_cmd" --build "$work/sdl-build" --parallel 2
"$cmake_cmd" -S "$work/Vulkan-Headers-${vulkan_version}" \
    -B "$work/vulkan-build" \
    -DCMAKE_INSTALL_PREFIX="$prefix"
needs_sudo=false
if [ ! -w "$prefix" ] && [ "$(id -u)" -ne 0 ]; then
    needs_sudo=true
fi
install_files() {
    if "$needs_sudo"; then
        sudo "$@"
    else
        "$@"
    fi
}
install_files "$cmake_cmd" --install "$work/sdl-build"
install_files "$cmake_cmd" --install "$work/vulkan-build"
if [ "$(uname -s)" = Linux ] && [ "$prefix" = /usr/local ]; then
    install_files ldconfig
fi
