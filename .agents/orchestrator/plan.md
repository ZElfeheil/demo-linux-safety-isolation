# Execution Plan — ARM64 Linux 6.6 Safety Isolation Demonstration System

## Overview
Decomposed into 6 clear milestones. Execution follows the Project Pattern with Explorer -> Worker -> Reviewer -> Challenger -> Forensic Auditor cycles per milestone.

## Milestones

### Milestone 1: Environment, Build Infrastructure & CI Pipeline
- Create Dockerfile.builder multi-stage build setup.
- Create cmake/aarch64-toolchain.cmake and env/ kernel config / scripts (build_rootfs.sh, run_qemu.sh).
- Create repository root config files: .clang-tidy, docker-compose.yml, README.md, .github/workflows/build.yml (6 CI jobs).

### Milestone 2: Kernel Modules Implementation
- C Kernel module `safety_mem.ko`: PTE walking, PMD block splitting via apply_to_page_range, set_memory_ro/rw, Memory barriers dsb/isb, /proc/safety_mem_status.
- C Kernel module `bad_driver.ko`: 3 attack modes (vmalloc direct, write without mutex, phys_to_virt linear map bypass).
- C Kernel module `mutex_threads.ko` & `rogue_thread.ko`: Thread A (safety), Thread B (coop), Thread C (rogue).
- C Kernel module `ctx_monitor.ko`: die_notifier registration at INT_MAX for DIE_PAGE_FAULT trapping.
- C Kernel module `smmu_guard.ko`: SMMUv3 IOMMU domain DMA filtering & fault logging.

### Milestone 3: Userspace Infrastructure & Core Binaries
- C++20 `common/`: scenario.hpp (Scenario concept), proc_reader.hpp (RAII /proc reader with std::expected), memory_region.hpp (PhysicalMemoryView with std::span and std::bit_cast).
- C++20 `devmem`: Physical memory inspector with read, write, and watch modes.
- C++20 `analysis`: Latency and security metric aggregation producing results/comparison_table.md.

### Milestone 4: Interactive TUI Dashboard & Harness Presenter Mode
- C++20 `monitor`: Dashboard with 3x std::jthread (mem_poller, event_streamer, renderer), terminal split view, SIGWINCH handling.
- C++20 `harness`: Interactive 4-beat flow (Setup, Question, Reveal, Explain), tmux auto-launch, module_loader RAII insmod/rmmod, Scenarios B, D, F, G implementations.

### Milestone 5: Quality Assurance & Static Analysis Suite
- Kernel static analysis: sparse (make C=1) and smatch checks.
- C++ static analysis & guidelines: clang-tidy, cppcheck.
- Runtime Sanitizer validation: ASan, UBSan, TSan unit tests verifying 0 data races in std::jthread loops.

### Milestone 6: Headless QEMU Automated Validation & Final Delivery
- Automated execution: harness --auto --scenario all in QEMU.
- Validate expected states for Scenarios B, D, and F.
- Verify generation of results/comparison_table.md.
- Forensic Auditor integrity review across full project artifact.
- Final completion handoff report to Sentinel.
