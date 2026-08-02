# Handoff Report — Milestone 1 Review & Criticism

## 1. Observation

A comprehensive review of the 12 Milestone 1 files and `docs/implementation_plan.md` was conducted. The following specific observations were recorded:

1. **`Dockerfile.builder` (Lines 90-103) & `env/build_rootfs.sh` (Lines 14, 38-55)**:
   - In `Dockerfile.builder` Stage 5 (`rootfs-builder`):
     ```dockerfile
     COPY --from=module-builder /demo/kernel/ /demo/rootfs_modules_src/
     COPY --from=userspace-builder /demo/build/bin/ /demo/rootfs_bin_src/

     RUN mkdir -p /demo/rootfs/modules /demo/rootfs/bin \
         && find /demo/rootfs_modules_src/ -name "*.ko" -exec cp {} /demo/rootfs/modules/ \; \
         && cp -r /demo/rootfs_bin_src/* /demo/rootfs/bin/ \
         && chmod +x /demo/build_rootfs.sh \
         && /demo/build_rootfs.sh
     ```
   - In `env/build_rootfs.sh`:
     - Line 8: `ROOTFS_DIR="${ROOTFS_DIR:-/demo/rootfs}"`
     - Line 10: `MODULES_SRC="${MODULES_SRC:-/demo/kernel}"`
     - Line 11: `BIN_SRC="${BIN_SRC:-/demo/build/bin}"`
     - Line 14: `rm -rf "${ROOTFS_DIR}"`
     - Line 38: `if ls "${MODULES_SRC}"/*.ko >/dev/null 2>&1; then ...`
     - Line 48: `for app in harness monitor devmem analysis; do if [[ -f "${BIN_SRC}/${app}" ]]; then ...`

2. **`docker-compose.yml` (Lines 10-12, 20-22)**:
   - `outputs` property is declared under service level instead of build block level:
     ```yaml
     services:
       build:
         build:
           context: .
           dockerfile: Dockerfile.builder
           target: artifacts
         outputs:
           - type: local
             dest: ./out
     ```
   - Command `docker compose config` produces error:
     `validating docker-compose.yml: services.build Additional property outputs is not allowed`

3. **`Dockerfile.builder` (Stage 7, Line 126) & `cmake/CMakePresets.json`**:
   - `Dockerfile.builder` Stage 7 invokes:
     `RUN cmake -S /demo/userspace -B /demo/build-xray --preset xray -DCMAKE_TOOLCHAIN_FILE=/demo/cmake/aarch64-toolchain-clang.cmake -GNinja`
   - `CMakePresets.json` is located at `cmake/CMakePresets.json`. It is NOT copied to `/demo/userspace/CMakePresets.json`.
   - `cmake` searches for presets in source directory `-S` (`/demo/userspace`).

4. **`.github/workflows/build.yml` (Lines 66, 71, 156, 185)**:
   - Static analysis and test steps suppress failures with `|| true`:
     - Line 66: `make -C kernel CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- || true`
     - Line 71: `smatch --project=kernel kernel/safety_mem/safety_mem.c || true`
     - Line 156: `ctest --test-dir userspace/build-asan --output-on-failure || true`
     - Line 185: `ctest --test-dir userspace/build-tsan --output-on-failure || true`

5. **`docker-compose.yml` (Lines 15-22)**:
   - Service `xray` targets `userspace-xray-builder` which inherits from `base` (full Ubuntu 22.04 environment). `outputs: type=local` dumps the root directory.

---

## 2. Logic Chain

1. **Empty RootFS Defect**:
   - `Dockerfile.builder` stage 5 manually copies `.ko` modules to `/demo/rootfs/modules/` and binaries to `/demo/rootfs/bin/`, then executes `/demo/build_rootfs.sh`.
   - `build_rootfs.sh` immediately executes `rm -rf /demo/rootfs`, erasing all files copied in the previous step.
   - `build_rootfs.sh` then looks for `.ko` files in `${MODULES_SRC}` (defaulting to `/demo/kernel`) and binaries in `${BIN_SRC}` (defaulting to `/demo/build/bin`). Neither directory exists in the `rootfs-builder` container stage (which copies to `/demo/rootfs_modules_src` and `/demo/rootfs_bin_src`).
   - Consequently, `build_rootfs.sh` packages an initramfs (`initramfs.cpio.gz`) that contains zero `.ko` kernel modules and zero demo binaries. The QEMU virtual machine will boot into a shell missing all required demo binaries (`harness`, `monitor`, `devmem`, `analysis`) and kernel modules.

2. **Docker Compose Schema Validation Error**:
   - In Compose Specification, `outputs` is a valid option under `services.<name>.build.outputs`, NOT `services.<name>.outputs`.
   - Running `docker compose run build` or `docker compose config` fails schema validation.

3. **CMake Preset Path Mismatch**:
   - `cmake --preset <name>` requires `CMakePresets.json` to exist in the source root passed via `-S` (`/demo/userspace`).
   - Because `CMakePresets.json` is stored under `cmake/CMakePresets.json`, `cmake --preset xray` in stage 7 will fail with "Could not read presets from /demo/userspace".

4. **CI Suppression of Failures**:
   - The implementation plan mandates that all 6 CI jobs must pass before code is merged to `main`.
   - Adding `|| true` to static analysis (`sparse`, `smatch`) and dynamic test suites (`ctest` under ASan/UBSan and TSan) causes failed analysis/tests to return exit status 0. CI marks failed jobs as successful, masking regressions and bugs.

---

## 3. Caveats

- The C++ userspace source code files (`userspace/`) and kernel module source files (`kernel/`) belong to subsequent milestones and were out of scope for this build infrastructure review.
- Docker container build execution was validated structurally and syntactically; full execution of kernel compilation within Docker requires Docker daemon privileges in host environment.

---

## 4. Conclusion

**Verdict**: **REQUEST_CHANGES**

### Findings Summary

#### [Critical] Finding 1: `build_rootfs.sh` Wipes RootFS Pre-Populated Files & Fails to Include Demo Artifacts
- **Location**: `Dockerfile.builder` (lines 90-103), `env/build_rootfs.sh` (lines 14, 38-55)
- **Why**: `rm -rf /demo/rootfs` inside `build_rootfs.sh` deletes pre-copied modules/binaries. Environment variables `MODULES_SRC` and `BIN_SRC` are not supplied, defaulting to missing paths (`/demo/kernel`, `/demo/build/bin`).
- **Suggestion**: Set `ENV MODULES_SRC=/demo/rootfs_modules_src` and `ENV BIN_SRC=/demo/rootfs_bin_src` in `Dockerfile.builder` and invoke `/demo/build_rootfs.sh` directly, removing redundant manual `cp` prior to script execution.

#### [Major] Finding 2: `docker-compose.yml` Indentation Error for `outputs`
- **Location**: `docker-compose.yml` (lines 10-12, 20-22)
- **Why**: `outputs:` placed at service level instead of under `build:`. `docker compose` fails schema validation.
- **Suggestion**: Indent `outputs:` inside the `build:` sub-block for both `build` and `xray` services.

#### [Major] Finding 3: Missing `CMakePresets.json` in `userspace/` for Preset Build
- **Location**: `Dockerfile.builder` (line 126), `cmake/CMakePresets.json`
- **Why**: `cmake -S /demo/userspace --preset xray` fails because `CMakePresets.json` is located in `cmake/` instead of `userspace/`.
- **Suggestion**: Symlink or place `CMakePresets.json` in `userspace/` or adjust preset invocation.

#### [Major] Finding 4: CI Workflow Masking Failure Exit Codes with `|| true`
- **Location**: `.github/workflows/build.yml` (lines 66, 71, 156, 185)
- **Why**: `|| true` suppresses non-zero exit codes from static analyzers (`sparse`, `smatch`) and sanitizer test suites (`ctest`).
- **Suggestion**: Remove `|| true` so genuine test/analysis failures halt the CI workflow.

#### [Minor] Finding 5: `xray` Compose Service Dumps Full OS Filesystem
- **Location**: `docker-compose.yml` (lines 14-22), `Dockerfile.builder` (lines 114-131)
- **Why**: `userspace-xray-builder` inherits from `base` (Ubuntu OS image). `type=local` export dumps the entire container root filesystem.
- **Suggestion**: Add a lightweight `scratch` export stage for XRay artifacts similar to `artifacts`.

---

## 5. Verification Method

To verify the findings independently:

1. **Verify Compose Schema Validation Error**:
   ```bash
   docker compose config
   ```
   *Expected result*: Validation error: `services.build Additional property outputs is not allowed`.

2. **Inspect RootFS Build Script & Dockerfile logic**:
   - Inspect `Dockerfile.builder` lines 98-102.
   - Inspect `env/build_rootfs.sh` lines 8-15, 38-55.
   - Confirm `rm -rf /demo/rootfs` executes after files are copied to `/demo/rootfs`, and confirm default `MODULES_SRC` and `BIN_SRC` paths don't exist in `rootfs-builder`.

3. **Verify Preset File Location**:
   - Run `cmake -S userspace -B userspace/build-xray --preset xray`
   *Expected result*: `CMake Error: Could not read presets from userspace`.

4. **Inspect CI Workflow**:
   - Inspect `.github/workflows/build.yml` lines 66, 71, 156, 185 for trailing `|| true`.
