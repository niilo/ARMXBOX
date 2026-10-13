#!/usr/bin/env bash
# Runs a command in the X1 BOX development container. The host needs only Docker.
#   docker/dev.sh                      interactive shell
#   docker/dev.sh ./build.sh --debug   host build, output lands in build/
#   docker/dev.sh android/gradlew -p android assembleDebug
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
image="${XEMU_DEV_IMAGE:-x1box-dev}"

uid="$(id -u)"
gid="$(id -g)"
# Rootless Docker maps container UID N to a host subuid, so bind-mounted files
# would not belong to the host user. There, container root is the host user.
if docker info --format '{{json .SecurityOptions}}' | grep -q rootless; then
    uid=0
    gid=0
fi

docker build -t "$image" \
    --build-arg UID="$uid" --build-arg GID="$gid" \
    "$repo/docker"

tty_flags=()
if [ -t 0 ] && [ -t 1 ]; then
    tty_flags=(-it)
fi

# Named volumes keep the Gradle, Cargo and ccache state between runs.
exec docker run --rm "${tty_flags[@]}" \
    -v "$repo:/work" \
    -v x1box-gradle:/home/dev/.gradle \
    -v x1box-cargo:/opt/cargo/registry \
    -v x1box-ccache:/home/dev/.cache/ccache \
    "$image" "${@:-bash}"
