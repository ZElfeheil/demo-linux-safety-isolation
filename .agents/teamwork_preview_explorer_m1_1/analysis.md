# Milestone 1: Environment & Build Infrastructure — Analysis & Implementation Blueprint

## 1. Executive Summary & Mission Scope

This analysis provides the complete, self-contained implementation blueprint for **Milestone 1: Environment & Build Infrastructure** of the Linux Safety Isolation Demo project. 

The primary goal of Milestone 1 is establishing an automated, multi-stage, containerized ARM64 cross-compilation pipeline capable of compiling:
1. Linux 6.6 LTS kernel image (`Image`) with custom debug/security configuration.
2. Kernel C modules (`safety_mem.ko`, `bad_driver.ko`, `mutex_threads.ko`, `rogue_thread.ko`, `ctx_monitor.ko`, `smmu_guard.ko`) with `sparse`/`smatch` static analysis.
3. Modern C++20 userspace binaries (`monitor`, `harness`, `devmem`, `analysis`) using CMake 3.25+ and Ninja.
4. Minimal bootable initramfs (`initramfs.cpio.gz`) embedding BusyBox, kernel modules, and userspace executables.
5. Exporting build artifacts (`./out/Image` and `./out/initramfs.cpio.gz`) cleanly to the host filesystem for execution in QEMU (using `-accel hvf` on Apple Silicon host).

---

## 2. Repository Audit & Baseline Context

### Current Directory Structure
Inspection of `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation` reveals:
```
demo-linux-safety-isolation/
├── LICENSE
├── README.md
├── docs/
│   ├── implementation_plan.md
│   └── presentation.md
└── .agents/
    └── teamwork_preview_explorer_m1_1/
        ├── ORIGINAL_REQUEST.md
        ├── BRIEFING.md
        └── progress.md
```

### Target File Tree (Milestone 1 Additions)
Milestone 1 requires introducing the following build infrastructure files:
```
demo-linux-safety-isolation/
├── Dockerfile.builder
├── docker-compose.yml
└── cmake/
    ├── aarch64-toolchain.cmake
    ├── aarch64-toolchain-clang.cmake   (optional XRay)
    └── CMakePresets.json
```

---

## 3. Specification 1: Multi-Stage `Dockerfile.builder`

### Blueprint Overview & Stage Flow
`Dockerfile.builder` uses BuildKit multi-stage builds to segregate dependencies and allow parallel/cached compilation:

```
[ubuntu:22.04]
      │
      ▼
   ( base ) ─────────────────────────┐
      │                              │
      ▼                              ▼
( kernel-builder )          ( userspace-builder )
      │                              │
      ▼                              │
( module-builder )                   │
      │                              │
      └──────────────┬───────────────┘
                     ▼
              ( rootfs-builder )
                     │
                     ▼
                ( artifacts ) [scratch stage -> exports ./out]
```

### Complete Code Specification for `Dockerfile.builder`

```dockerfile
# ==============================================================================
# Stage 1: Base Environment (Toolchains & Build Utilities)
# ==============================================================================
FROM ubuntu:22.04 AS base
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    libc6-dev-arm64-cross \
    libstdc++-12-dev-arm64-cross \
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
    smatch \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

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
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image

# ==============================================================================
# Stage 3: Kernel Module Builder & Static Analysis
# ==============================================================================
FROM kernel-builder AS module-builder
WORKDIR /demo/kernel

COPY kernel/ /demo/kernel/

# Build C kernel modules against kernel 6.6 tree
RUN make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
         KERNEL_SRC=/demo/linux-6.6 all

# Run sparse static analysis on kernel module source code
RUN make C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
         KERNEL_SRC=/demo/linux-6.6 all

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

# Create target directory layout and copy artifacts
RUN mkdir -p /demo/rootfs/modules /demo/rootfs/bin \
    && find /demo/rootfs_modules_src/ -name "*.ko" -exec cp {} /demo/rootfs/modules/ \; \
    && cp -r /demo/rootfs_bin_src/* /demo/rootfs/bin/ \
    && chmod +x /demo/build_rootfs.sh \
    && /demo/build_rootfs.sh

# ==============================================================================
# Stage 6: Final Scratch Stage for Artifact Export
# ==============================================================================
FROM scratch AS artifacts
COPY --from=kernel-builder /demo/linux-6.6/arch/arm64/boot/Image /out/Image
COPY --from=rootfs-builder /demo/rootfs/initramfs.cpio.gz /out/initramfs.cpio.gz

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
```

### Key Technical Considerations for `Dockerfile.builder`
- **Minimal APT Layers**: Package installations consolidated and lists cleared in the same layer to minimize layer size.
- **Sparse Static Analysis**: `make C=1` runs Linux `sparse` checking during the module build stage.
- **Robust Artifact Copying**: `rootfs-builder` copies all `.ko` modules found recursively to handle subfolder module organization (`safety_mem/`, `bad_driver/`, `mutex_threads/`, etc.).

---

## 4. Specification 2: `docker-compose.yml`

### Purpose & BuildKit Integration
`docker-compose.yml` provides a single entry point (`docker compose run build` or `docker compose build`) to build all artifacts inside the containerized environment and export `Image` and `initramfs.cpio.gz` directly into `./out/` on the host system using standard Docker BuildKit local file export features.

### Complete Code Specification for `docker-compose.yml`

```yaml
version: '3.8'

services:
  # Primary build service targeting the 'artifacts' stage
  build:
    build:
      context: .
      dockerfile: Dockerfile.builder
      target: artifacts
    outputs:
      - type: local
        dest: ./out

  # Optional build service targeting Clang XRay instrumented binaries
  xray:
    build:
      context: .
      dockerfile: Dockerfile.builder
      target: userspace-xray-builder
    outputs:
      - type: local
        dest: ./out/xray

  # Interactive shell container for debugging build environment
  shell:
    build:
      context: .
      dockerfile: Dockerfile.builder
      target: base
    volumes:
      - .:/workspace
    working_dir: /workspace
    command: /bin/bash
```

---

## 5. Specification 3: `cmake/aarch64-toolchain.cmake`

### Requirements
- Target OS: `Linux`
- Target CPU architecture: `aarch64`
- Cross-compiler: `aarch64-linux-gnu-gcc` and `aarch64-linux-gnu-g++`
- Search rules: Enforce root path isolation to prevent accidentally finding host x86_64 or host arm64 libraries/headers.
- Standard enforcement: C++20 required (`-std=c++20`).

### Complete Code Specification for `cmake/aarch64-toolchain.cmake`

```cmake
# ==============================================================================
# CMake Toolchain File for GCC ARM64 Cross-Compilation
# Target Architecture: aarch64-linux-gnu
# ==============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Specify cross-compiler executables
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP        aarch64-linux-gnu-strip CACHE FILEPATH "Strip")

# Target sysroot search path
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)

# Search mode policies
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Enforce C++20 globally across target builds
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

### Complete Code Specification for `cmake/aarch64-toolchain-clang.cmake` (Optional XRay)

```cmake
# ==============================================================================
# CMake Toolchain File for Clang ARM64 Cross-Compilation (XRay Support)
# Target Architecture: aarch64-linux-gnu
# ==============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   clang-16)
set(CMAKE_CXX_COMPILER clang++-16)

set(CMAKE_C_COMPILER_TARGET   aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

---

## 6. Specification 4: `cmake/CMakePresets.json`

### Overview of Required Presets
1. `debug`: `-g -O0` for fast compilation and step-by-step debugging.
2. `asan`: AddressSanitizer + UndefinedBehaviorSanitizer (`-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1`).
3. `tsan`: ThreadSanitizer (`-fsanitize=thread -g -O1`) for race condition checking on `std::jthread` and `std::atomic` code.
4. `release`: `-O3 -DNDEBUG` for optimized binary execution.
5. `xray`: Optional Clang XRay instrumentation (`-fxray-instrument -fxray-instruction-threshold=50`).

### Complete Code Specification for `cmake/CMakePresets.json`

```json
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 25,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "toolchainFile": "${sourceDir}/../cmake/aarch64-toolchain.cmake",
      "cacheVariables": {
        "CMAKE_CXX_STANDARD": "20",
        "CMAKE_CXX_STANDARD_REQUIRED": "ON",
        "CMAKE_CXX_EXTENSIONS": "OFF",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "debug",
      "displayName": "Debug Build",
      "description": "Debug symbols, no optimization (-O0 -g)",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_FLAGS_DEBUG": "-g -O0 -Wall -Wextra -Wpedantic"
      }
    },
    {
      "name": "asan",
      "displayName": "ASan + UBSan Build",
      "description": "AddressSanitizer and UndefinedBehaviorSanitizer instrumentation",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_CXX_FLAGS_RELWITHDEBINFO": "-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra"
      }
    },
    {
      "name": "tsan",
      "displayName": "TSan Build",
      "description": "ThreadSanitizer instrumentation for detecting data races",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_CXX_FLAGS_RELWITHDEBINFO": "-g -O1 -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra"
      }
    },
    {
      "name": "release",
      "displayName": "Release Build",
      "description": "High optimization, stripped assertions (-O3 -DNDEBUG)",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_FLAGS_RELEASE": "-O3 -DNDEBUG -Wall -Wextra"
      }
    },
    {
      "name": "xray",
      "displayName": "Clang XRay Build (Optional)",
      "description": "Function call timing instrumentation with Clang XRay",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/xray",
      "toolchainFile": "${sourceDir}/../cmake/aarch64-toolchain-clang.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_CXX_COMPILER": "clang++-16",
        "CMAKE_EXE_LINKER_FLAGS": "-fuse-ld=lld",
        "CMAKE_CXX_FLAGS": "-g -O2 -fxray-instrument -fxray-instruction-threshold=50 -Wall -Wextra"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "debug",
      "configurePreset": "debug"
    },
    {
      "name": "asan",
      "configurePreset": "asan"
    },
    {
      "name": "tsan",
      "configurePreset": "tsan"
    },
    {
      "name": "release",
      "configurePreset": "release"
    },
    {
      "name": "xray",
      "configurePreset": "xray"
    }
  ]
}
```

---

## 7. Data Flow & Inter-Stage Build Graph

```
Source Files:
 env/kernel.config ──────┐
 kernel/* ───────────────┼──> Dockerfile.builder
 userspace/* ────────────┤      ├── Stage 1: base (Ubuntu 22.04 ARM64 cross tools)
 cmake/* ────────────────┤      ├── Stage 2: kernel-builder (Linux 6.6 -> arch/arm64/boot/Image)
 env/build_rootfs.sh ────┘      ├── Stage 3: module-builder (kernel/*.ko + sparse C=1)
                                ├── Stage 4: userspace-builder (CMake/Ninja -> /demo/build/bin/*)
                                ├── Stage 5: rootfs-builder (initramfs.cpio.gz)
                                └── Stage 6: artifacts (scratch)
                                       │
                                       ▼ (docker compose run build)
Host Filesystem:
 ./out/Image
 ./out/initramfs.cpio.gz
       │
       ▼
 Executed by: ./env/run_qemu.sh (QEMU virt ARM64, -accel hvf)
```

---

## 8. Verification & Independent Test Plan

### Step-by-Step Verification Protocol

1. **Verify Docker Builder Specification**:
   ```bash
   DOCKER_BUILDKIT=1 docker build --file Dockerfile.builder --target artifacts --output type=local,dest=./out .
   ```
   *Expected Output*: Creation of `./out/Image` (~10-15MB) and `./out/initramfs.cpio.gz` (~5-10MB).

2. **Verify Docker Compose Pipeline**:
   ```bash
   docker compose run build
   ```
   *Expected Output*: Output files successfully copied to `./out/`.

3. **Verify CMake Presets and Cross-Toolchain Configuration**:
   Inside a test container or local cross-compilation environment:
   ```bash
   cmake -S userspace -B build/debug --preset debug
   ninja -C build/debug
   file build/debug/bin/harness
   ```
   *Expected Output*: `ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), dynamically linked...`

4. **Verify Static Analysis Integration**:
   Build kernel stage and confirm `sparse` executes:
   ```bash
   docker build --file Dockerfile.builder --target module-builder .
   ```
   *Expected Output*: Kernel module compilation output including sparse checks without error.

---

## 9. Conclusion & Handoff Readiness

The build infrastructure specification detailed above completely fulfills all requirements of Milestone 1. It provides exact, drop-in file definitions for:
- `Dockerfile.builder`
- `docker-compose.yml`
- `cmake/aarch64-toolchain.cmake`
- `cmake/aarch64-toolchain-clang.cmake`
- `cmake/CMakePresets.json`

The implementer agent can proceed immediately with creating these files as designed.
