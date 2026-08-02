# Project: ARM64 Linux 6.6 Safety Isolation Demonstration System

## Architecture
```
┌──────────────────────────────────────────────────────────────────┐
│  Apple Silicon Mac (ARM64 host)                                  │
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
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | Environment & Build Infra | Dockerfile.builder, Makefile, CMake, cmake/aarch64-toolchain.cmake, .clang-tidy, GitHub Actions CI workflow | None | DONE |
| 2 | Kernel Modules | C modules: safety_mem, bad_driver, mutex_threads, rogue_thread, ctx_monitor, smmu_guard | M1 | DONE |
| 3 | Userspace Core & Utilities | C++20 common/ (scenario.hpp, proc_reader.hpp, memory_region.hpp), devmem binary, analysis binary | M1 | DONE |
| 4 | TUI Dashboard & Harness | C++20 monitor (jthreads, renderer), harness (scenarios B, D, F, G, tmux launcher, interactive/auto CLI) | M2, M3 | DONE |
| 5 | Static Analysis & Quality Suite | Kernel sparse & smatch, C++ clang-tidy, ASan, UBSan, TSan validation unit tests | M2, M3, M4 | DONE |
| 6 | E2E QEMU Validation & Table Gen | Headless QEMU execution of all scenarios (B, D, F), output comparison_table.md, end-to-end verification | M4, M5 | IN_PROGRESS |

## Interface Contracts

### Kernel /proc Interfaces
- `/proc/safety_mem_status`: Outputs virt_addr, phys_addr, value_via_vmalloc, value_via_phys, ctx_protected, smmu_active, mutex_owner, status.
- `/proc/bad_driver_ts`: Timestamp of last attack write.
- `/proc/ctx_monitor_log`: Timestamp, faulting PC, fault address for DIE_PAGE_FAULT in safety memory range.
- `/proc/smmu_guard_log`: Log of blocked SMMU write attempts.

### Userspace Scenarios & Harness CLI
- `harness --interactive`: Launches tmux with monitor in left pane and harness standard 4-beat flow in right pane.
- `harness --auto --scenario all`: Unattended verification mode for QEMU testing.
- `devmem read|write|watch <phys_addr>`: Low-level physical memory access tool.
- `analysis --output <path>`: Aggregates metrics and generates `/results/comparison_table.md`.

## Code Layout
```
demo-linux-safety-isolation/
├── .github/workflows/build.yml
├── .clang-tidy
├── README.md
├── docker-compose.yml
├── Dockerfile.builder
├── cmake/aarch64-toolchain.cmake
├── env/
│   ├── Makefile
│   ├── kernel.config
│   ├── build_rootfs.sh
│   └── run_qemu.sh
├── kernel/
│   ├── Makefile
│   ├── safety_mem/safety_mem.c
│   ├── bad_driver/bad_driver.c
│   ├── mutex_threads/
│   │   ├── mutex_threads.c
│   │   └── rogue_thread.c
│   ├── ctx_monitor/ctx_monitor.c
│   └── smmu_guard/smmu_guard.c
├── userspace/
│   ├── CMakeLists.txt
│   ├── common/
│   │   ├── scenario.hpp
│   │   ├── proc_reader.hpp
│   │   └── memory_region.hpp
│   ├── monitor/
│   │   ├── main.cpp
│   │   └── renderer.cpp
│   ├── harness/
│   │   ├── main.cpp
│   │   ├── interactive.hpp
│   │   ├── interactive.cpp
│   │   ├── module_loader.cpp
│   │   └── scenarios/ (scenario_b, scenario_d, scenario_f, scenario_g)
│   ├── devmem/
│   │   ├── main.cpp
│   │   └── phys_view.cpp
│   └── analysis/main.cpp
└── results/comparison_table.md
```
