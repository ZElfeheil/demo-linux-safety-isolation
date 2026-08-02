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
