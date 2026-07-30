# Linux Safety Isolation Demo

[![CI Build](https://github.com/ZElfeheil/demo-linux-safety-isolation/actions/workflows/build.yml/badge.svg)](https://github.com/ZElfeheil/demo-linux-safety-isolation/actions/workflows/build.yml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL_v2-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Linux 6.6 LTS](https://img.shields.io/badge/Linux_Kernel-6.6_LTS-orange.svg)](https://kernel.org)
[![Target: ARM64](https://img.shields.io/badge/Architecture-ARM64-red.svg)](https://arm.com)

Interactive ARM64 Linux 6.6 demonstration illustrating the boundary between software synchronization (mutex) and hardware-enforced safety isolation (PTE & SMMUv3).

---

## 🎯 Master Analogy: The Server Room Building

Everything maps to a single physical analogy to make abstract kernel mechanisms clear:

- **Safety Memory (`safety_buf`)**: A server rack inside a locked room.
- **Mutex**: A sign-in sheet on the door. (*"A sign-in sheet only works if everyone reads it."*)
- **`set_memory_ro` / PTE**: A keycard lock on the front door.
- **Linear Map Alias**: A fire exit — a second door to the same room with no keycard reader. (*"Locking the front door doesn't help if you forgot the fire exit."*)
- **DMA Bus Write**: A forklift driving through the wall. (*"Door locks stop people. They don't stop forklifts."*)
- **SMMUv3**: A reinforced concrete perimeter wall around the entire building.

---

## 🚀 Quick Start (One-Command Build & Run)

### Requirements
- Docker Desktop (or `docker compose`)
- macOS (Apple Silicon M1/M2/M3 recommended for `-accel hvf`) or Linux host
- QEMU (`qemu-system-aarch64`)

### 1. Build Everything (Inside Docker)
Cross-compiles Linux 6.6 LTS ARM64 kernel, out-of-tree kernel modules, C++20 userspace tools, and generates `initramfs.cpio.gz`:

```bash
docker compose run build
```

### 2. Launch QEMU & Interactive Demo

```bash
./env/run_qemu.sh
```

Inside the QEMU shell, run the interactive presenter harness:

```bash
harness --interactive
```

---

## 📊 Core Scenarios Breakdown

| Scenario | Protection Mechanism | Attack Vector | Result | Hardware Enforcement |
| :--- | :--- | :--- | :--- | :--- |
| **B: Mutex** | `struct mutex` lock | Rogue thread (ignores lock) | ❌ **CORRUPTED** | None (Voluntary software rule) |
| **D: CTX DMA** | `set_memory_ro` (vmalloc) | Linear map alias (`phys_to_virt`) | ❌ **CORRUPTED** | Partial (Vmalloc PTE protected; linear map PTE exposed) |
| **F: Full CTX** | PTE protection + SMMUv3 | Bus-mastering DMA write | ✅ **PROTECTED** | Full (CPU MMU + SMMUv3 bus enforcement) |

---

## 🛠️ Code Structure

```
demo-linux-safety-isolation/
├── .github/workflows/build.yml     # CI Pipeline (6 quality jobs)
├── Dockerfile.builder             # Multi-stage Docker cross-compilation
├── docker-compose.yml
├── cmake/                         # CMake toolchain & presets (ASan/TSan/Release)
├── env/                           # Kernel config, rootfs script, QEMU runner
├── kernel/                        # Out-of-tree C kernel modules
│   ├── safety_mem/                # CTX01 owner & PTE page table walker
│   ├── bad_driver/                # Attack driver (modes 0, 1, 2)
│   ├── mutex_threads/             # Cooperative vs Rogue threads
│   ├── ctx_monitor/               # SR die_notifier exception monitor
│   └── smmu_guard/                # SMMUv3 bus isolation module
└── userspace/                     # Modern C++20 applications
    ├── common/                    # Concepts, ProcReader, PhysicalMemoryView
    ├── monitor/                   # 3-jthread live dashboard
    ├── harness/                   # 4-beat interactive presenter UI
    ├── devmem/                    # Live physical memory inspector
    └── analysis/                  # Comparison table generator
```

---

## 📜 License

This project is licensed under the [GNU General Public License v2.0](LICENSE).
