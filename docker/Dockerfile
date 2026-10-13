# Development environment for X1 BOX. The host needs only Docker: host build
# (./build.sh) and Android build (android/gradlew) both run inside this image.
# The base matches the Ubuntu 22.04 CI runners.
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
# Matches the host user so files written into the bind-mounted repo keep their
# owner. docker/dev.sh passes these; rootless Docker passes 0 (see dev.sh).
ARG UID=1000
ARG GID=1000
ARG ANDROID_CMDLINE_TOOLS_URL=https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip

# LLVM 21 from apt.llvm.org, as the CI workflow does.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl gnupg unzip xz-utils zstd file \
    && install -d /etc/apt/keyrings \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
        | gpg --dearmor -o /etc/apt/keyrings/llvm.gpg \
    && echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-21 main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends \
        clang-21 lld-21 llvm-21-dev ccache \
        build-essential cmake git pkg-config flex bison \
        python3 python3-pip python3-venv python3-tomli python3-yaml \
        libfuse2 libusb-1.0-0-dev libgtk-3-dev libepoxy-dev libpixman-1-dev \
        libsdl2-dev libsamplerate0-dev zlib1g-dev libssl-dev libpcap-dev \
        libslirp-dev libvulkan-dev libcurl4-gnutls-dev libglib2.0-dev \
        libgnutls28-dev openjdk-21-jdk-headless \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols \
    && rm -rf /var/lib/apt/lists/*

# SDL3 is not packaged for jammy. Build the version pinned in subprojects/sdl3.wrap
# and install it under /usr/local so pkg-config finds it before meson's fallback,
# which does not resolve (it looks for sdl3_dep; SDL3 exports SDL3_shared_dep).
ARG SDL3_VERSION=3.4.10
RUN curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-${SDL3_VERSION}.tar.gz" \
        | tar -xz -C /tmp \
    && cmake -S /tmp/SDL3-${SDL3_VERSION} -B /tmp/sdl3-build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    && cmake --build /tmp/sdl3-build -j"$(nproc)" \
    && cmake --install /tmp/sdl3-build \
    && rm -rf /tmp/SDL3-${SDL3_VERSION} /tmp/sdl3-build

# jammy ships Vulkan headers 1.2.204, too old for the nv2a Vulkan backend
# (VK_EXT_extended_dynamic_state3). Install the version pinned by
# ubuntu-win64-cross/vulkan-headers.mk into /usr/local, which the compiler
# searches before /usr/include. libvulkan from apt stays the loader.
ARG VULKAN_HEADERS_VERSION=vulkan-sdk-1.4.309.0
RUN curl -fsSL "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/${VULKAN_HEADERS_VERSION}.tar.gz" \
        | tar -xz -C /tmp \
    && cmake -S /tmp/Vulkan-Headers-${VULKAN_HEADERS_VERSION} -B /tmp/vkh-build \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --install /tmp/vkh-build \
    && rm -rf /tmp/Vulkan-Headers-${VULKAN_HEADERS_VERSION} /tmp/vkh-build \
    && grep -m1 "define VK_HEADER_VERSION " /usr/local/include/vulkan/vulkan_core.h

# meson and ninja from PyPI; apt's meson on jammy is too old for the meson wraps.
# configure builds its own venv from this one, so tomli and pyyaml must be here
# too; the apt copies in /usr/lib are not visible to it.
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir meson ninja tomli pyyaml
ENV PATH=/opt/venv/bin:$PATH

# Rust for the DSP JIT and the ISO->XISO converter, plus the Android target.
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo
ENV PATH=/opt/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain stable \
    && rustup target add aarch64-linux-android

# Android SDK: command-line tools plus the versions pinned in android/README.md
# and android/app/build.gradle.kts.
ENV ANDROID_HOME=/opt/android-sdk ANDROID_SDK_ROOT=/opt/android-sdk
ENV PATH=/opt/android-sdk/cmdline-tools/latest/bin:/opt/android-sdk/platform-tools:$PATH
RUN mkdir -p $ANDROID_HOME/cmdline-tools \
    && curl -fsSL -o /tmp/cmdline-tools.zip "$ANDROID_CMDLINE_TOOLS_URL" \
    && unzip -q /tmp/cmdline-tools.zip -d $ANDROID_HOME/cmdline-tools \
    && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest \
    && rm /tmp/cmdline-tools.zip \
    && yes | sdkmanager --licenses >/dev/null \
    && sdkmanager "platform-tools" "platforms;android-36" "build-tools;36.1.0" \
        "ndk;30.0.15729638" "cmake;3.22.1"

# Unprivileged user. -o permits UID 0 for rootless Docker, where container root
# is the host user and bind-mounted files stay correctly owned.
RUN groupadd -o -g "$GID" dev \
    && useradd -o -m -u "$UID" -g "$GID" -s /bin/bash dev \
    && mkdir -p /home/dev/.gradle /home/dev/.cache/ccache /opt/cargo/registry \
    && chown -R "$UID:$GID" /home/dev /opt/cargo /opt/rustup

ENV CCACHE_DIR=/home/dev/.cache/ccache
ENV GRADLE_USER_HOME=/home/dev/.gradle
USER dev
WORKDIR /work
CMD ["bash"]
