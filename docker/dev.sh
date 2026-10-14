#!/usr/bin/env bash
# Runs a command in the X1 BOX development container. The host needs only Docker.
#   docker/dev.sh                      interactive shell
#   docker/dev.sh ./build.sh --debug   host build, output lands in build/
#   docker/dev.sh android/gradlew -p android assembleDebug
#   docker/dev.sh --gpu vulkaninfo --summary   expose the host GPU (Linux, /dev/dri)
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
image="${XEMU_DEV_IMAGE:-x1box-dev}"
android_properties="$repo/docker/android-local.properties"
build_flags=(build)
run_flags=(run --rm)
host_os="$(uname -s)"

# Linux SDK/NDK host tools are x86-64, even when the APK targets ARM64.
# Preserve the Linux default; use an amd64 image on Apple Silicon Macs.
platform="${XEMU_DEV_PLATFORM:-${DOCKER_DEFAULT_PLATFORM:-}}"
if [ -z "$platform" ] && [ "$host_os" = Darwin ] && \
        [ "$(uname -m)" = arm64 ]; then
    platform=linux/amd64
fi
if [ -n "$platform" ]; then
    build_flags+=(--platform "$platform")
    run_flags+=(--platform "$platform")
fi
if [ "$host_os" = Darwin ]; then
    # Gradle's Linux file watcher can fail under macOS CPU translation.
    run_flags+=(--env GRADLE_OPTS=-Dorg.gradle.vfs.watch=false)
fi

if [ "${1:-}" = "--gpu" ]; then
    shift
    if [ ! -e /dev/dri/renderD128 ]; then
        echo "dev.sh: --gpu needs /dev/dri/renderD128 on the host" >&2
        exit 1
    fi
    # Pass the host render group so the container user can open the render node.
    render_gid="$(stat -c '%g' /dev/dri/renderD128)"
    run_flags+=(--device /dev/dri --group-add "$render_gid")
fi

uid="$(id -u)"
gid="$(id -g)"
# Rootless Docker maps container UID N to a host subuid, so bind-mounted files
# would not belong to the host user. There, container root is the host user.
if docker info --format '{{json .SecurityOptions}}' | grep -q rootless; then
    uid=0
    gid=0
fi

docker "${build_flags[@]}" -t "$image" \
    --build-arg UID="$uid" --build-arg GID="$gid" \
    "$repo/docker"

if [ -t 0 ] && [ -t 1 ]; then
    run_flags+=(-it)
fi

# Overlay the container's SDK settings without rewriting the host's file.
# A file must exist at the bind-mount destination in the shared checkout.
if [ ! -e "$repo/android/local.properties" ]; then
    if [ -L "$repo/android/local.properties" ]; then
        echo "dev.sh: android/local.properties is a broken symlink" >&2
        exit 1
    fi
    touch "$repo/android/local.properties"
fi

# Named volumes keep the Gradle, Cargo and ccache state between runs.
exec docker "${run_flags[@]}" \
    -v "$repo:/work" \
    -v "$android_properties:/work/android/local.properties:ro" \
    -v x1box-gradle:/home/dev/.gradle \
    -v x1box-cargo:/opt/cargo/registry \
    -v x1box-ccache:/home/dev/.cache/ccache \
    "$image" "${@:-bash}"
