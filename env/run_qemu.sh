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
QEMU_GDB="${QEMU_GDB:-0}"
QEMU_ACCEL="${QEMU_ACCEL:-}"
QEMU_GDB_PORT="${QEMU_GDB_PORT:-1234}"
QEMU_SSH_PORT="${QEMU_SSH_PORT:-2222}"

if [[ "${HOST_ARCH}" == "arm64" || "${HOST_ARCH}" == "aarch64" ]]; then
    if [[ "${HOST_OS}" == "Darwin" ]]; then
        # Apple Silicon Mac -> HVF acceleration
        ACCEL="hvf"
    elif [[ "${HOST_OS}" == "Linux" && -w /dev/kvm ]]; then
        # Native Linux ARM64 with KVM rights -> KVM acceleration
        ACCEL="kvm"
    fi
fi

if [[ -n "${QEMU_ACCEL}" ]]; then
    ACCEL="${QEMU_ACCEL}"
fi

echo "=========================================================================="
echo " Launching QEMU ARM64 Virt Environment"
echo " Host OS: ${HOST_OS} (${HOST_ARCH})"
echo " QEMU Accelerator: -accel ${ACCEL}"
echo " Kernel Image: ${KERNEL_IMG}"
echo " Initramfs: ${INITRD_IMG}"
echo " SSH forwarding: 127.0.0.1:${QEMU_SSH_PORT} -> guest:22"
if [[ "${QEMU_GDB}" == "1" ]]; then
    echo " GDB stub: tcp::${QEMU_GDB_PORT} (CPU paused until debugger connects)"
fi
echo " Exit hotkey: Ctrl-A then X"
echo "=========================================================================="

QEMU_DEBUG_ARGS=()
if [[ "${QEMU_GDB}" == "1" ]]; then
    QEMU_DEBUG_ARGS=(-S -gdb "tcp::${QEMU_GDB_PORT}")
fi

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
  -append "console=ttyAMA0 nokaslr loglevel=7 reboot=k iomem=relaxed strict-devmem=0" \
  -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${QEMU_SSH_PORT}-:22" \
  -device virtio-net-pci,netdev=net0 \
  -virtfs local,path="${PROJECT_ROOT}",mount_tag=hostshare,security_model=none \
  -nographic \
  -no-reboot \
  "${QEMU_DEBUG_ARGS[@]}" \
  -serial mon:stdio
