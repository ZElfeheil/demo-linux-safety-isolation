# Original User Request

## Initial Request — 2026-07-30T17:22:52Z

An interactive ARM64 Linux 6.6 safety isolation demonstration system highlighting the contrast between software synchronization (mutex) and hardware-enforced memory protection (PTE walking & SMMUv3).

Working directory: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation
Integrity mode: development

## Requirements

### R1. Complete 7-Scenario Architecture Implementation
Implement all kernel C modules (safety_mem.ko, bad_driver.ko, mutex_threads.ko, ctx_monitor.ko, smmu_guard.ko) and C++20 userspace applications (monitor, harness, devmem, analysis) according to the established docs/implementation_plan.md.

### R2. Automated Verification & Interactive TUI
Provide both an interactive presenter mode (harness --interactive) with 4-beat Q&A flow and an automated headless verification suite (harness --auto --scenario all).

## Acceptance Criteria

### Execution & Security Verification
- [ ] All C kernel modules compile cleanly against ARM64 Linux 6.6 with zero sparse or smatch warnings.
- [ ] C++20 userspace code builds cleanly under -Wall -Wextra and passes clang-tidy (C++ Core Guidelines).
- [ ] Unit tests pass under ASan, UBSan, and TSan (zero data races in std::jthread dashboard loops).
- [ ] harness --auto --scenario all successfully executes in QEMU, validating expected output states for Scenarios B, D, and F.
- [ ] analysis binary generates results/comparison_table.md matching expected latency and protection status metrics.
