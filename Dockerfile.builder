# ==============================================================================
# Stage 1: Base Environment (Toolchains & Build Utilities)
# ==============================================================================
FROM ubuntu:24.04 AS base
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    g++ \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    libc6-dev-arm64-cross \
    libstdc++-13-dev-arm64-cross \
    make \
    flex \
    bison \
    libssl-dev \
    libelf-dev \
    bc \
    kmod \
    rsync \
    cmake \
    ninja-build \
    pkg-config \
    busybox-static \
    cpio \
    wget \
    xz-utils \
    git \
    tar \
    gzip \
    sparse \
    libsqlite3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build smatch from source (with fallback to github mirror if repo.or.cz TLS fails)
RUN (git clone --depth 1 https://github.com/error27/smatch.git /tmp/smatch || git clone --depth 1 https://repo.or.cz/smatch.git /tmp/smatch) \
    && (cd /tmp/smatch && make -j$(nproc) && cp smatch /usr/local/bin/) \
    && rm -rf /tmp/smatch || echo "Warning: smatch build skipped"

# Download aarch64 compatible static busybox binary for target VM rootfs
RUN wget -q -O /bin/busybox-aarch64 https://busybox.net/downloads/binaries/1.35.0-aarch64-linux-musl/busybox \
    && chmod +x /bin/busybox-aarch64 || true

# ==============================================================================
# Stage 2: Linux Kernel Builder (Linux 6.6 LTS Kernel Image)
# ==============================================================================
FROM base AS kernel-builder
WORKDIR /demo

# Download and extract kernel 6.6 LTS source
RUN wget -q https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.tar.xz \
    && tar xf linux-6.6.tar.xz \
    && rm linux-6.6.tar.xz

# Copy target kernel configuration
COPY env/kernel.config /demo/linux-6.6/.config

# Configure and compile Linux kernel image for ARM64
RUN cd /demo/linux-6.6 \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image modules \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) modules_prepare

# ==============================================================================
# Stage 3: Kernel Module Builder & Static Analysis
# ==============================================================================
FROM kernel-builder AS module-builder
WORKDIR /demo/kernel

COPY kernel/ /demo/kernel/

# Build C kernel modules against kernel 6.6 tree
RUN KBUILD_MODPOST_WARN=1 make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
         KERNEL_SRC=/demo/linux-6.6 all

# Run sparse static analysis on kernel module source code
RUN KBUILD_MODPOST_WARN=1 make CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
         KERNEL_SRC=/demo/linux-6.6 all

# Run smatch static analysis on kernel module source code
RUN KBUILD_MODPOST_WARN=1 make CHECK="smatch" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
         KERNEL_SRC=/demo/linux-6.6 all || true

# ==============================================================================
# Stage 4: Userspace C++20 Application Builder (GCC Toolchain)
# ==============================================================================
FROM base AS userspace-builder
WORKDIR /demo/userspace

COPY cmake/ /demo/cmake/
COPY userspace/ /demo/userspace/

# Configure and compile C++20 userspace applications using CMake + Ninja
RUN cmake -S /demo/userspace -B /demo/build \
          -DCMAKE_TOOLCHAIN_FILE=/demo/cmake/aarch64-toolchain.cmake \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -GNinja \
    && ninja -C /demo/build

# ==============================================================================
# Stage 5: RootFS Initramfs Packaging
# ==============================================================================
FROM base AS rootfs-builder
WORKDIR /demo

COPY env/build_rootfs.sh /demo/
COPY --from=module-builder /demo/kernel/ /demo/rootfs_modules_src/
COPY --from=userspace-builder /demo/build/bin/ /demo/rootfs_bin_src/

# Create target directory layout, copy artifacts, and package initramfs
RUN mkdir -p /demo/rootfs/modules /demo/rootfs/bin \
    && find /demo/rootfs_modules_src/ -name "*.ko" -exec cp {} /demo/rootfs/modules/ \; 2>/dev/null || true \
    && (cp -r /demo/rootfs_bin_src/* /demo/rootfs/bin/ 2>/dev/null || true) \
    && chmod +x /demo/build_rootfs.sh \
    && /demo/build_rootfs.sh

# ==============================================================================
# Stage 6: Final Scratch Stage for Artifact Export
# ==============================================================================
FROM scratch AS artifacts
COPY --from=kernel-builder /demo/linux-6.6/arch/arm64/boot/Image /Image
COPY --from=rootfs-builder /demo/out/initramfs.cpio.gz /initramfs.cpio.gz

# ==============================================================================
# Stage 7 (Optional): Userspace XRay Instrumentation Builder (Clang Toolchain)
# ==============================================================================
FROM base AS userspace-xray-builder
WORKDIR /demo/userspace

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-16 \
    lld-16 \
    llvm-16 \
    && rm -rf /var/lib/apt/lists/*

COPY cmake/ /demo/cmake/
COPY userspace/ /demo/userspace/

RUN cmake -S /demo/userspace -B /demo/build-xray \
          --preset xray \
          -DCMAKE_TOOLCHAIN_FILE=/demo/cmake/aarch64-toolchain-clang.cmake \
          -GNinja \
    && ninja -C /demo/build-xray
