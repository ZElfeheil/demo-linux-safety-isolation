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
mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,etc/dropbear,proc,sys,dev,dev/pts,tmp,modules,results,root,mnt/host,usr/bin,usr/sbin,lib,usr/lib,lib64,var/run,var/log}

if [[ -d /opt/aarch64-root ]]; then
    echo "[*] Copying ARM64 shared libraries and runtime files..."
    cp -rn /opt/aarch64-root/* "${ROOTFS_DIR}/" 2>/dev/null || true
fi

# Dropbear's crypto dependencies are present in the builder sysroot but are
# not all part of the packages extracted into /opt/aarch64-root.
for runtime_lib in libcrypt.so.1 libgmp.so.10; do
    for lib_path in "/lib/aarch64-linux-gnu/${runtime_lib}" "/usr/lib/aarch64-linux-gnu/${runtime_lib}" "/lib/${runtime_lib}"; do
        if [[ -f "${lib_path}" ]]; then
            mkdir -p "${ROOTFS_DIR}/lib/aarch64-linux-gnu"
            cp -L "${lib_path}" "${ROOTFS_DIR}/lib/aarch64-linux-gnu/${runtime_lib}"
            break
        fi
    done
done

# Ensure dynamic linker interpreter /lib/ld-linux-aarch64.so.1 is populated
mkdir -p "${ROOTFS_DIR}/lib" "${ROOTFS_DIR}/lib64" "${ROOTFS_DIR}/usr/lib" "${ROOTFS_DIR}/etc"
for ld_location in "${ROOTFS_DIR}/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1" "${ROOTFS_DIR}/usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1" "${ROOTFS_DIR}/lib/ld-linux-aarch64.so.1"; do
    if [[ -f "${ld_location}" ]]; then
        echo "[+] Found dynamic interpreter at ${ld_location}. Creating symlinks..."
        ln -sf "${ld_location#${ROOTFS_DIR}}" "${ROOTFS_DIR}/lib/ld-linux-aarch64.so.1"
        ln -sf "${ld_location#${ROOTFS_DIR}}" "${ROOTFS_DIR}/lib64/ld-linux-aarch64.so.1"
        break
    fi
done

echo "[*] Creating flat library symlinks in /lib and /usr/lib for all shared objects..."
cat << 'LDCONF' > "${ROOTFS_DIR}/etc/ld.so.conf"
/lib
/usr/lib
/lib/aarch64-linux-gnu
/usr/lib/aarch64-linux-gnu
LDCONF

for so_file in $(find "${ROOTFS_DIR}"/lib "${ROOTFS_DIR}"/usr/lib -name "*.so*" 2>/dev/null); do
    so_name="$(basename "${so_file}")"
    target_rel="${so_file#${ROOTFS_DIR}}"
    if [[ ! -e "${ROOTFS_DIR}/lib/${so_name}" ]]; then
        ln -sf "${target_rel}" "${ROOTFS_DIR}/lib/${so_name}"
    fi
    if [[ ! -e "${ROOTFS_DIR}/usr/lib/${so_name}" ]]; then
        ln -sf "${target_rel}" "${ROOTFS_DIR}/usr/lib/${so_name}"
    fi
done

# ---------------------------------------------------------------------
# 1. Install Busybox Static Binary & Symlinks (aarch64)
# ---------------------------------------------------------------------
echo "[*] Installing Busybox userspace utilities..."
BUSYBOX_BIN=""
for candidate in /bin/busybox-aarch64 /usr/bin/busybox-aarch64 /bin/busybox; do
    if [[ -f "${candidate}" ]] && file -b "${candidate}" 2>/dev/null | grep -qi "ELF 64-bit.*aarch64\|ELF 64-bit.*ARM"; then
        BUSYBOX_BIN="${candidate}"
        break
    fi
done

if [[ -n "${BUSYBOX_BIN}" ]]; then
    cp "${BUSYBOX_BIN}" "${ROOTFS_DIR}/bin/busybox"
else
    echo "[*] Downloading static aarch64 Busybox binary..."
    wget -q -O "${ROOTFS_DIR}/bin/busybox" https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv8l || {
        echo "[-] Error: Failed to acquire static aarch64 BusyBox binary! A static aarch64 BusyBox binary is required." >&2
        exit 1
    }
fi

chmod 4755 "${ROOTFS_DIR}/bin/busybox"
# Create symlinks for standard UNIX commands (explicitly to support cross-building without qemu-user)
for cmd in sh ash bash ls cat echo mount umount mdev insmod rmmod modprobe dmesg clear ps kill sleep mkdir touch chmod chown sync poweroff reboot halt uname du find grep ifconfig route ip; do
    ln -sf busybox "${ROOTFS_DIR}/bin/${cmd}"
done

# Minimal root account used only by the local QEMU SSH forwarding endpoint.
cat << 'PASSWD_EOF' > "${ROOTFS_DIR}/etc/passwd"
root:x:0:0:root:/root:/bin/sh
PASSWD_EOF
cat << 'SHADOW_EOF' > "${ROOTFS_DIR}/etc/shadow"
root::19793:0:99999:7:::
SHADOW_EOF
chmod 600 "${ROOTFS_DIR}/etc/shadow"

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
# 2b. Generate /bin/reset_demo helper script
# ---------------------------------------------------------------------
echo "[*] Generating /bin/reset_demo script..."
cat << 'RESET_EOF' > "${ROOTFS_DIR}/bin/reset_demo"
#!/bin/sh
# reset_demo - Unload and reload all demo kernel modules cleanly

rmmod_wait() {
    local mod="$1"
    if grep -q "^${mod}" /proc/modules 2>/dev/null; then
        local i=0
        while [ $i -lt 5 ]; do
            rmmod "${mod}" 2>/dev/null && echo "  [ok] rmmod ${mod}" && return 0
            echo "  [..] waiting for ${mod} kthread to stop..."
            sleep 1
            i=$((i+1))
        done
        echo "  [!] rmmod ${mod} failed after retries"
        return 1
    fi
}

echo "[reset] Unloading demo modules..."
# rogue_thread must stop first (its kthread writes continuously)
rmmod_wait rogue_thread
sleep 1   # give kthread time to exit

# Then the rest in dependency order
for mod in safety_mem smmu_guard mutex_threads bad_driver ctx_monitor; do
    rmmod_wait "${mod}"
done

echo "[reset] Reloading base modules..."
insmod /modules/ctx_monitor.ko  && echo "  [ok] ctx_monitor" || echo "  [!] ctx_monitor failed"
insmod /modules/safety_mem.ko   && echo "  [ok] safety_mem"  || echo "  [!] safety_mem failed"

echo "[reset] Done. State is clean."
echo ""
echo "  /proc/safety_mem_status:"
cat /proc/safety_mem_status 2>/dev/null || echo "  (not available)"
RESET_EOF
chmod +x "${ROOTFS_DIR}/bin/reset_demo"

# ---------------------------------------------------------------------
# 2c. Generate individual run_demo_scenario_xx scripts
# ---------------------------------------------------------------------
echo "[*] Generating run_demo_scenario_xx scripts..."

for sc in "b:Rogue Thread - Unsynchronized Write" \
          "d:Bad Driver - Physical Linear Map Bypass" \
          "e:SMMU Active - CPU Write Bypasses SMMU" \
          "f:Full CTX + SMMU Hardware Isolation" \
          "g:Mutex Metadata Attack"; do
    id=$(echo "$sc" | cut -d':' -f1)
    name=$(echo "$sc" | cut -d':' -f2)
    script_path="${ROOTFS_DIR}/bin/run_demo_scenario_${id}"

    cat << EOF > "$script_path"
#!/bin/sh
# run_demo_scenario_${id} - Run safety isolation scenario ${id} with status monitoring

DIVIDER="============================================================"
THIN="------------------------------------------------------------"

show_status() {
    echo "\${THIN}"
    echo "  /proc/safety_mem_status:"
    cat /proc/safety_mem_status 2>/dev/null || echo "  (module not loaded)"
    PHYS=\$(cat /proc/safety_mem_status 2>/dev/null | grep phys_addr | awk '{print \$NF}')
    if [ -n "\$PHYS" ]; then
        echo "  devmem \$PHYS => \$(devmem \$PHYS 2>/dev/null || echo 'error')"
    fi
    echo "\${THIN}"
}

# Ensure base modules are loaded
insmod /modules/ctx_monitor.ko 2>/dev/null
insmod /modules/safety_mem.ko 2>/dev/null

echo "\${DIVIDER}"
echo "  SCENARIO ${id}: ${name}"
echo "\${DIVIDER}"

echo ""
echo ">>> [BEFORE] State:"
show_status

echo ""
echo ">>> Running: harness --scenario ${id}"
harness --scenario "${id}"
sleep 1

echo ""
echo ">>> [AFTER] State:"
show_status

echo ""
echo ">>> Kernel Event Log (dmesg):"
echo "\${THIN}"
dmesg | tail -n 12
echo "\${THIN}"

echo ""
echo ">>> Resetting..."
reset_demo
EOF
    chmod +x "$script_path"
done

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

# Create /etc/profile for shell sessions
cat << 'PEOF' > "${ROOTFS_DIR}/etc/profile"
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export TERM=xterm-256color
export LANG=C.UTF-8
export LC_ALL=C.UTF-8
export LC_CTYPE=C.UTF-8
PEOF

# Generate PID 1 Startup Script (/init)
echo "[*] Generating /init startup script..."
cat << 'EOF' > "${ROOTFS_DIR}/init"
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export TERM=xterm-256color
export LANG=C.UTF-8
export LC_ALL=C.UTF-8
export LC_CTYPE=C.UTF-8

# Mount virtual filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true
mkdir -p /sys/kernel/tracing
mount -t tracefs tracefs /sys/kernel/tracing 2>/dev/null || true

# Populate device nodes and PTY pseudo-terminals
[ -c /dev/ptmx ] || mknod -m 666 /dev/ptmx c 5 2 2>/dev/null || ln -sf /dev/pts/ptmx /dev/ptmx 2>/dev/null || true
mdev -s 2>/dev/null || true

# Configure QEMU user networking and start the lightweight SSH server.
ifconfig lo up 2>/dev/null || true
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up 2>/dev/null || true
route add default gw 10.0.2.2 eth0 2>/dev/null || true
if command -v dropbear >/dev/null 2>&1; then
    mkdir -p /etc/dropbear /var/run
    dropbear -R -B -E -p 22 -P /var/run/dropbear.pid
fi

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
echo "  SSH monitor: ssh -p 2222 root@127.0.0.1"
echo ""
echo "  Run a scenario:"
echo "    # harness --scenario B"
echo "=========================================================================="
echo ""

# Start a login shell with a controlling TTY.
export ENV=/etc/profile
exec cttyhack /bin/sh -l
EOF

chmod +x "${ROOTFS_DIR}/init"

echo "[*] Listing packaged binaries in ${ROOTFS_DIR}/bin/:"
ls -la "${ROOTFS_DIR}/bin/"

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
