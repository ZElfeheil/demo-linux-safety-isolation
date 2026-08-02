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
