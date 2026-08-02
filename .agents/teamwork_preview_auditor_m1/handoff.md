# Forensic Audit Handoff Report — Milestone 1: Environment & Build Infrastructure

**Work Product**: Milestone 1 Infrastructure (`Dockerfile.builder`, `docker-compose.yml`, `cmake/`, `env/`, `.clang-tidy`, `.github/workflows/build.yml`, `README.md`)  
**Profile**: General Project / Integrity Forensics  
**Integrity Mode**: `development` (read from root `ORIGINAL_REQUEST.md`)  
**Verdict**: 🔴 **INTEGRITY VIOLATION**

---

## Forensic Audit Summary

| Check # | Component / File | Check Name | Result | Details |
|---|---|---|---|---|
| 1 | `.github/workflows/build.yml` | Hardcoded / Fabricated Output | 🔴 FAIL | Line 213 fabricates report using `echo "..." > xray-report.txt` instead of executing real tools |
| 2 | `.github/workflows/build.yml` | Swallowed CI Failures (`|| true`) | 🔴 FAIL | Lines 66, 71, 156, 185 append `|| true` to Sparse, Smatch, ASan/UBSan, and TSan test commands |
| 3 | `Dockerfile.builder` & `env/build_rootfs.sh` | Build Pipeline & Artifact Integrity | 🔴 FAIL | Stage 5 wipes `/demo/rootfs` and fails to set `MODULES_SRC`/`BIN_SRC`, producing empty initramfs without `.ko` modules or C++ binaries |
| 4 | `cmake/` & `docker-compose.yml` | Toolchain & Orchestration | 🟢 PASS | CMake toolchain files, presets, and Docker Compose configurations are authentic and valid |
| 5 | `env/kernel.config` & `run_qemu.sh` | Kernel Config & QEMU Execution | 🟢 PASS | Valid Linux 6.6 ARM64 kernel config with SMMUv3/IOMMU enabled and correct QEMU launcher flags |
| 6 | `.clang-tidy` & `README.md` | Code Quality & Documentation | 🟢 PASS | Comprehensive Clang-Tidy C++20 ruleset and clear master documentation |

---

## 1. Observation

### Observation 1.1: Fabricated Report Artifact in CI Workflow
* **Location**: `.github/workflows/build.yml`, Lines 211-214
* **Verbatim Code**:
```yaml
      - name: Generate XRay Profile Report
        run: |
          echo "XRay instrumentation profile generated successfully." > xray-report.txt
```
* **Impact**: Rather than executing the compiled XRay binary or invoking LLVM profiling tools to generate authentic timing data, the job creates a hardcoded static string artifact (`echo "..." > xray-report.txt`), representing a **Fabricated Verification Output (Prohibited Pattern #3)**.

### Observation 1.2: Error Swallowing (`|| true`) across CI Verification Jobs
* **Location**: `.github/workflows/build.yml`, Lines 66, 71, 156, 185
* **Verbatim Code**:
  - Line 66 (`kernel-static`):
    ```yaml
    make -C kernel CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- || true
    ```
  - Line 71 (`kernel-static`):
    ```yaml
    smatch --project=kernel kernel/safety_mem/safety_mem.c || true
    ```
  - Line 156 (`asan-ubsan`):
    ```yaml
    ctest --test-dir userspace/build-asan --output-on-failure || true
    ```
  - Line 185 (`tsan`):
    ```yaml
    ctest --test-dir userspace/build-tsan --output-on-failure || true
    ```
* **Impact**: Appending `|| true` to critical static analysis tools (Sparse, Smatch) and runtime sanitizer test suites (ASan, UBSan, TSan) forces the CI workflow steps to return exit code 0 even if static analysis fails or sanitizers detect memory corruption / data races. This violates the integrity rule against self-certifying / failure-masking test configurations.

### Observation 1.3: Defective RootFS Packaging in `Dockerfile.builder` & `build_rootfs.sh`
* **Location**: `Dockerfile.builder` (Lines 90-102) & `env/build_rootfs.sh` (Lines 8-16, 38-55)
* **Verbatim Code**:
  - `Dockerfile.builder` (Lines 94-102):
    ```dockerfile
    FROM base AS rootfs-builder
    WORKDIR /demo

    COPY env/build_rootfs.sh /demo/
    COPY --from=module-builder /demo/kernel/ /demo/rootfs_modules_src/
    COPY --from=userspace-builder /demo/build/bin/ /demo/rootfs_bin_src/

    RUN mkdir -p /demo/rootfs/modules /demo/rootfs/bin \
        && find /demo/rootfs_modules_src/ -name "*.ko" -exec cp {} /demo/rootfs/modules/ \; \
        && cp -r /demo/rootfs_bin_src/* /demo/rootfs/bin/ \
        && chmod +x /demo/build_rootfs.sh \
        && /demo/build_rootfs.sh
    ```
  - `env/build_rootfs.sh` (Lines 8-16, 38-55):
    ```bash
    ROOTFS_DIR="${ROOTFS_DIR:-/demo/rootfs}"
    MODULES_SRC="${MODULES_SRC:-/demo/kernel}"
    BIN_SRC="${BIN_SRC:-/demo/build/bin}"

    rm -rf "${ROOTFS_DIR}"
    mkdir -p "${ROOTFS_DIR}"/{bin,sbin,...}
    ```
* **Impact**:
  1. `Dockerfile.builder` copies built artifacts to `/demo/rootfs_modules_src/` and `/demo/rootfs_bin_src/` and places them in `/demo/rootfs/`.
  2. Next, line 102 invokes `/demo/build_rootfs.sh`.
  3. `build_rootfs.sh` line 14 immediately runs `rm -rf /demo/rootfs`, destroying all newly copied modules and binaries.
  4. `build_rootfs.sh` then looks for modules in `MODULES_SRC` (`/demo/kernel`) and binaries in `BIN_SRC` (`/demo/build/bin`). Neither environment variable is set in `Dockerfile.builder`, nor do those default paths exist in the `rootfs-builder` stage.
  5. `build_rootfs.sh` prints warnings (`Warning: No .ko files found`, `Warning: app binary not found`) and builds an `initramfs.cpio.gz` archive containing ONLY Busybox and `/init`, missing ALL kernel modules (`safety_mem.ko`, `bad_driver.ko`, etc.) and ALL C++ binaries (`harness`, `monitor`, `devmem`, `analysis`).

---

## 2. Logic Chain

1. **Premise 1**: An authentic build infrastructure deliverable must produce functional build outputs (kernel image AND complete initramfs containing all required kernel modules and userspace binaries) and must enforce real quality gates without swallowing errors or fabricating reports.
2. **Step 1 (Tracing Artifact Build Pipeline)**: In `Dockerfile.builder`, stage 5 executes `build_rootfs.sh`. `build_rootfs.sh` executes `rm -rf /demo/rootfs` and fails to locate `.ko` files or compiled C++ binaries because `MODULES_SRC` and `BIN_SRC` are unset and default to paths (`/demo/kernel` and `/demo/build/bin`) that do not contain the compiled binaries in stage 5.
3. **Inference 1**: The resulting `initramfs.cpio.gz` artifact generated by `docker build --target artifacts` is defective/empty, lacking all project kernel modules and userspace binaries required for QEMU scenario execution.
4. **Step 2 (Tracing CI Verification Workflow)**: In `.github/workflows/build.yml`, jobs `kernel-static`, `asan-ubsan`, and `tsan` append `|| true` to all check and test commands (`make -C kernel CHECK="sparse" C=1 ... || true`, `smatch ... || true`, `ctest ... || true`).
5. **Inference 2**: Any failure in static analysis (Sparse/Smatch) or runtime sanitizer unit tests (ASan/UBSan/TSan data races) is suppressed, guaranteeing a green check in GitHub Actions regardless of code quality or test failure.
6. **Step 3 (Tracing Report Generation)**: In `.github/workflows/build.yml`, job `xray-profile` writes a fixed text string to `xray-report.txt` via shell echo rather than executing the profiled target or LLVM report tools.
7. **Inference 3**: This constitutes a fabricated verification output.
8. **Conclusion**: The combination of failure-masking `|| true` in CI, fake artifact generation, and a broken rootfs packaging step violates integrity standards for Milestone 1.

---

## 3. Caveats

- **Scope Limit**: Only Milestone 1 files (`Dockerfile.builder`, `docker-compose.yml`, `cmake/`, `env/`, `.clang-tidy`, `.github/workflows/build.yml`, `README.md`) were audited. Kernel C module implementations and userspace C++ source code in `kernel/` and `userspace/` were inspected solely to verify build script paths and dependencies.
- **Environment Context**: Integrity mode is set to `development` in `ORIGINAL_REQUEST.md`. While code reuse and external tools are allowed under `development` mode, hardcoded test/report outputs, failure-swallowing verification scripts, and non-functional artifact packaging pipelines remain strictly prohibited across all integrity modes.

---

## 4. Conclusion

**Verdict**: 🔴 **INTEGRITY VIOLATION**

Milestone 1 work products CANNOT be accepted in their current state due to the following required remediations:
1. **Fix `Dockerfile.builder` & `env/build_rootfs.sh` RootFS packaging**:
   - Pass `ENV MODULES_SRC=/demo/rootfs_modules_src` and `ENV BIN_SRC=/demo/rootfs_bin_src` in `Dockerfile.builder` before calling `build_rootfs.sh`, OR update `build_rootfs.sh` to avoid deleting pre-copied files in `ROOTFS_DIR`.
2. **Remove `|| true` error masking in `.github/workflows/build.yml`**:
   - Remove `|| true` from lines 66, 71, 156, and 185 so that Sparse, Smatch, ASan/UBSan, and TSan failures correctly fail CI checks when violations occur.
3. **Replace fabricated XRay report generation in `.github/workflows/build.yml`**:
   - Update line 213 to execute real XRay profiling commands or build outputs instead of `echo "..." > xray-report.txt`.

---

## 5. Verification Method

To independently verify these findings:

1. **Verify RootFS Packaging Bug**:
   ```bash
   # Inspect Dockerfile.builder lines 94-102 and env/build_rootfs.sh lines 14-55
   cat Dockerfile.builder | grep -A 10 "rootfs-builder"
   cat env/build_rootfs.sh | grep -E "(rm -rf|MODULES_SRC|BIN_SRC)"
   ```
   *Expected Observation*: `build_rootfs.sh` executes `rm -rf /demo/rootfs` and looks for `${MODULES_SRC}` (`/demo/kernel`) and `${BIN_SRC}` (`/demo/build/bin`), which do not contain the Stage 3/4 outputs in `rootfs-builder`.

2. **Verify CI Error Swallowing**:
   ```bash
   grep -n "|| true" .github/workflows/build.yml
   ```
   *Expected Output*: Lines 66, 71, 156, 185 containing `|| true` attached to analysis/test execution commands.

3. **Verify Fabricated Report Generation**:
   ```bash
   grep -n -C 3 "xray-report.txt" .github/workflows/build.yml
   ```
   *Expected Output*: Line 213 writing static string via `echo` to `xray-report.txt`.
