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
mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,proc,sys,dev,dev/pts,tmp,modules,results,root,mnt/host}

# ---------------------------------------------------------------------
# 1. Install Busybox Static Binary & Symlinks (aarch64)
# ---------------------------------------------------------------------
echo "[*] Installing Busybox userspace utilities..."
BUSYBOX_BIN=""
if [[ -f /bin/busybox-aarch64 ]]; then
    BUSYBOX_BIN="/bin/busybox-aarch64"
elif [[ -f /usr/bin/busybox-aarch64 ]]; then
    BUSYBOX_BIN="/usr/bin/busybox-aarch64"
elif [[ -f /bin/busybox ]] && file /bin/busybox 2>/dev/null | grep -q "ARM aarch64"; then
    BUSYBOX_BIN="/bin/busybox"
fi

if [[ -n "${BUSYBOX_BIN}" ]]; then
    cp "${BUSYBOX_BIN}" "${ROOTFS_DIR}/bin/busybox"
else
    echo "[*] Downloading static aarch64 Busybox binary..."
    wget -q -O "${ROOTFS_DIR}/bin/busybox" https://busybox.net/downloads/binaries/1.35.0-aarch64-linux-musl/busybox || {
        echo "[-] Error: Failed to acquire static aarch64 BusyBox binary! A static aarch64 BusyBox binary is required." >&2
        exit 1
    }
fi

chmod 4755 "${ROOTFS_DIR}/bin/busybox"
# Create symlinks for standard UNIX commands
"${ROOTFS_DIR}/bin/busybox" --install -s "${ROOTFS_DIR}/bin" 2>/dev/null || true

# ---------------------------------------------------------------------
# 2. Copy Kernel Modules (.ko)
# ---------------------------------------------------------------------
echo "[*] Installing kernel modules into /modules..."
KO_FILES=$(find "${MODULES_SRC}" -name "*.ko" 2>/dev/null || true)
if [[ -n "${KO_FILES}" ]]; then
    for ko in ${KO_FILES}; do
        cp -v "${ko}" "${ROOTFS_DIR}/modules/"
    done
else
    echo "[!] Warning: No .ko files found in ${MODULES_SRC}."
    if ! ls "${ROOTFS_DIR}/modules"/*.ko >/dev/null 2>&1; then
        echo "[-] Notice: Module directory in rootfs has no .ko files yet." >&2
    fi
fi

# ---------------------------------------------------------------------
# 3. Copy Compiled C++20 Binaries into /bin/
# ---------------------------------------------------------------------
echo "[*] Installing C++20 demo binaries into /bin..."
for app in harness monitor devmem analysis; do
    if [[ -f "${BIN_SRC}/${app}" ]]; then
        cp -v "${BIN_SRC}/${app}" "${ROOTFS_DIR}/bin/${app}"
        chmod +x "${ROOTFS_DIR}/bin/${app}"
    elif [[ -f "${ROOTFS_DIR}/bin/${app}" ]]; then
        echo "[+] Binary ${app} already present in ${ROOTFS_DIR}/bin/."
    else
        echo "[!] Warning: ${app} binary not found in ${BIN_SRC} or ${ROOTFS_DIR}/bin/."
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
OUT_DIR="$(cd "${OUT_DIR}" 2>/dev/null && pwd || echo "${OUT_DIR}")"
cd "${ROOTFS_DIR}"
find . -print0 | cpio --null -ov --format=newc --owner=0:0 | gzip -9 > "${OUT_DIR}/initramfs.cpio.gz"

echo "[+] initramfs.cpio.gz successfully created at ${OUT_DIR}/initramfs.cpio.gz"
echo "[+] Total initramfs size: $(du -h "${OUT_DIR}/initramfs.cpio.gz" | cut -f1)"
