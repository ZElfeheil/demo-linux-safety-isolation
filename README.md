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
