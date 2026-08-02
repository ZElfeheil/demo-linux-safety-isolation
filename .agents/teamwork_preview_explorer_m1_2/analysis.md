# Milestone 1 Architecture & Implementation Blueprint: Environment & Build Infrastructure

## 1. Executive Summary & Architectural Overview

Milestone 1 establishes the baseline build pipeline, kernel configuration, root filesystem packaging, and virtual machine execution environment for the **Linux Safety Isolation Demo**. 

The goal of this environment is to provide a reproducible, near-native execution platform on Apple Silicon Macs (and generic x86_64 host fallbacks) that demonstrates safety-critical kernel memory isolation mechanisms, comparing software mutexes against kernel page table protections (CPU MMU) and physical bus DMA protections (ARM SMMUv3).

### Target Execution Topology
```
┌──────────────────────────────────────────────────────────────────┐
│  Apple Silicon Mac (ARM64 host) / Linux Host                     │
│                                                                  │
│  ┌───────────────────────────────────────┐                      │
│  │  Docker Container: safety-demo-builder│                      │
│  │  aarch64-linux-gnu-gcc  (kernel C)    │ → .ko files          │
│  │  aarch64-linux-gnu-g++  (C++20)       │ → monitor, harness   │
│  │  Linux 6.6 LTS source + Kbuild        │ → kernel Image       │
│  │  busybox + cmake + ninja              │ → initramfs.cpio.gz  │
│  └──────────────────────┬────────────────┘                      │
│                         │ ./out/Image  ./out/initramfs.cpio.gz  │
│                         ▼                                        │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  QEMU ARM64 (-accel hvf — near-native on Apple Silicon)    │ │
│  │  -machine virt,iommu=smmuv3  -cpu cortex-a57  -m 512M      │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

The system is decoupled into four primary environment configuration artifacts located in `env/`:
1. `env/kernel.config`: Minimal ARM64 Linux 6.6 defconfig extension containing required debugging, module support, tracing, IOMMU/SMMUv3 features, and vulnerability-demonstration flags.
2. `env/Makefile`: Top-level orchestration Makefile driving Docker build stages, host dependency validation, artifact export to `out/`, static analysis triggers, and QEMU execution.
3. `env/build_rootfs.sh`: Root filesystem builder script assembling busybox userland, compiled kernel modules (`.ko`), C++20 binaries (`harness`, `monitor`, `devmem`, `analysis`), `/init` startup environment, and `initramfs.cpio.gz` packaging.
4. `env/run_qemu.sh`: Host architecture auto-detecting QEMU launch script configuring HVF acceleration on Apple Silicon (or TCG fallback), ARM SMMUv3 virtualization, serial console routing, and host VirtFS sharing.

---

## 2. Kernel Configuration Blueprint (`env/kernel.config`)

### 2.1 Complete Required Configuration Flags

The kernel must be built from standard Linux 6.6 LTS source targeting `ARCH=arm64`. The baseline configuration starts from `defconfig` and applies the following mandatory key flags:

```ini
# =====================================================================
# Linux Safety Isolation Demo - Kernel Configuration (ARM64 Linux 6.6)
# Target: QEMU ARM64 virt machine with SMMUv3
# =====================================================================

# System Architecture & Machine Support
CONFIG_ARM64=y
CONFIG_ARCH_VIRT=y
CONFIG_SMP=y
CONFIG_NR_CPUS=4

# Core Debugging & Symbol Resolution
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT=y
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y

# Module Support & Dynamic Loading
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_MODULE_FORCE_UNLOAD=y

# Pseudo Filesystems & Tracing Infrastructure
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_DEBUG_FS=y
CONFIG_FTRACE=y
CONFIG_FUNCTION_TRACER=y
CONFIG_DYNAMIC_FTRACE=y

# Hardware IOMMU & SMMUv3 Support (Scenario F)
CONFIG_IOMMU_SUPPORT=y
CONFIG_IOMMU_API=y
CONFIG_ARM_SMMU_V3=y

# Serial Console & VirtFS Host Share Support
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_NET_9P=y
CONFIG_NET_9P_VIRTIO=y
CONFIG_9P_FS=y
CONFIG_9P_FS_POSIX_ACL=y
CONFIG_9P_FS_SECURITY=y

# Initramfs & Compression
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y

# Vulnerability Exposure & Demonstration Overrides (CRITICAL FOR DEMO)
CONFIG_STRICT_KERNEL_RWX=n
CONFIG_RODATA_FULL_DEFAULT_ENABLED=n
```

### 2.2 Detailed Flag Rationale & Vulnerability Exposure Mechanisms

| Flag | Value | Technical Rationale & Role in Demo |
| :--- | :---: | :--- |
| `CONFIG_DEBUG_KERNEL` | `y` | Enables kernel debugging infrastructure; prerequisite for `DEBUG_FS`, `FTRACE`, and symbol inspection tools. |
| `CONFIG_DEBUG_INFO` | `y` | Retains DWARF debug symbols needed for tracing, symbol resolution, and debugging kernel module functions. |
| `CONFIG_KALLSYMS_ALL` | `y` | Exports all non-exported kernel symbols into `/proc/kallsyms`. Required for `safety_mem.ko` and `ctx_monitor.ko` to dynamically look up internal kernel helper addresses such as `apply_to_page_range` and `flush_tlb_kernel_range`. |
| `CONFIG_MODULES` | `y` | Enables loadable kernel module (`.ko`) support (`insmod`/`rmmod`), allowing scenario modules (`safety_mem`, `bad_driver`, `mutex_threads`, `ctx_monitor`, `smmu_guard`) to be dynamically loaded. |
| `CONFIG_MODULE_UNLOAD` | `y` | Enables `rmmod` functionality, allowing the harness script to reset state cleanly between scenario executions. |
| `CONFIG_PROC_FS` | `y` | Enables `/proc` pseudo-filesystem. Crucial for custom communication channels: `/proc/safety_mem_status`, `/proc/bad_driver_ts`, `/proc/ctx_monitor_log`. |
| `CONFIG_DEBUG_FS` | `y` | Enables `/sys/kernel/debug` interface, specifically required for `trace_pipe` event streaming. |
| `CONFIG_FTRACE` | `y` | Enables Linux kernel tracing framework. |
| `CONFIG_FUNCTION_TRACER` | `y` | Enables `trace_printk()` logging. Used by kernel modules to push real-time events (`VIOLATION: mutex held`) directly into `trace_pipe` for the monitor TUI. |
| `CONFIG_ARM_SMMU_V3` | `y` | Compiles the ARM System MMU v3 driver into the kernel, enabling hardware-level DMA translation and fault detection for Scenario F. |
| `CONFIG_IOMMU_SUPPORT` | `y` | Core Linux IOMMU subsystem support; prerequisite for `ARM_SMMU_V3`. |
| `CONFIG_STRICT_KERNEL_RWX` | `n` | **Vulnerability Flag**: Disables strict default kernel memory read-only/no-execute enforcement. Allows kernel modules (`safety_mem.ko`) to dynamically manipulate PTE permission bits via custom page table walks without early kernel boot panics. |
| `CONFIG_RODATA_FULL_DEFAULT_ENABLED` | `n` | **Vulnerability Flag**: Disables automatic locking of read-only data across linear mapping PMD sections. This is essential for **Scenario D**: it allows `safety_mem` to demonstrate how `set_memory_ro` only protects the vmalloc virtual alias, while the linear map (`phys_to_virt`) alias remains writable until explicit Level 2 PMD splitting and linear PTE wrprotect walking is performed. |

---

## 3. Build Orchestration Blueprint (`env/Makefile`)

### 3.1 Overview & Responsibilities

The top-level `Makefile` orchestrates the complete lifecycle:
1. **Pre-flight Checks**: Verifies required host tools (`docker`, `qemu-system-aarch64`).
2. **Containerized Build Execution**: Triggers `docker build` using `Dockerfile.builder` to build Linux 6.6 Image, busybox, C kernel modules, and C++20 binaries in a hermetic Ubuntu 22.04 container.
3. **Artifact Extraction**: Uses Docker BuildKit filesystem export (`--output type=local,dest=out`) to place compiled `Image` and `initramfs.cpio.gz` directly into host `./out/`.
4. **Target Execution**: Invokes `env/run_qemu.sh` to launch the virtual machine.
5. **Static Analysis & Code Quality**: Exposes helper targets for running `sparse`, `smatch`, `clang-tidy`, `cppcheck`, and sanitizer builds.

### 3.2 Complete `env/Makefile` Implementation

```makefile
# =====================================================================
# Linux Safety Isolation Demo - Top-Level Orchestration Makefile
# =====================================================================

SHELL        := /usr/bin/env bash
OUT_DIR      := out
DOCKER_FILE  := Dockerfile.builder
IMAGE_NAME   := safety-demo-builder

.PHONY: all build run clean check-deps kernel-static check-userspace xray help

all: build

# ---------------------------------------------------------------------
# Pre-flight Dependency Verification
# ---------------------------------------------------------------------
check-deps:
	@echo "[*] Checking host dependencies..."
	@command -v docker >/dev/null 2>&1 || { echo "[-] Error: 'docker' command not found. Please install Docker or Podman."; exit 1; }
	@command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "[-] Error: 'qemu-system-aarch64' not found. Please install QEMU ARM64."; exit 1; }
	@echo "[+] All host dependencies present."

# ---------------------------------------------------------------------
# Primary Build Target (Containerized Build & Artifact Extraction)
# ---------------------------------------------------------------------
build: check-deps
	@echo "=== [1/2] Building artifacts inside Docker container ==="
	@mkdir -p $(OUT_DIR)
	@DOCKER_BUILDKIT=1 docker build \
		--target artifacts \
		--output type=local,dest=$(OUT_DIR) \
		-f $(DOCKER_FILE) .
	@echo "=== [2/2] Verifying generated build artifacts ==="
	@test -f $(OUT_DIR)/Image || { echo "[-] Error: $(OUT_DIR)/Image missing!"; exit 1; }
	@test -f $(OUT_DIR)/initramfs.cpio.gz || { echo "[-] Error: $(OUT_DIR)/initramfs.cpio.gz missing!"; exit 1; }
	@echo "[+] Build complete. Artifacts successfully exported to $(OUT_DIR)/"

# ---------------------------------------------------------------------
# Run QEMU Environment
# ---------------------------------------------------------------------
run: build
	@echo "=== Launching QEMU ARM64 Safety Isolation Demo ==="
	@./env/run_qemu.sh

# ---------------------------------------------------------------------
# Quality & Analysis Targets
# ---------------------------------------------------------------------
kernel-static:
	@echo "=== Running Kernel Static Analysis (sparse + smatch) ==="
	@docker build --target module-builder -f $(DOCKER_FILE) .

check-userspace:
	@echo "=== Running Userspace Static Analysis (clang-tidy + cppcheck) ==="
	@docker build --target userspace-builder -f $(DOCKER_FILE) .

xray:
	@echo "=== Building Optional Clang XRay Profile Target ==="
	@docker build --target userspace-xray-builder -t safety-demo-xray -f $(DOCKER_FILE) .

# ---------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------
clean:
	@echo "=== Cleaning build artifacts ==="
	@rm -rf $(OUT_DIR)
	@docker builder prune -f --filter "label=stage=builder" 2>/dev/null || true
	@echo "[+] Clean finished."

help:
	@echo "Linux Safety Isolation Demo - Build Infrastructure Commands:"
	@echo "  make build           Build kernel Image & initramfs.cpio.gz via Docker"
	@echo "  make run             Build and launch QEMU ARM64 virtual machine"
	@echo "  make check-deps      Verify host dependencies (docker, qemu-system-aarch64)"
	@echo "  make kernel-static   Run sparse and smatch static analysis on kernel C code"
	@echo "  make check-userspace Run clang-tidy and cppcheck on C++20 userspace"
	@echo "  make xray            Build optional Clang XRay profiling stage"
	@echo "  make clean           Remove out/ artifacts and prune build cache"
```

---

## 4. Root Filesystem Builder Blueprint (`env/build_rootfs.sh`)

### 4.1 RootFS Layout Specifications

`env/build_rootfs.sh` constructs the minimal initramfs tree required by the Linux kernel during boot. The target directory hierarchy is structured as follows:

```
/ (rootfs)
├── bin/                 # Busybox symlinks + C++20 binaries (harness, monitor, devmem, analysis)
├── sbin/                # System administration symlinks (mdev, insmod, rmmod, etc.)
├── etc/                 # System configuration (inittab, fstab, profile)
├── proc/                # Mount point for procfs
├── sys/                 # Mount point for sysfs (/sys/kernel/debug for trace_pipe)
├── dev/                 # Mount point for devtmpfs & mdev device nodes
│   └── pts/             # Mount point for devpts (pty support for tmux splits)
├── tmp/                 # Temporary working files
├── modules/             # Kernel modules (.ko): safety_mem, bad_driver, mutex_threads, rogue_thread, ctx_monitor, smmu_guard
├── results/             # Target directory for output comparison_table.md
└── init                 # Startup init script (PID 1)
```

### 4.2 Complete `env/build_rootfs.sh` Implementation

```bash
#!/usr/bin/env bash
# =====================================================================
# Linux Safety Isolation Demo - RootFS Construction & Packaging Script
# =====================================================================

set -euo pipefail

ROOTFS_DIR="${ROOTFS_DIR:-/demo/rootfs}"
OUT_DIR="${OUT_DIR:-/demo/out}"
MODULES_SRC="${MODULES_SRC:-/demo/kernel}"
BIN_SRC="${BIN_SRC:-/demo/build/bin}"

echo "[*] Creating RootFS directory structure at ${ROOTFS_DIR}..."
rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,proc,sys,dev,dev/pts,tmp,modules,results,root,mnt/host}

# ---------------------------------------------------------------------
# 1. Install Busybox Static Binary & Symlinks
# ---------------------------------------------------------------------
echo "[*] Installing Busybox userspace utilities..."
if [[ -f /bin/busybox ]]; then
    cp /bin/busybox "${ROOTFS_DIR}/bin/busybox"
elif command -v busybox >/dev/null 2>&1; then
    cp "$(command -v busybox)" "${ROOTFS_DIR}/bin/busybox"
else
    echo "[-] Error: busybox static binary not found in container build path!" >&2
    exit 1
fi

chmod 4755 "${ROOTFS_DIR}/bin/busybox"
# Create symlinks for standard UNIX commands
"${ROOTFS_DIR}/bin/busybox" --install -s "${ROOTFS_DIR}/bin"

# ---------------------------------------------------------------------
# 2. Copy Kernel Modules (.ko)
# ---------------------------------------------------------------------
echo "[*] Installing kernel modules into /modules..."
if ls "${MODULES_SRC}"/*.ko >/dev/null 2>&1; then
    cp -v "${MODULES_SRC}"/*.ko "${ROOTFS_DIR}/modules/"
else
    echo "[!] Warning: No .ko files found in ${MODULES_SRC}. Module loading will be unavailable."
fi

# ---------------------------------------------------------------------
# 3. Copy Compiled C++20 Binaries into /bin/
# ---------------------------------------------------------------------
echo "[*] Installing C++20 demo binaries into /bin..."
for app in harness monitor devmem analysis; do
    if [[ -f "${BIN_SRC}/${app}" ]]; then
        cp -v "${BIN_SRC}/${app}" "${ROOTFS_DIR}/bin/${app}"
        chmod +x "${ROOTFS_DIR}/bin/${app}"
    else
        echo "[!] Warning: ${app} binary not found in ${BIN_SRC}."
    fi
done

# ---------------------------------------------------------------------
# 4. Generate PID 1 Startup Script (/init)
# ---------------------------------------------------------------------
echo "[*] Generating /init startup script..."
cat << 'EOF' > "${ROOTFS_DIR}/init"
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export TERM=xterm-256color

# Mount virtual filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true

# Populate device nodes
echo /sbin/mdev > /proc/sys/kernel/hotplug 2>/dev/null || true
mdev -s 2>/dev/null || true

# Mount hostshare via 9p if VirtFS is configured
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host 2>/dev/null || true

# System banner
clear
echo "=========================================================================="
echo "          LINUX SAFETY ISOLATION DEMO — QEMU ARM64 ENVIRONMENT          "
echo "=========================================================================="
echo "  Kernel: $(uname -r)  | Architecture: $(uname -m)"
echo "  Modules path: /modules/"
echo "  Binaries path: /bin/ (harness, monitor, devmem, analysis)"
echo ""
echo "  To launch interactive demo flow:"
echo "    # harness --interactive"
echo "=========================================================================="
echo ""

# Spawn interactive shell on ttyAMA0 console
exec /bin/sh
EOF

chmod +x "${ROOTFS_DIR}/init"

# ---------------------------------------------------------------------
# 5. Package RootFS into initramfs.cpio.gz
# ---------------------------------------------------------------------
echo "[*] Packaging initramfs.cpio.gz..."
mkdir -p "${OUT_DIR}"
cd "${ROOTFS_DIR}"
find . -print0 | cpio --null -ov --format=newc | gzip -9 > "${OUT_DIR}/initramfs.cpio.gz"

echo "[+] initramfs.cpio.gz successfully created at ${OUT_DIR}/initramfs.cpio.gz"
echo "[+] Total initramfs size: $(du -h "${OUT_DIR}/initramfs.cpio.gz" | cut -f1)"
```

---

## 5. QEMU Execution Runner Blueprint (`env/run_qemu.sh`)

### 5.1 Architecture Detection & Acceleration Mechanics

`env/run_qemu.sh` executes QEMU ARM64 emulation on the host. Key technical requirements:
- **Host Detection**: Detects `uname -m`. On Apple Silicon Macs (`arm64`), QEMU uses `-accel hvf` (Hypervisor.framework) for near-native hardware virtualization. On x86_64 hosts or non-hvf environments, it falls back to `-accel tcg` (Tiny Code Generator).
- **Machine & SMMU Emulation**: Specifies `-machine virt,iommu=smmuv3` to emulate an ARM Virt machine with an ARM System MMU v3 controller enabled.
- **CPU & Memory Configuration**: Configures `-cpu cortex-a57` with 512MB RAM (`-m 512M`).
- **Kernel & Initrd Wiring**: Points to `out/Image` and `out/initramfs.cpio.gz`.
- **Kernel Command Line (`-append`)**: `console=ttyAMA0 nokaslr loglevel=7`. Disables KASLR (`nokaslr`) to ensure static, deterministic kernel symbol/page table virtual address mapping during live `devmem` demonstrations.
- **Host VirtFS Sharing**: Connects host working directory via 9p virtio channel (`-virtfs local,path=.,mount_tag=hostshare,security_model=none`).
- **Console Routing**: Disables QEMU graphical output (`-nographic`) and routes serial output directly to stdio (`-serial mon:stdio`), allowing `Ctrl-A x` exit hotkeys.

### 5.2 Complete `env/run_qemu.sh` Implementation

```bash
#!/usr/bin/env bash
# =====================================================================
# Linux Safety Isolation Demo - QEMU ARM64 Execution Script
# =====================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

KERNEL_IMG="${PROJECT_ROOT}/out/Image"
INITRD_IMG="${PROJECT_ROOT}/out/initramfs.cpio.gz"

# ---------------------------------------------------------------------
# Pre-flight Artifact Check
# ---------------------------------------------------------------------
if [[ ! -f "${KERNEL_IMG}" ]]; then
    echo "[-] Error: Kernel image missing at ${KERNEL_IMG}." >&2
    echo "    Please run 'make build' first." >&2
    exit 1
fi

if [[ ! -f "${INITRD_IMG}" ]]; then
    echo "[-] Error: Initramfs archive missing at ${INITRD_IMG}." >&2
    echo "    Please run 'make build' first." >&2
    exit 1
fi

# ---------------------------------------------------------------------
# Host Architecture & QEMU Accelerator Detection
# ---------------------------------------------------------------------
HOST_ARCH="$(uname -m)"
HOST_OS="$(uname -s)"
ACCEL="tcg"

if [[ "${HOST_ARCH}" == "arm64" || "${HOST_ARCH}" == "aarch64" ]]; then
    if [[ "${HOST_OS}" == "Darwin" ]]; then
        # Apple Silicon Mac -> HVF acceleration
        ACCEL="hvf"
    elif [[ "${HOST_OS}" == "Linux" && -w /dev/kvm ]]; then
        # Native Linux ARM64 with KVM rights -> KVM acceleration
        ACCEL="kvm"
    fi
fi

echo "=========================================================================="
echo " Launching QEMU ARM64 Virt Environment"
echo " Host OS: ${HOST_OS} (${HOST_ARCH})"
echo " QEMU Accelerator: -accel ${ACCEL}"
echo " Kernel Image: ${KERNEL_IMG}"
echo " Initramfs: ${INITRD_IMG}"
echo " Exit hotkey: Ctrl-A then X"
echo "=========================================================================="

# ---------------------------------------------------------------------
# QEMU System Invocation
# ---------------------------------------------------------------------
exec qemu-system-aarch64 \
  -machine virt,iommu=smmuv3 \
  -cpu cortex-a57 \
  -m 512M \
  -accel "${ACCEL}" \
  -kernel "${KERNEL_IMG}" \
  -initrd "${INITRD_IMG}" \
  -append "console=ttyAMA0 nokaslr loglevel=7" \
  -virtfs local,path="${PROJECT_ROOT}",mount_tag=hostshare,security_model=none \
  -nographic \
  -serial mon:stdio
```

---

## 6. Verification & Implementation Roadmap

To verify this blueprint independently:

1. **Kernel Config Verification**:
   Ensure `CONFIG_STRICT_KERNEL_RWX=n` and `CONFIG_RODATA_FULL_DEFAULT_ENABLED=n` are present in `env/kernel.config`. Confirm `CONFIG_ARM_SMMU_V3=y` and `CONFIG_KALLSYMS_ALL=y` are explicitly set.
2. **Build Test**:
   Execute `make build` inside the project root once the files are placed into `env/`. Validate that `out/Image` (~15–25MB) and `out/initramfs.cpio.gz` (~3–8MB) are cleanly created.
3. **Execution Test**:
   Execute `make run` or `./env/run_qemu.sh` on an Apple Silicon Mac. Confirm QEMU launches using `-accel hvf`, boots Linux 6.6 LTS within ~1.5 seconds, presents the interactive banner, and drops to `#` root prompt on `ttyAMA0`.
