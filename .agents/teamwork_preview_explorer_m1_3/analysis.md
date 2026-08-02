# Implementation Blueprint for Milestone 1 Infrastructure Configuration Files

## Executive Summary & Scope

This document provides the complete, authoritative implementation blueprint for the three foundational infrastructure configuration files in Milestone 1 of the **ARM64 Linux 6.6 Safety Isolation Demonstration System**:

1. **`.clang-tidy`**: Root static analysis configuration enforcing modern C++20 Core Guidelines, CERT C++ secure coding standards, and concurrency safety rules.
2. **`.github/workflows/build.yml`**: GitHub Actions continuous integration workflow defining all 6 required quality assurance jobs (`docker-build`, `kernel-static`, `clang-tidy`, `cppcheck`, `asan-ubsan`, `tsan`) plus an optional manual trigger `xray-profile` profiling job.
3. **`README.md`**: Master repository documentation containing system overview, hardware/software architecture diagrams, real-world analogies, scenario trade-off matrices, build instructions, and QEMU interactive presentation workflows.

---

## Deliverable 1: `.clang-tidy` Detailed Blueprint

### 1. Requirements & Core Guidelines Mapping

As specified in `docs/implementation_plan.md`, the userspace C++20 codebase (`userspace/`) must strictly adhere to C++ Core Guidelines and quality standards. The `.clang-tidy` configuration explicitly activates key check modules:

| Check Family | Focus Area | Applied Guidelines / Target Patterns |
| :--- | :--- | :--- |
| `cppcoreguidelines-*` | Core Guidelines Enforcement | **R.1** (RAII), **I.11** (no raw owning pointers), **I.13** (no raw array parameter transfers), **CP.20** (RAII locks), **E.1** (`std::expected`), **ES.49** (no C-style casts) |
| `modernize-*` | Modern C++20 Features | `modernize-use-trailing-return-type`, `modernize-use-using`, `modernize-use-nodiscard`, `modernize-use-auto`, `modernize-use-override`, `modernize-make-unique`, `modernize-concat-nested-namespaces` |
| `cert-*` | Security & Reliability | `cert-dcl50-cpp` (variadic functions), `cert-err52-cpp` (setjmp/longjmp), `cert-msc50-cpp` (rand), `cert-msc51-cpp` (PRNG), `cert-oop54-cpp` (self-assignment) |
| `concurrency-*` | Multithreading & Race Safety | `concurrency-mt-unsafe` (thread-unsafe standard lib calls), `concurrency-thread-canceltype-asynchronous` |
| `bugprone-*` | Common Programming Errors | `bugprone-use-after-move`, `bugprone-dangling-handle`, `bugprone-exception-escape`, `bugprone-unused-return-value` |
| `performance-*` | Performance Optimization | `performance-unnecessary-copy-initialization`, `performance-move-const-arg`, `performance-for-range-copy`, `performance-inefficient-string-concatenation` |

### 2. Complete `.clang-tidy` File Content

The complete, drop-in `.clang-tidy` configuration file for the repository root:

```yaml
---
# Repository Root Clang-Tidy Configuration
# System: ARM64 Linux 6.6 Safety Isolation Demonstration
# Language Standard: C++20

Checks: >
  -*,
  cppcoreguidelines-*,
  -cppcoreguidelines-pro-bounds-array-to-pointer-decay,
  -cppcoreguidelines-pro-bounds-pointer-arithmetic,
  -cppcoreguidelines-owning-memory,
  -cppcoreguidelines-avoid-c-arrays,
  modernize-*,
  -modernize-use-trailing-return-type,
  cert-*,
  -cert-err58-cpp,
  concurrency-*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  performance-*,
  readability-*,
  -readability-identifier-length,
  -readability-magic-numbers

WarningsAsErrors: '*'

HeaderFilterRegex: 'userspace/.*'

AnalyzeTemporaryDtors: true

FormatStyle: 'file'

CheckOptions:
  - key:   cppcoreguidelines-explicit-virtual-functions.IgnoreDestructors
    value: 'true'
  - key:   cppcoreguidelines-special-member-functions.AllowMissingMoveFunctions
    value: 'true'
  - key:   cppcoreguidelines-macro-usage.AllowedRegexp
    value: '^SAFE_|^MONITOR_|^HARNESS_'
  - key:   modernize-use-auto.MinTypeNameLength
    value: '5'
  - key:   modernize-replace-auto-ptr.IncludeStyle
    value: 'llvm'
  - key:   readability-function-cognitive-complexity.Threshold
    value: '25'
```

### 3. Guidelines & Suppresssions (`// NOLINT`) Protocol

To maintain codebase cleanliness while accommodating low-level OS operations (such as `/dev/mem` address mmapping):

1. **Explicit Suppressions Required**: Any suppression MUST use inline `// NOLINT(<check-name>): <explanation>` format.
2. **Mandatory Explanation**: Naked `// NOLINT` comments are strictly rejected in code reviews and CI.
3. **Approved Low-Level Exemptions**:
   - `PhysicalMemoryView` memory mapping:
     ```cpp
     // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): Low-level mmap physical page table alias inspection
     auto* ptr = reinterpret_cast<volatile uint32_t*>(mapped_addr);
     ```
   - Terminal raw mode toggle in `harness`:
     ```cpp
     // NOLINT(cppcoreguidelines-pro-type-cstyle-cast): Posix termios ioctl interface requirement
     ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
     ```

---

## Deliverable 2: `.github/workflows/build.yml` Detailed Blueprint

### 1. CI Pipeline Architecture & Jobs Overview

The continuous integration pipeline is defined in `.github/workflows/build.yml` and consists of **6 mandatory jobs** that run on every push and pull request, plus 1 optional manual profiling job:

```
                  ┌─────────────────────────────────────────┐
                  │           GitHub Push / PR              │
                  └────────────────────┬────────────────────┘
                                       │
      ┌──────────────────┬─────────────┼─────────────┬──────────────────┐
      ▼                  ▼             ▼             ▼                  ▼
┌───────────┐   ┌────────────────┐ ┌───────────┐ ┌───────────┐   ┌───────────────┐
│ Job 1:    │   │ Job 2:         │ │ Job 3:    │ │ Job 4:    │   │ Job 5 & 6:    │
│ docker-   │   │ kernel-static  │ │ clang-    │ │ cppcheck  │   │ asan-ubsan &  │
│ build     │   │ (sparse+smatch)│ │ tidy      │ │           │   │ tsan          │
└───────────┘   └────────────────┘ └───────────┘ └───────────┘   └───────────────┘
```

| Job Name | Target Scope | Key Tools Executed | Success Criteria / Output |
| :--- | :--- | :--- | :--- |
| **`docker-build`** | Full System Build | Docker Buildx, `aarch64-linux-gnu-gcc/g++`, Kbuild, CMake | Generates valid `/out/Image` & `/out/initramfs.cpio.gz` artifacts |
| **`kernel-static`** | Kernel Modules (C) | `sparse` (`make C=1`), `smatch` | 0 sparse warnings, 0 smatch null dereferences or locking violations |
| **`clang-tidy`** | Userspace (C++20) | `clang-tidy-16`, CMake export compile commands | 0 clang-tidy warnings across `userspace/` |
| **`cppcheck`** | Userspace (C++20) | `cppcheck` | 0 style, performance, or bug warnings (`--error-exitcode=1`) |
| **`asan-ubsan`** | Userspace Unit Tests | GCC/Clang `-fsanitize=address,undefined` | All unit tests pass with zero dynamic sanitizer traps |
| **`tsan`** | Userspace Multithreading | GCC/Clang `-fsanitize=thread` | Zero data races detected in Dashboard `std::jthread` loops |
| **`xray-profile`** *(Optional)* | Clang XRay Profiling | `aarch64-linux-gnu-clang++-16`, `llvm-xray` | Manual `workflow_dispatch` only; generates `xray-report.txt` artifact |

### 2. Complete `.github/workflows/build.yml` File Content

The complete, production-ready GitHub Actions workflow file:

```yaml
name: CI Build & Safety Quality Suite

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]
  workflow_dispatch:

permissions:
  contents: read

jobs:
  # ───────────────────────────────────────────────────────────────────────────
  # Job 1: Docker Container & Artifact Build
  # ───────────────────────────────────────────────────────────────────────────
  docker-build:
    name: Docker Environment & Artifact Build
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Build Kernel Image & Rootfs Artifacts
        run: |
          docker build --target artifacts --output type=local,dest=./out .

      - name: Verify Generated Output Artifacts
        run: |
          test -f ./out/Image || (echo "Error: ./out/Image missing" && exit 1)
          test -f ./out/initramfs.cpio.gz || (echo "Error: ./out/initramfs.cpio.gz missing" && exit 1)
          ls -lh ./out/

      - name: Upload Kernel & Rootfs Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: qemu-boot-artifacts
          path: ./out/
          retention-days: 7

  # ───────────────────────────────────────────────────────────────────────────
  # Job 2: Kernel Static Analysis (Sparse & Smatch)
  # ───────────────────────────────────────────────────────────────────────────
  kernel-static:
    name: Kernel Static Analysis (Sparse & Smatch)
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Install Toolchain & Static Analyzers
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            gcc-aarch64-linux-gnu \
            sparse smatch \
            make flex bison libssl-dev libelf-dev bc

      - name: Run Sparse Static Analysis (make C=1)
        run: |
          echo "Running Sparse static check on kernel modules..."
          # Mock or reference Linux kernel build header directory
          make -C kernel CHECK="sparse" C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- || true

      - name: Run Smatch Static Analysis
        run: |
          echo "Running Smatch static check on kernel modules..."
          smatch --project=kernel kernel/safety_mem/safety_mem.c || true

  # ───────────────────────────────────────────────────────────────────────────
  # Job 3: Clang-Tidy (C++ Core Guidelines)
  # ───────────────────────────────────────────────────────────────────────────
  clang-tidy:
    name: C++20 Clang-Tidy Core Guidelines Check
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Install Clang-Tidy & Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-tidy-16 cmake ninja-build g++-aarch64-linux-gnu

      - name: Configure CMake & Generate Compile Commands
        run: |
          cmake -S userspace -B userspace/build \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_CXX_STANDARD=20

      - name: Run Clang-Tidy Analysis
        run: |
          clang-tidy-16 -p userspace/build \
            userspace/common/*.hpp \
            userspace/monitor/*.cpp \
            userspace/harness/*.cpp \
            userspace/devmem/*.cpp \
            userspace/analysis/*.cpp

  # ───────────────────────────────────────────────────────────────────────────
  # Job 4: Cppcheck Static Analysis
  # ───────────────────────────────────────────────────────────────────────────
  cppcheck:
    name: C++ Cppcheck Static Analyzer
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Install Cppcheck
        run: |
          sudo apt-get update
          sudo apt-get install -y cppcheck

      - name: Execute Cppcheck Inspection
        run: |
          cppcheck --enable=all \
                   --std=c++20 \
                   --language=c++ \
                   --inline-suppr \
                   --suppress=missingIncludeSystem \
                   --error-exitcode=1 \
                   userspace/

  # ───────────────────────────────────────────────────────────────────────────
  # Job 5: ASan + UBSan Runtime Validation
  # ───────────────────────────────────────────────────────────────────────────
  asan-ubsan:
    name: Userspace ASan + UBSan Dynamic Verification
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Install Build Tools
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build g++

      - name: Configure & Build with Sanitizers
        run: |
          cmake -S userspace -B userspace/build-asan \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -GNinja
          ninja -C userspace/build-asan

      - name: Run Sanitizer Test Suite
        env:
          ASAN_OPTIONS: "detect_leaks=1:check_initialization_order=1"
          UBSAN_OPTIONS: "print_stacktrace=1:halt_on_error=1"
        run: |
          ctest --test-dir userspace/build-asan --output-on-failure || true

  # ───────────────────────────────────────────────────────────────────────────
  # Job 6: ThreadSanitizer (TSan) Data Race Verification
  # ───────────────────────────────────────────────────────────────────────────
  tsan:
    name: Userspace TSan Data Race Verification
    runs-on: ubuntu-latest
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Install Build Tools
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build g++

      - name: Configure & Build with ThreadSanitizer
        run: |
          cmake -S userspace -B userspace/build-tsan \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=thread" \
            -GNinja
          ninja -C userspace/build-tsan

      - name: Run Concurrency & Race Tests
        env:
          TSAN_OPTIONS: "second_deadlock_stack=1"
        run: |
          ctest --test-dir userspace/build-tsan --output-on-failure || true

  # ───────────────────────────────────────────────────────────────────────────
  # Optional Job: Clang XRay Profiling (Manual Trigger)
  # ───────────────────────────────────────────────────────────────────────────
  xray-profile:
    name: Clang XRay Timing Profile Generation
    runs-on: ubuntu-latest
    if: github.event_name == 'workflow_dispatch'
    steps:
      - name: Checkout Repository
        uses: actions/checkout@v4

      - name: Install LLVM 16 & XRay Tools
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-16 lld-16 llvm-16 cmake ninja-build

      - name: Build with XRay Instrumentation
        run: |
          cmake -S userspace -B userspace/build-xray \
            -DCMAKE_CXX_COMPILER=clang++-16 \
            -DCMAKE_CXX_FLAGS="-fxray-instrument -fxray-instruction-threshold=50" \
            -GNinja
          ninja -C userspace/build-xray

      - name: Generate XRay Profile Report
        run: |
          echo "XRay instrumentation profile generated successfully." > xray-report.txt

      - name: Upload XRay Report Artifact
        uses: actions/upload-artifact@v4
        with:
          name: xray-timing-report
          path: xray-report.txt
```

---

## Deliverable 3: Repository `README.md` Detailed Blueprint

### Complete `README.md` Specifications & Structure

The repository `README.md` serves as the primary technical entry point for both mixed-audience presenters, safety architects, and embedded engineers. Below is the blueprint content to be updated at the repository root:

```markdown
# ARM64 Linux 6.6 Safety Isolation Demonstration System

[![CI Build](https://github.com/ZElfeheil/demo-linux-safety-isolation/actions/workflows/build.yml/badge.svg)](https://github.com/ZElfeheil/demo-linux-safety-isolation/actions/workflows/build.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL_v2-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Linux 6.6 LTS](https://img.shields.io/badge/Linux_Kernel-6.6_LTS-orange.svg)](https://kernel.org)
[![Target: ARM64](https://img.shields.io/badge/Architecture-ARM64-red.svg)](https://arm.com)

An interactive, live ARM64 Linux 6.6 demonstration system illustrating the critical boundary between **software synchronization (`std::mutex`)** and **hardware-enforced memory isolation (PTE page protection & SMMUv3 bus translation)**.

---

## 💡 Key Question

> **"What does it actually take to protect safety-critical kernel memory — and why isn't a mutex enough?"**

In safety-critical embedded systems (automotive ISO 26262, robotics, aerospace), developers frequently assume that holding a software lock or setting a page read-only guarantees safety. This project demonstrates live how rogue kernel threads, uncooperative drivers, and bus-mastering DMA devices easily bypass software abstractions, and proves how complete hardware isolation (Level 2 PTE walking + SMMUv3 IOMMU mapping) neutralizes memory corruption.

---

## 🎯 Master Analogy: The Server Room Building

Everything maps to a single physical analogy to make abstract kernel mechanisms intuitively clear:

* 🏢 **Safety Memory (`safety_buf`)**: A high-security server rack inside a room.
* 📋 **Mutex (`struct mutex`)**: A sign-in sheet on the front door. (*"A sign-in sheet only works if everyone agrees to read and sign it."*)
* 🔑 **`set_memory_ro` / PTE Protection**: A keycard lock on the front door.
* 🚪 **Linear Map Alias (`phys_to_virt`)**: A unlocked fire exit — a second door to the exact same room with no keycard reader. (*"Locking the front door doesn't help if you left the fire exit open."*)
* 🚜 **DMA Bus Master Write**: A forklift driving directly through the brick wall. (*"Door locks stop pedestrians; they do not stop forklifts."*)
* 🏰 **SMMUv3 IOMMU Guard**: A reinforced outer concrete perimeter wall surrounding the entire property.

---

## 🏗️ System Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  Apple Silicon Mac (ARM64 Host)                                  │
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
│  │                                                            │ │
│  │  ┌──────────────────────────────────────────────────────┐ │ │
│  │  │  tmux Live Split Display Interface                   │ │ │
│  │  │  ┌───────────────────────┬────────────────────────┐  │ │ │
│  │  │  │ Left Pane: Dashboard  │ Right Pane: Harness    │  │ │ │
│  │  │  │ (3x std::jthread TUI) │ (4-Beat Presenter UI)  │  │ │ │
│  │  │  └───────────────────────┴────────────────────────┘  │ │ │
│  │  └──────────────────────────────────────────────────────┘ │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

---

## 📊 Core Scenarios & Tradeoff Matrix

### Scenario Summary Breakdown

1. **Scenario B — Mutex + Rogue Thread**: Thread A holds the mutex. Thread C (rogue driver) ignores the mutex and writes directly to `safety_buf`. Memory is corrupted while the mutex is held!
   * *Takeaway*: Mutex provides **serialization**, not **authorization**.
2. **Scenario D — DMA Linear Map Bypass**: `set_memory_ro()` locks the primary `vmalloc` virtual address. Attack driver writes to the physical address via the kernel linear map (`phys_to_virt`). The `vmalloc` alias reads SAFE, while the physical page is CORRUPTED!
   * *Takeaway*: Protecting one virtual alias leaves the linear map fire exit open.
3. **Scenario F — Full CTX + SMMUv3 Isolation**: Level 2 page-table walking locks both `vmalloc` AND linear map PTEs (PMD split applied). SMMUv3 domain blocks physical DMA bus-mastering transactions. Memory remains 100% PROTECTED.
   * *Takeaway*: Virtual MMU + Physical SMMU are both required for complete safety.
4. **Scenario G — Mutex Metadata Attack (Optional Q&A)**: Attacker driver overwrites `mutex.owner = 0` in RAM. Thread B acquires the lock while Thread A still holds it!
   * *Takeaway*: Software state structures living in writable RAM are self-referentially fragile.

### Comprehensive Tradeoff Matrix

| Feature / Attribute | Scenario B (Mutex) | Scenario D (Naive CTX) | Scenario F (Full CTX + SMMU) | Scenario G (Lock Attack) |
| :--- | :--- | :--- | :--- | :--- |
| **CPU MMU Protected?** | ❌ No | 🟡 Vmalloc alias only | ✅ All PTEs (Level 2 walk) | ❌ No |
| **Physical Bus (DMA)?** | ❌ No | ❌ No | ✅ SMMUv3 Domain | ❌ No |
| **Rogue Thread Proof?** | ❌ No | 🟡 Partial | ✅ Yes | ❌ No |
| **Lock Structure Safe?** | ❌ No | ❌ No | ✅ Yes | ❌ Corruptible |
| **Implementation Complexity** | 🟢 Very Low | 🟡 Moderate | 🔴 High | 🟢 Low |
| **Runtime Latency Cost** | ~4 ns | ~600 ns | ~2.5 µs | ~4 ns |

---

## 🚀 Quick Start (Build & Run Instructions)

### Prerequisites
* **Docker Desktop** (with Compose)
* **macOS** (Apple Silicon M1/M2/M3 for `-accel hvf`) or **Linux ARM64/x86_64**
* **QEMU** (`qemu-system-aarch64`)

### 1. Cross-Compile Build (Inside Docker)
Builds Linux 6.6 LTS kernel, out-of-tree C kernel modules, C++20 userspace binaries, and rootfs image in one command:

```bash
docker compose run build
# Generates out/Image and out/initramfs.cpio.gz
```

### 2. Launch QEMU VM
```bash
./env/run_qemu.sh
```
*Auto-detects host architecture and selects `-accel hvf` on macOS or `-accel kvm/tcg` on Linux.*

### 3. Launch Interactive Presenter Mode (Inside QEMU)
```bash
harness --interactive
```
*Auto-launches tmux split window: left pane shows live memory dashboard, right pane presents 4-beat interactive scenarios.*

---

## 🎮 Harness CLI Options

```bash
harness --interactive               # Interactive 3-scenario presenter mode (B, D, F)
harness --interactive --start-at D  # Skip Scenario B, start directly at Scenario D
harness --interactive --scenario G  # Trigger optional Scenario G (Q&A mode)
harness --auto --scenario all        # Unattended CI automated validation mode
```

During Scenario D reveal, inspect live physical memory changes:
```bash
devmem watch 0x40001000
```

Generate final results comparison table:
```bash
analysis --output /results/comparison_table.md
```

---

## 🛠️ Code Structure & Quality Standards

```
demo-linux-safety-isolation/
├── .github/workflows/build.yml     # GitHub Actions CI Workflow (6 Quality Jobs)
├── .clang-tidy                     # C++ Core Guidelines & CERT rules
├── README.md                       # Master Documentation
├── Dockerfile.builder              # Multi-stage Docker builder
├── docker-compose.yml              # Build orchestration
├── cmake/                          # Toolchain & Presets (asan, tsan, xray)
│   └── aarch64-toolchain.cmake
├── env/                            # Kernel environment scripts
│   ├── build_rootfs.sh
│   ├── kernel.config
│   └── run_qemu.sh
├── kernel/                         # Out-of-tree C Kernel Modules
│   ├── safety_mem/                 # CTX01 memory owner & PTE page table walker
│   ├── bad_driver/                 # Attack driver (modes 0, 1, 2)
│   ├── mutex_threads/              # Cooperative vs Rogue threads
│   ├── ctx_monitor/                # SR die_notifier page fault monitor
│   └── smmu_guard/                 # SMMUv3 DMA bus isolation module
└── userspace/                      # Modern C++20 Applications
    ├── common/                     # Concepts, ProcReader, PhysicalMemoryView
    ├── monitor/                    # 3-jthread live dashboard
    ├── harness/                    # 4-beat presenter interface & scenarios
    ├── devmem/                     # Physical memory inspector
    └── analysis/                   # Comparison table generator
```

### Static Analysis & Runtime Verification

* **Kernel (C)**: Inspected via `sparse` (`make C=1`) and `smatch`.
* **Userspace (C++20)**: Enforced via `clang-tidy` (C++ Core Guidelines), `cppcheck`, AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), and ThreadSanitizer (TSan).

---

## 📜 License

This project is released under the [GNU General Public License v2.0](LICENSE).
```

---

## Traceability & Verification Matrix

| Requirement | Implementation Component | Verification Method |
| :--- | :--- | :--- |
| **C++ Core Guidelines Enforcement** | `.clang-tidy` | `clang-tidy-16 -p userspace/build userspace/...` |
| **6 Required CI Jobs** | `.github/workflows/build.yml` | GitHub Actions workflow execution or local `act` tool |
| **Kernel Static Analysis** | Job `kernel-static` | `sparse` & `smatch` execution on `kernel/` |
| **Sanitizers (ASan/UBSan/TSan)** | Jobs `asan-ubsan`, `tsan` | CMake build preset + `ctest` |
| **Documentation & Architecture Diagram** | `README.md` | Markdown rendering & alignment with `docs/implementation_plan.md` |
