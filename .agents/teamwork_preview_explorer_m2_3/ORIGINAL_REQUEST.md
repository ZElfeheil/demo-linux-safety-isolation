## 2026-07-30T17:40:25Z
You are an Explorer agent.
Working Directory: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m2_3
Project Root: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation
Project Specs: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/orchestrator/PROJECT.md

Task: Explore and design the blueprint for Kernel Modules `ctx_monitor.c`, `smmu_guard.c`, and top-level `kernel/Makefile` under Linux 6.6 LTS on ARM64.

Scope & Technical Specifications:
1. `kernel/ctx_monitor/ctx_monitor.c`:
   - `register_die_notifier()` callback registration at priority `INT_MAX` for trapping `DIE_PAGE_FAULT` exceptions.
   - Inspecting `struct die_args` and `struct pt_regs` to extract faulting PC, fault address, and process context when a page fault occurs within the protected memory range.
   - Procfs interface `/proc/ctx_monitor_log`: Exposing log of caught faults.

2. `kernel/smmu_guard/smmu_guard.c`:
   - SMMUv3 IOMMU domain DMA protection hooks / fault logging under Linux 6.6 LTS.
   - Procfs interface `/proc/smmu_guard_log`: Exposing blocked SMMU DMA access attempts.

3. `kernel/Makefile`:
   - Top-level Kernel Makefile building all 5 module directories out-of-tree (`safety_mem`, `bad_driver`, `mutex_threads`, `ctx_monitor`, `smmu_guard`).
   - Support for cross-compilation (`ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`).

4. Deliver your detailed analysis report to:
   `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m2_3/handoff.md`
   Send a completion message back to parent.
