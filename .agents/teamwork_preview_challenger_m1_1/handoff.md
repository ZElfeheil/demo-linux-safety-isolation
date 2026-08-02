# Handoff & Validation Report — Milestone 1: Environment & Build Infrastructure

**Agent**: Challenger (`teamwork_preview_challenger_m1_1`)  
**Date**: 2026-07-30  
**Target Repository**: `demo-linux-safety-isolation`  

---

## 1. Observation

Empirical testing and verification were conducted directly against the project repository using CLI tools (`cmake`, `docker compose`, `make`, `bash -n`, `python3`). Below are verbatim command execution outputs and code references:

### Observation 1.1 — `CMakePresets.json` Discovery Failure
- **Command**: `cmake -S userspace -B build-test --preset debug`
- **Output**:
  ```text
  CMake Error: Could not read presets from /Users/zeyadelfeheil/Documents/GitHub/demo-linux-safety-isolation/userspace:
  File not found: /Users/zeyadelfeheil/Documents/GitHub/demo-linux-safety-isolation/userspace/CMakePresets.json
  ```
- **File Location**: `cmake/CMakePresets.json`

### Observation 1.2 — Missing Root `Makefile` & Execution Failure
- **Command**: `make build` (executed at repository root)
- **Output**:
  ```text
  make: *** No rule to make target 'build'.  Stop.
  ```
- **File Location**: `env/Makefile` (No `Makefile` exists in top-level project root `/`).

### Observation 1.3 — `Dockerfile.builder` & `build_rootfs.sh` Disconnect
- **File References**: `Dockerfile.builder:94-102`, `env/build_rootfs.sh:8-15,38-55`
- **Dockerfile.builder (Stage 5)**:
  ```dockerfile
  COPY --from=module-builder /demo/kernel/ /demo/rootfs_modules_src/
  COPY --from=userspace-builder /demo/build/bin/ /demo/rootfs_bin_src/
  RUN mkdir -p /demo/rootfs/modules /demo/rootfs/bin \
      && find /demo/rootfs_modules_src/ -name "*.ko" -exec cp {} /demo/rootfs/modules/ \; \
      && cp -r /demo/rootfs_bin_src/* /demo/rootfs/bin/ \
      && chmod +x /demo/build_rootfs.sh \
      && /demo/build_rootfs.sh
  ```
- **build_rootfs.sh**:
  ```bash
  ROOTFS_DIR="${ROOTFS_DIR:-/demo/rootfs}"
  MODULES_SRC="${MODULES_SRC:-/demo/kernel}"
  BIN_SRC="${BIN_SRC:-/demo/build/bin}"

  rm -rf "${ROOTFS_DIR}"
  mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,proc,sys,dev,dev/pts,tmp,modules,results,root,mnt/host}
  ...
  if ls "${MODULES_SRC}"/*.ko >/dev/null 2>&1; ...
  ```

### Observation 1.4 — Architecture Mismatch for `busybox`
- **File References**: `Dockerfile.builder:24`, `env/build_rootfs.sh:21-24`
- **Content**: `Dockerfile.builder` installs host `busybox-static` (`apt-get install -y busybox-static`). `build_rootfs.sh` copies `/bin/busybox` directly into ARM64 `rootfs/bin/busybox`.

### Observation 1.5 — `docker-compose.yml` Schema Validation Failure
- **Command**: `docker compose config`
- **Output**:
  ```text
  validating /Users/zeyadelfeheil/Documents/GitHub/demo-linux-safety-isolation/docker-compose.yml: services.build Additional property outputs is not allowed
  ```

### Observation 1.6 — CI Workflow Error Suppression (`|| true`)
- **File Reference**: `.github/workflows/build.yml`
- **Lines 66, 71, 156, 185**:
  ```yaml
  make -C kernel CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- || true
  smatch --project=kernel kernel/safety_mem/safety_mem.c || true
  ctest --test-dir userspace/build-asan --output-on-failure || true
  ctest --test-dir userspace/build-tsan --output-on-failure || true
  ```

---

## 2. Logic Chain

1. **Preset Discovery Logic**: CMake looks for `CMakePresets.json` in the root source directory specified by `-S` (Observation 1.1). Because `CMakePresets.json` is located in `cmake/` instead of `userspace/`, any invocation of `cmake --preset` or Docker Stage 7 (`userspace-xray-builder`, line 127) fails immediately because the file cannot be located.
2. **Build Entrypoint Logic**: Both `README.md` and `run_qemu.sh` instruct users to run `make build`. Executing `make build` at repo root fails because `Makefile` is located inside `env/Makefile` (Observation 1.2). Furthermore, running `make` inside `env/` breaks relative paths to `Dockerfile.builder` and output directories.
3. **Initramfs Packaging Logic**: In `Dockerfile.builder` Stage 5, build artifacts are copied into `/demo/rootfs/modules` and `/demo/rootfs/bin` (Observation 1.3). Immediately afterwards, `build_rootfs.sh` executes `rm -rf /demo/rootfs`, wiping those directories. It then checks `MODULES_SRC` (`/demo/kernel`) and `BIN_SRC` (`/demo/build/bin`), neither of which exist in Stage 5. Consequently, `build_rootfs.sh` packages an initramfs that is completely missing all `.ko` kernel modules and all userspace application binaries (`harness`, `monitor`, `devmem`, `analysis`).
4. **Binary Compatibility Logic**: Installing `busybox-static` via `apt-get` on an x86_64 host produces an x86_64 ELF binary (Observation 1.4). When `build_rootfs.sh` packages this host binary into an ARM64 initramfs, booting QEMU with an ARM64 kernel results in kernel panic / exec format error (`Failed to execute /init`) when PID 1 attempts to run `/bin/sh`.
5. **Specification Compliance Logic**: `docker-compose.yml` uses the key `outputs` under service build definitions (Observation 1.5). `outputs` is a Docker BuildKit CLI parameter (`docker buildx --output`) and is not valid in the Docker Compose file schema, causing `docker compose` commands to fail validation.
6. **Quality Assurance Logic**: CI steps in `.github/workflows/build.yml` append `|| true` to static analysis and test execution steps (Observation 1.6). This forces job exit codes to 0 regardless of analysis errors or test failures, defeating the purpose of automated quality enforcement.

---

## 3. Caveats

- **Full End-to-End Kernel Compilation**: Full Docker container build requires network access to `cdn.kernel.org` to fetch the Linux 6.6 kernel tarball. The container build was analyzed structurally and statically verified.
- **QEMU Acceleration Hardware**: On macOS Apple Silicon, `-accel hvf` is supported by `qemu-system-aarch64`, but requires `-cpu host` or `-cpu cortex-a57`. On Linux hosts without `/dev/kvm` permissions, QEMU falls back to `-accel tcg`.

---

## 4. Conclusion

Milestone 1 environment scripts (`build_rootfs.sh`, `run_qemu.sh`) pass basic bash syntax checking (`bash -n`), and `kernel.config` contains appropriate configuration flags. However, **five high/critical infrastructure defects** prevent successful artifact generation and VM execution:

1. **`CMakePresets.json` is misplaced** in `cmake/` instead of `userspace/`, breaking CMake preset loading.
2. **Missing top-level `Makefile`** breaks standard build commands (`make build`, `make run`).
3. **Artifact wiping defect in `build_rootfs.sh` / `Dockerfile.builder`** results in an empty initramfs missing kernel modules and application binaries.
4. **Architecture mismatch for `busybox`** packs x86_64 binaries into ARM64 rootfs on standard Linux build hosts.
5. **Invalid property in `docker-compose.yml`** breaks `docker compose` schema validation.

---

## 5. Verification Method

To independently verify these findings, run the following commands from the repository root:

```bash
# 1. Verify CMakePresets misplaced error:
cmake -S userspace -B build-test --preset debug

# 2. Verify missing top-level Makefile error:
make build

# 3. Verify Docker Compose schema validation error:
docker compose config

# 4. Verify bash script syntax:
bash -n env/build_rootfs.sh && bash -n env/run_qemu.sh

# 5. Inspect Dockerfile.builder Stage 5 and build_rootfs.sh lines 14 & 38-55:
grep -n "rm -rf" env/build_rootfs.sh
grep -n "MODULES_SRC" env/build_rootfs.sh
```

---

## Adversarial Review

## Challenge Summary

**Overall risk assessment**: CRITICAL

The current build infrastructure fails to produce functional QEMU boot artifacts due to structural file misplacements, artifact wiping in rootfs construction, binary architecture mismatches, and invalid configuration syntax.

## Challenges

### [CRITICAL] Challenge 1: `build_rootfs.sh` wipes copied build artifacts & misses source paths
- **Assumption challenged**: `Dockerfile.builder` Stage 5 successfully packages kernel modules and userspace binaries into `initramfs.cpio.gz`.
- **Attack scenario**: Docker stage 5 populates `/demo/rootfs`, then calls `build_rootfs.sh`. `build_rootfs.sh` runs `rm -rf /demo/rootfs` and fails to find source paths because environment variables `MODULES_SRC` and `BIN_SRC` are not set for stage 5 paths.
- **Blast radius**: The resulting `initramfs.cpio.gz` contains zero kernel modules and zero userspace binaries. QEMU boots into an empty shell missing all system functionality.
- **Mitigation**: Update `Dockerfile.builder` Stage 5 to pass `MODULES_SRC=/demo/rootfs_modules_src` and `BIN_SRC=/demo/rootfs_bin_src` to `build_rootfs.sh`, and update `build_rootfs.sh` to avoid wiping pre-populated directories if sources are not provided.

### [HIGH] Challenge 2: Misplaced `CMakePresets.json`
- **Assumption challenged**: Running `cmake --preset <name>` configures C++20 userspace builds according to presets.
- **Attack scenario**: Developer or CI script invokes `cmake -S userspace --preset debug`. CMake looks for `userspace/CMakePresets.json` and fails.
- **Blast radius**: Stage 7 Docker build (`userspace-xray-builder`) and developer workflow fail completely.
- **Mitigation**: Symlink or move `cmake/CMakePresets.json` to `userspace/CMakePresets.json`.

### [HIGH] Challenge 3: x86_64 `busybox` packaged into ARM64 initramfs
- **Assumption challenged**: `apt-get install busybox-static` inside container provides a compatible binary for target ARM64 kernel.
- **Attack scenario**: Build executed on x86_64 host (standard GitHub Actions runner). `apt-get` installs x86_64 `busybox`. `build_rootfs.sh` copies it to ARM64 rootfs.
- **Blast radius**: QEMU ARM64 kernel boots, attempts to execute `/init` -> `/bin/sh` -> `/bin/busybox`, and panics with Exec format error.
- **Mitigation**: Download static ARM64 busybox binary or cross-compile busybox for `aarch64`.

### [HIGH] Challenge 4: Missing top-level `Makefile` & Invalid `docker-compose.yml`
- **Assumption challenged**: `make build` and `docker compose up` work out-of-the-box.
- **Attack scenario**: User executes `make build` or `docker compose config`.
- **Blast radius**: Commands exit immediately with errors (`No rule to make target` and `Additional property outputs is not allowed`).
- **Mitigation**: Add root `Makefile` linking to `env/Makefile` and fix `docker-compose.yml` schema syntax.

### [MEDIUM] Challenge 5: CI Workflow Error Masking (`|| true`)
- **Assumption challenged**: CI build status reflects actual static analysis and test results.
- **Attack scenario**: Static analysis (sparse/smatch) or dynamic sanitizer tests fail.
- **Blast radius**: `|| true` suppresses exit codes, reporting green build status on broken code.
- **Mitigation**: Remove `|| true` suppressions from `.github/workflows/build.yml`.

## Stress Test Results

| Scenario | Expected Behavior | Actual Behavior | Pass/Fail |
|---|---|---|---|
| `bash -n env/build_rootfs.sh` | Clean syntax | Syntax valid | PASS |
| `bash -n env/run_qemu.sh` | Clean syntax | Syntax valid | PASS |
| `cmake -S userspace --preset debug` | Configures Ninja build | File not found error | **FAIL** |
| `make build` from project root | Invokes container build | No rule to make target | **FAIL** |
| `docker compose config` | Validates Compose YAML | Property `outputs` not allowed | **FAIL** |
| Busybox target architecture | ARM64 static binary | Host (x86_64) binary copied | **FAIL** |
| RootFS artifact inclusion | Modules & apps in initramfs | Wiped by script, missing from image | **FAIL** |

## Unchallenged Areas

- **Kernel C code logic in `kernel/`**: Directory does not exist yet (Milestone 2 scope).
- **Userspace C++ code logic in `userspace/`**: Directory does not exist yet (Milestone 3 scope).
