# Handoff Report — Milestone 1 Build Infrastructure Verification & Stress Test

## 1. Observation

Direct inspections of `Dockerfile.builder`, `docker-compose.yml`, `env/Makefile`, `.github/workflows/build.yml`, and `env/build_rootfs.sh` revealed 7 critical discrepancies across path references, stage names, artifact output destinations, build target semantics, and static analysis execution models:

1. **Artifact Export Output Path Subdirectory Nesting**:
   - `Dockerfile.builder` (lines 107-109):
     ```dockerfile
     FROM scratch AS artifacts
     COPY --from=kernel-builder /demo/linux-6.6/arch/arm64/boot/Image /out/Image
     COPY --from=rootfs-builder /demo/rootfs/initramfs.cpio.gz /out/initramfs.cpio.gz
     ```
   - `docker-compose.yml` (lines 10-12):
     ```yaml
     outputs:
       - type: local
         dest: ./out
     ```
   - `env/Makefile` (lines 29-32):
     ```makefile
     DOCKER_BUILDKIT=1 docker build --target artifacts --output type=local,dest=$(OUT_DIR) -f $(DOCKER_FILE) .
     ```
   - `.github/workflows/build.yml` (line 29):
     ```yaml
     docker build --target artifacts --output type=local,dest=./out .
     ```
   - BuildKit exports the stage filesystem root (`/`) to `dest`. Because the files inside stage `artifacts` are placed in `/out/Image` and `/out/initramfs.cpio.gz`, exporting root to `./out` outputs files to `./out/out/Image` and `./out/out/initramfs.cpio.gz`.
   - Verification checks in `env/Makefile` (lines 34-35), `.github/workflows/build.yml` (lines 33-34), and `env/run_qemu.sh` (lines 11-12) look for `./out/Image` and `./out/initramfs.cpio.gz`, failing with missing file errors.

2. **RootFS Initramfs Build Script Output Path Mismatch**:
   - `env/build_rootfs.sh` (lines 9 & 109):
     ```bash
     OUT_DIR="${OUT_DIR:-/demo/out}"
     find . -print0 | cpio --null -ov --format=newc | gzip -9 > "${OUT_DIR}/initramfs.cpio.gz"
     ```
   - `Dockerfile.builder` Stage 6 (`artifacts`, line 109):
     ```dockerfile
     COPY --from=rootfs-builder /demo/rootfs/initramfs.cpio.gz /out/initramfs.cpio.gz
     ```
   - `build_rootfs.sh` generates the archive at `/demo/out/initramfs.cpio.gz`, but `Dockerfile.builder` attempts to copy from `/demo/rootfs/initramfs.cpio.gz`. This causes `docker build` to fail with file not found.

3. **RootFS Workspace Destruction & Source Path Mismatch**:
   - `Dockerfile.builder` Stage 5 (`rootfs-builder`, lines 94-102):
     ```dockerfile
     COPY --from=module-builder /demo/kernel/ /demo/rootfs_modules_src/
     COPY --from=userspace-builder /demo/build/bin/ /demo/rootfs_bin_src/
     RUN mkdir -p /demo/rootfs/modules /demo/rootfs/bin \
         && find /demo/rootfs_modules_src/ -name "*.ko" -exec cp {} /demo/rootfs/modules/ \; \
         && cp -r /demo/rootfs_bin_src/* /demo/rootfs/bin/ \
         && chmod +x /demo/build_rootfs.sh \
         && /demo/build_rootfs.sh
     ```
   - `env/build_rootfs.sh` (lines 8-15):
     ```bash
     ROOTFS_DIR="${ROOTFS_DIR:-/demo/rootfs}"
     rm -rf "${ROOTFS_DIR}"
     ```
   - `build_rootfs.sh` starts with `rm -rf /demo/rootfs`, immediately deleting all `.ko` modules and userspace binaries pre-populated by `Dockerfile.builder`. Furthermore, `build_rootfs.sh` expects source modules in `${MODULES_SRC}` (`/demo/kernel`) and binaries in `${BIN_SRC}` (`/demo/build/bin`), which do not exist in the `rootfs-builder` stage context.

4. **CI Workflow vs. Makefile Execution Model Divergence for Static Analysis**:
   - `env/Makefile` (lines 48-55):
     ```makefile
     kernel-static:
         docker build --target module-builder -f $(DOCKER_FILE) .
     check-userspace:
         docker build --target userspace-builder -f $(DOCKER_FILE) .
     ```
   - `.github/workflows/build.yml` (lines 66 & 96):
     ```yaml
     # Job 2: kernel-static
     make -C kernel CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- || true
     # Job 3: clang-tidy
     clang-tidy-16 -p userspace/build ...
     ```
   - `env/Makefile` relies on Docker containers containing the cross-compiler and Linux 6.6 kernel source tree (`/demo/linux-6.6`). In contrast, `.github/workflows/build.yml` attempts to execute build and analysis tools directly on the GitHub Actions host runner runner without mounting kernel source or header trees.

5. **Clang XRay Target Export & Toolchain Parameter Discrepancy**:
   - `docker-compose.yml` (lines 15-22) exports XRay binaries locally:
     ```yaml
     target: userspace-xray-builder
     outputs:
       - type: local
         dest: ./out/xray
     ```
   - `env/Makefile` (lines 56-58) builds the image tag without exporting files:
     ```makefile
     xray:
         docker build --target userspace-xray-builder -t safety-demo-xray -f $(DOCKER_FILE) .
     ```
   - `.github/workflows/build.yml` (lines 204-208) runs `cmake` natively without passing `-DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain-clang.cmake` or `--preset xray`.

6. **Shell Service Container Workspace Mount Mismatch**:
   - `docker-compose.yml` (lines 30-32):
     ```yaml
     volumes:
       - .:/workspace
     working_dir: /workspace
     ```
   - `Dockerfile.builder` uses `/demo` as `WORKDIR` across all stages (`kernel-builder`, `module-builder`, `userspace-builder`, `rootfs-builder`). Interactively running scripts inside `docker compose run shell` causes path resolution errors as scripts reference `/demo/`.

7. **Docker Builder Prune Filter Label Mismatch**:
   - `env/Makefile` (line 66):
     ```makefile
     docker builder prune -f --filter "label=stage=builder" 2>/dev/null || true
     ```
   - `Dockerfile.builder` defines no `LABEL stage=builder` in any stage, causing `make clean` prune to silently miss all build cache layers.

---

## 2. Logic Chain

1. **Output Directory Path Logic**:
   - Observation: Stage `artifacts` copies assets to `/out/Image` and `/out/initramfs.cpio.gz`.
   - Observation: BuildKit command is `docker build --target artifacts --output type=local,dest=./out`.
   - Logic: BuildKit writes container `/out/Image` into host `./out`, creating `./out/out/Image`.
   - Observation: `env/Makefile` tests `test -f out/Image`.
   - Logic: `test -f out/Image` evaluates to false because the file resides at `out/out/Image`. Build pipeline fails.

2. **RootFS Artifact Source Location Logic**:
   - Observation: `build_rootfs.sh` writes `initramfs.cpio.gz` to `/demo/out/initramfs.cpio.gz`.
   - Observation: `Dockerfile.builder` attempts `COPY --from=rootfs-builder /demo/rootfs/initramfs.cpio.gz /out/initramfs.cpio.gz`.
   - Logic: Docker cannot locate `/demo/rootfs/initramfs.cpio.gz` during stage 6 execution. Stage build aborts with non-zero exit code.

3. **RootFS Content Cleanup Logic**:
   - Observation: Stage `rootfs-builder` copies binaries into `/demo/rootfs/modules` and `/demo/rootfs/bin`.
   - Observation: Stage `rootfs-builder` executes `/demo/build_rootfs.sh`.
   - Observation: `build_rootfs.sh` line 14 runs `rm -rf /demo/rootfs`.
   - Logic: `build_rootfs.sh` deletes all files copied into `/demo/rootfs` prior to archive creation. The resulting `initramfs.cpio.gz` lacks kernel modules and userspace binaries.

4. **CI vs. Local Build Consistency Logic**:
   - Observation: `env/Makefile` executes static analysis targets via Docker (`docker build --target module-builder`).
   - Observation: `.github/workflows/build.yml` attempts host execution (`make -C kernel`).
   - Logic: Host environment lacks Linux kernel 6.6 source tree installed during Docker stage 2. Host execution fails or produces false results.

---

## 3. Caveats

- **Docker Daemon Availability**: Direct end-to-end execution of `docker build` was limited by local system Docker daemon status; analysis was derived via strict code and syntax cross-verification against Docker BuildKit specifications.
- **Scope Limit**: As specified by reviewer constraints, no implementation source files outside agent workspace were modified. Remediation requires coordinating fixes with Implementer.

---

## 4. Conclusion

The build infrastructure suffers from **7 critical configuration discrepancies**. Running `make build` or `docker compose up build` currently fails at artifact output extraction and initramfs packing stages. Addressing these path and execution mismatches is required before Milestone 1 build targets can pass cleanly.

---

## 5. Verification Method

To independently verify these findings when Docker is running:

1. **Verify Export Path Subdirectory Bug**:
   ```bash
   docker build --target artifacts --output type=local,dest=./out -f Dockerfile.builder .
   ls -d ./out/out  # Shows nested ./out/out directory instead of ./out/Image
   ```

2. **Verify RootFS Copy Bug**:
   ```bash
   docker build --target rootfs-builder -f Dockerfile.builder .
   # Fails on stage 6: COPY --from=rootfs-builder /demo/rootfs/initramfs.cpio.gz
   ```

3. **Verify CI Host Missing Headers**:
   ```bash
   make -C kernel CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
   # Fails on host runner: no rule to make target or missing Linux kernel headers
   ```
